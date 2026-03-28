#include "DbgEngSession.hpp"

#include <cstdio>
#include <dbghelp.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// HRESULT constants used in initialize()
// ---------------------------------------------------------------------------

// CoInitializeEx returns this when the apartment is already initialized as STA
// by another caller on this thread. We treat it as success — dbgeng works fine
// in either apartment model from our perspective.
static constexpr HRESULT kHrComChangedMode { RPC_E_CHANGED_MODE };

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Release a COM interface pointer and null it out.
// Written as a macro so the pointer variable itself is nulled at the call site.
#define SAFE_RELEASE(ptr)           \
    do {                            \
        if ((ptr) != nullptr) {     \
            (ptr)->Release ();      \
            (ptr) = nullptr;        \
        }                           \
    } while (0)

// ---------------------------------------------------------------------------
// DbgEngSession::initialize
// ---------------------------------------------------------------------------

bool DbgEngSession::initialize ()
{
    // Step 1 — COM apartment.
    // We request MTA (COINIT_MULTITHREADED) because the dbgeng event loop and
    // the IO thread both need to make COM calls.  If the thread was already
    // initialized as STA (RPC_E_CHANGED_MODE), that is acceptable — dbgeng
    // does not require MTA, it just works better with it.
    bool isSuccess { true };
    bool isComInitialized { false };

    HRESULT hr { CoInitializeEx (nullptr, COINIT_MULTITHREADED) };
    if (FAILED (hr) and hr != kHrComChangedMode)
    {
        fprintf (stderr, "WHATDBG: CoInitializeEx failed: 0x%08lX\n",
                static_cast<unsigned long> (hr));
        isSuccess = false;
    }
    else
    {
        isComInitialized = true;
    }

    // Step 2 — dbgeng entry point.
    // DebugCreate is the correct way to obtain IDebugClient — NOT CoCreateInstance.
    if (isSuccess == true)
    {
        hr = DebugCreate (__uuidof (IDebugClient5),
                         reinterpret_cast<void**> (&clientPtr));
        if (FAILED (hr))
        {
            fprintf (stderr, "WHATDBG: DebugCreate failed: 0x%08lX\n",
                    static_cast<unsigned long> (hr));
            isSuccess = false;
        }
    }

    // Step 3 — IDebugControl4
    if (isSuccess == true)
    {
        hr = clientPtr->QueryInterface (__uuidof (IDebugControl4),
                                        reinterpret_cast<void**> (&controlPtr));
        if (FAILED (hr))
        {
            fprintf (stderr, "WHATDBG: QueryInterface(IDebugControl4) failed: 0x%08lX\n",
                    static_cast<unsigned long> (hr));
            isSuccess = false;
        }
    }

    // Step 4 — IDebugSymbols3
    if (isSuccess == true)
    {
        hr = clientPtr->QueryInterface (__uuidof (IDebugSymbols3),
                                        reinterpret_cast<void**> (&symbolsPtr));
        if (FAILED (hr))
        {
            fprintf (stderr, "WHATDBG: QueryInterface(IDebugSymbols3) failed: 0x%08lX\n",
                    static_cast<unsigned long> (hr));
            isSuccess = false;
        }
    }

    // Step 5 — IDebugRegisters2
    if (isSuccess == true)
    {
        hr = clientPtr->QueryInterface (__uuidof (IDebugRegisters2),
                                        reinterpret_cast<void**> (&registersPtr));
        if (FAILED (hr))
        {
            fprintf (stderr, "WHATDBG: QueryInterface(IDebugRegisters2) failed: 0x%08lX\n",
                    static_cast<unsigned long> (hr));
            isSuccess = false;
        }
    }

    // Step 6 — IDebugDataSpaces4
    if (isSuccess == true)
    {
        hr = clientPtr->QueryInterface (__uuidof (IDebugDataSpaces4),
                                        reinterpret_cast<void**> (&dataSpacesPtr));
        if (FAILED (hr))
        {
            fprintf (stderr, "WHATDBG: QueryInterface(IDebugDataSpaces4) failed: 0x%08lX\n",
                    static_cast<unsigned long> (hr));
            isSuccess = false;
        }
    }

    // Step 7 — IDebugSystemObjects4
    if (isSuccess == true)
    {
        hr = clientPtr->QueryInterface (__uuidof (IDebugSystemObjects4),
                                        reinterpret_cast<void**> (&systemObjectsPtr));
        if (FAILED (hr))
        {
            fprintf (stderr, "WHATDBG: QueryInterface(IDebugSystemObjects4) failed: 0x%08lX\n",
                    static_cast<unsigned long> (hr));
            isSuccess = false;
        }
    }

    if (isSuccess == false)
    {
        // Release any interfaces acquired before the failure, in reverse order.
        SAFE_RELEASE (systemObjectsPtr);
        SAFE_RELEASE (dataSpacesPtr);
        SAFE_RELEASE (registersPtr);
        SAFE_RELEASE (symbolsPtr);
        SAFE_RELEASE (controlPtr);
        SAFE_RELEASE (clientPtr);

        // Only uninitialize COM if we successfully initialized it.
        if (isComInitialized == true)
        {
            CoUninitialize ();
        }
    }

    if (isSuccess == true)
    {
        isInitialized_ = true;

        // Enable line number loading — OFF by default in programmatic dbgeng.
        // Without this, GetOffsetByLine will always return E_UNEXPECTED
        // because PDB line tables are never read.
        symbolsPtr->AddSymbolOptions (SYMOPT_LOAD_LINES);
        fprintf (stderr, "WHATDBG: SYMOPT_LOAD_LINES enabled\n");

        // Request that the engine stop at the initial loader breakpoint
        // (ntdll!LdrpDoDebuggerBreak).  Without this, dbgeng handles the
        // initial INT3 internally and never delivers it to the Exception
        // callback.  The symbol engine only becomes functional after this
        // breakpoint is processed — without it, GetOffsetByLine and Reload
        // fail with E_UNEXPECTED ("partially initialized target").
        controlPtr->AddEngineOptions (DEBUG_ENGOPT_INITIAL_BREAK);
        fprintf (stderr, "WHATDBG: DEBUG_ENGOPT_INITIAL_BREAK enabled\n");
    }

    return isSuccess;
}

// ---------------------------------------------------------------------------
// DbgEngSession::shutdown
// ---------------------------------------------------------------------------

void DbgEngSession::shutdown ()
{
    if (isInitialized_ == true)
    {
        // Detach cleanly from any active debug target.
        // DEBUG_END_ACTIVE_DETACH leaves the target process running.
        // EndSession returns E_UNEXPECTED when no target has ever been attached,
        // so guard the call with isHasTarget.  This is a best-effort call —
        // ignore the return value; we are shutting down regardless.
        if (isHasTarget == true)
        {
            clientPtr->EndSession (DEBUG_END_ACTIVE_DETACH);
        }

        // Release in reverse acquisition order (systemObjectsPtr was last acquired).
        SAFE_RELEASE (systemObjectsPtr);
        SAFE_RELEASE (dataSpacesPtr);
        SAFE_RELEASE (registersPtr);
        SAFE_RELEASE (symbolsPtr);
        SAFE_RELEASE (controlPtr);
        SAFE_RELEASE (clientPtr);

        CoUninitialize ();

        isInitialized_ = false;
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

IDebugClient5* DbgEngSession::client () const
{
    return clientPtr;
}

IDebugControl4* DbgEngSession::control () const
{
    return controlPtr;
}

IDebugSymbols3* DbgEngSession::symbols () const
{
    return symbolsPtr;
}

IDebugRegisters2* DbgEngSession::registers () const
{
    return registersPtr;
}

IDebugDataSpaces4* DbgEngSession::dataSpaces () const
{
    return dataSpacesPtr;
}

IDebugSystemObjects4* DbgEngSession::systemObjects () const
{
    return systemObjectsPtr;
}

bool DbgEngSession::isInitialized () const
{
    return isInitialized_;
}

bool DbgEngSession::hasTarget () const
{
    return isHasTarget;
}

void DbgEngSession::setHasTarget ()
{
    isHasTarget = true;
}

void DbgEngSession::clearTarget ()
{
    isHasTarget = false;
}

bool DbgEngSession::isWaitingForConfiguration () const
{
    return isWaitingForConfig;
}

void DbgEngSession::setWaitingForConfiguration (bool isWaiting)
{
    isWaitingForConfig = isWaiting;
}

// ---------------------------------------------------------------------------
// DbgEngSession::registerCallbacks
// ---------------------------------------------------------------------------

bool DbgEngSession::registerCallbacks (IDebugEventCallbacks*   eventCb,
                                       IDebugOutputCallbacks2* outputCb)
{
    bool isSuccess { true };

    HRESULT hr { clientPtr->SetEventCallbacks (eventCb) };
    if (FAILED (hr))
    {
        fprintf (stderr,
                "WHATDBG: SetEventCallbacks failed: 0x%08lX\n",
                static_cast<unsigned long> (hr));
        isSuccess = false;
    }

    if (isSuccess == true)
    {
        // SetOutputCallbacks takes IDebugOutputCallbacks*, but outputCb implements
        // IDebugOutputCallbacks2 (a separate COM interface in the dbgeng headers).
        // reinterpret_cast is required here.  dbgeng will QueryInterface the
        // registered object for IID_IDebugOutputCallbacks2 and use Output2 when
        // available — our QueryInterface implementation returns the correct pointer.
        hr = clientPtr->SetOutputCallbacks (
            reinterpret_cast<IDebugOutputCallbacks*> (outputCb));
        if (FAILED (hr))
        {
            fprintf (stderr,
                    "WHATDBG: SetOutputCallbacks failed: 0x%08lX\n",
                    static_cast<unsigned long> (hr));
            // Roll back the event callback registration so we are in a consistent
            // state — either both are registered or neither is.
            clientPtr->SetEventCallbacks (nullptr);
            isSuccess = false;
        }
    }

    return isSuccess;
}

// ---------------------------------------------------------------------------
// DbgEngSession::unregisterCallbacks
// ---------------------------------------------------------------------------

void DbgEngSession::unregisterCallbacks ()
{
    clientPtr->SetEventCallbacks (nullptr);
    clientPtr->SetOutputCallbacks (nullptr);
}

// ---------------------------------------------------------------------------
// DbgEngSession::launch
// ---------------------------------------------------------------------------

bool DbgEngSession::launch (const std::string& program,
                             const std::string& cwd,
                             const std::vector<std::string>& args)
{
    // Build the command line: quote the program path, then append each arg.
    std::string commandLine { "\"" + program + "\"" };

    for (const std::string& arg : args)
    {
        commandLine += " ";
        commandLine += arg;
    }

    fprintf (stderr, "WHATDBG: launching: %s\n", commandLine.c_str ());

    // Set the working directory if one was provided.
    // IDebugClient5 does not have a SetCurrentDirectory method.
    // Use the Win32 SetCurrentDirectoryA before CreateProcess2 so the child
    // process inherits the correct working directory.
    if (cwd.empty () == false)
    {
        SetCurrentDirectoryA (cwd.c_str ());
    }

    // DEBUG_CREATE_PROCESS_OPTIONS — request a new console window so the
    // debuggee's stdout/stderr are visible separately from whatdbg's stderr.
    DEBUG_CREATE_PROCESS_OPTIONS options {};
    options.CreateFlags  = DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE;
    options.EngCreateFlags = 0;
    options.VerifierFlags  = 0;
    options.Reserved       = 0;

    HRESULT hr { clientPtr->CreateProcess2 (
        0,                                   // Server — 0 = local
        const_cast<PSTR> (commandLine.c_str ()),
        &options,
        sizeof (options),
        nullptr,                             // InitialDirectory — already set via Win32
        nullptr) };                          // Environment — inherit from parent

    bool isSuccess { SUCCEEDED (hr) };

    if (isSuccess == true)
    {
        // Wait for the initial debug events.  CreateProcess2 triggers a
        // sequence of events that dbgeng must fully process before the
        // symbol engine becomes functional:
        //
        //   1. CreateProcess event  — callback returns NO_CHANGE (target keeps running)
        //   2. Module load events   — ntdll, kernel32, etc. (target keeps running)
        //   3. Initial breakpoint   — INT3 at ntdll!LdrpDoDebuggerBreak
        //                             Exception callback returns BREAK → target stops
        //
        // The initial breakpoint (step 3) is the critical event.  Until it
        // is processed, dbgeng considers the target "partially initialized"
        // and all symbol operations fail with E_UNEXPECTED.
        //
        // We call WaitForEvent with INFINITE timeout.  The CreateProcess
        // callback returns NO_CHANGE, so the engine keeps running through
        // module loads until the Exception callback returns BREAK at the
        // initial INT3.  At that point WaitForEvent returns S_OK.
        static constexpr DWORD kLaunchTimeoutMs { 30000 };

        HRESULT hrWait { controlPtr->WaitForEvent (0, kLaunchTimeoutMs) };

        if (SUCCEEDED (hrWait))
        {
            ULONG execStatus { 0 };
            controlPtr->GetExecutionStatus (&execStatus);

            fprintf (stderr,
                    "WHATDBG: process launched — WaitForEvent returned, execStatus=%lu\n",
                    static_cast<unsigned long> (execStatus));

            if (execStatus == DEBUG_STATUS_BREAK)
            {
                isHasTarget = true;
                isWaitingForConfig = true;

                // Force-load all module symbols NOW.  dbgeng uses deferred
                // symbol loading by default (SYMOPT_DEFERRED_LOADS is ON).
                // Without /f, PDBs are not read until explicitly demanded —
                // but GetOffsetByLine does NOT trigger demand-loading.
                // setBreakpoints runs AFTER launch returns, so symbols must
                // be ready before tryResolve calls GetOffsetByLine.
                symbolsPtr->Reload ("/f");

                fprintf (stderr, "WHATDBG: symbol engine initialized — symbols force-loaded\n");
            }
            else
            {
                // Target didn't break — might need another WaitForEvent.
                // This shouldn't happen if the Exception callback returns BREAK
                // for the initial INT3.
                fprintf (stderr, "WHATDBG: WARNING — target not in BREAK state after launch\n");
                isHasTarget = true;
                isWaitingForConfig = true;
            }
        }
        else
        {
            fprintf (stderr,
                    "WHATDBG: WaitForEvent after launch failed: 0x%08lX\n",
                    static_cast<unsigned long> (hrWait));
            isSuccess = false;
        }
    }
    else
    {
        fprintf (stderr,
                "WHATDBG: CreateProcess2 failed: 0x%08lX\n",
                static_cast<unsigned long> (hr));
    }

    return isSuccess;
}

// ---------------------------------------------------------------------------
// DbgEngSession::appendSymbolPath
// ---------------------------------------------------------------------------

void DbgEngSession::appendSymbolPath (const std::string& path)
{
    HRESULT hr { symbolsPtr->AppendSymbolPath (path.c_str ()) };

    if (SUCCEEDED (hr))
    {
        fprintf (stderr, "WHATDBG: symbol path appended: %s\n", path.c_str ());
    }
    else
    {
        fprintf (stderr, "WHATDBG: AppendSymbolPath failed: 0x%08lX\n",
            static_cast<unsigned long> (hr));
    }
}

// ---------------------------------------------------------------------------
// DbgEngSession::appendSourcePath
// ---------------------------------------------------------------------------

void DbgEngSession::appendSourcePath (const std::string& path)
{
    HRESULT hr { symbolsPtr->AppendSourcePath (path.c_str ()) };

    if (SUCCEEDED (hr))
    {
        fprintf (stderr, "WHATDBG: source path appended: %s\n", path.c_str ());
    }
    else
    {
        fprintf (stderr, "WHATDBG: AppendSourcePath failed: 0x%08lX\n",
            static_cast<unsigned long> (hr));
    }
}

// ---------------------------------------------------------------------------
// DbgEngSession::reloadSymbols
// ---------------------------------------------------------------------------

void DbgEngSession::reloadSymbols ()
{
    fprintf (stderr, "WHATDBG: reloading symbols for all modules...\n");

    HRESULT hr { symbolsPtr->Reload ("/f") };
    fprintf (stderr, "WHATDBG: Reload result hr=0x%08lX\n",
        static_cast<unsigned long> (hr));
}

// ---------------------------------------------------------------------------
// DbgEngSession::attach
// ---------------------------------------------------------------------------

bool DbgEngSession::attach (ULONG pid)
{
    fprintf (stderr, "WHATDBG: attaching to PID %lu\n",
            static_cast<unsigned long> (pid));

    // Invasive attach — breaks into the process.
    // After WaitForEvent returns S_OK, the session is fully accessible:
    // all modules enumerated, symbol loading works, breakpoints work.
    HRESULT hr { clientPtr->AttachProcess (
        0,
        pid,
        DEBUG_ATTACH_DEFAULT) };

    bool isSuccess { SUCCEEDED (hr) };

    if (isSuccess == true)
    {
        static constexpr DWORD kAttachTimeoutMs { 30000 };

        HRESULT hrWait { controlPtr->WaitForEvent (0, kAttachTimeoutMs) };

        if (SUCCEEDED (hrWait))
        {
            isHasTarget = true;
            fprintf (stderr, "WHATDBG: attached to PID %lu\n",
                    static_cast<unsigned long> (pid));

            // Diagnose: enumerate loaded modules
            ULONG loadedCount { 0 };
            ULONG unloadedCount { 0 };
            symbolsPtr->GetNumberModules (&loadedCount, &unloadedCount);
            fprintf (stderr, "WHATDBG: %lu modules loaded, %lu unloaded\n",
                static_cast<unsigned long> (loadedCount),
                static_cast<unsigned long> (unloadedCount));

            // Log each module and its symbol status
            for (ULONG i { 0 }; i < loadedCount; ++i)
            {
                ULONG64 base { 0 };
                symbolsPtr->GetModuleByIndex (i, &base);

                char name[256] { 0 };
                symbolsPtr->GetModuleNameString (
                    DEBUG_MODNAME_MODULE, i, base,
                    name, sizeof (name), nullptr);

                DEBUG_MODULE_PARAMETERS params { };
                symbolsPtr->GetModuleParameters (1, &base, 0, &params);

                fprintf (stderr, "WHATDBG:   [%lu] %s symType=%lu\n",
                    static_cast<unsigned long> (i),
                    name,
                    static_cast<unsigned long> (params.SymbolType));
            }
        }
        else
        {
            fprintf (stderr,
                    "WHATDBG: WaitForEvent after attach failed: 0x%08lX\n",
                    static_cast<unsigned long> (hrWait));
            isSuccess = false;
        }
    }
    else
    {
        fprintf (stderr,
                "WHATDBG: AttachProcess failed: 0x%08lX\n",
                static_cast<unsigned long> (hr));
    }

    return isSuccess;
}

// ---------------------------------------------------------------------------
// DbgEngSession::resume
// ---------------------------------------------------------------------------

bool DbgEngSession::resume ()
{
    HRESULT hr { controlPtr->SetExecutionStatus (DEBUG_STATUS_GO) };

    if (FAILED (hr))
    {
        fprintf (stderr,
                "WHATDBG: SetExecutionStatus(GO) failed: 0x%08lX\n",
                static_cast<unsigned long> (hr));
    }

    return SUCCEEDED (hr);
}
