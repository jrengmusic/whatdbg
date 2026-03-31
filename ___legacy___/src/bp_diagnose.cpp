// bp_diagnose.cpp
//
// Standalone breakpoint lifecycle diagnostic for whatdbg.
//
// Launches bp_test_target.exe, sets a breakpoint on targetFunction(),
// resumes execution, and verifies the Breakpoint callback fires.
// Logs everything to bp_diagnose.log AND stderr.
// No DAP involved.
//
// Usage: bp_diagnose.exe
//   (finds bp_test_target.exe in the same directory as bp_diagnose.exe)
//
// Build: see CMakeLists.txt target "bp_diagnose"

#include "DbgEngSession.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// The source file under test and the line number of the first executable
// statement in targetFunction().
// MUST match the actual line in bp_test_target.cpp — see comment there.
static constexpr const char* kTargetFile    { "C:\\Users\\jreng\\Documents\\Poems\\dev\\whatdbg\\src\\bp_test_target.cpp" };
static constexpr const char* kTargetBasename { "bp_test_target.cpp" };
static constexpr ULONG       kTargetLine    { 26 };

static constexpr const char* kSourceDir     { "C:\\Users\\jreng\\Documents\\Poems\\dev\\whatdbg\\src" };
static constexpr const char* kSymbolDir     { "C:\\Users\\jreng\\Documents\\Poems\\dev\\whatdbg\\Builds\\Ninja" };
static constexpr const char* kLogFileName   { "bp_diagnose.log" };
static constexpr ULONG       kLaunchTimeout { 30000 };
static constexpr ULONG       kResumeTimeout { 10000 };

// EXCEPTION_BREAKPOINT — INT3
static constexpr DWORD kExceptionBreakpoint { 0x80000003 };

// ---------------------------------------------------------------------------
// DiagLog — writes timestamped lines to both a FILE* and stderr.
// Uses GetTickCount64() relative to program start.
// ---------------------------------------------------------------------------

struct DiagLog
{
    FILE*    file       { nullptr };
    ULONGLONG startTick { 0 };

    void open (const char* path)
    {
        startTick = GetTickCount64 ();
        file      = fopen (path, "w");

        if (file != nullptr)
        {
            write ("LOG OPENED");
        }
        else
        {
            fprintf (stderr, "BP_DIAGNOSE: failed to open log file: %s\n", path);
        }
    }

    void close ()
    {
        if (file != nullptr)
        {
            write ("LOG CLOSED");
            fclose (file);
            file = nullptr;
        }
    }

    void write (const char* message) const
    {
        ULONGLONG elapsed { GetTickCount64 () - startTick };

        fprintf (stderr, "[%6llu ms] %s\n",
                 static_cast<unsigned long long> (elapsed),
                 message);

        if (file != nullptr)
        {
            fprintf (file, "[%6llu ms] %s\n",
                     static_cast<unsigned long long> (elapsed),
                     message);
            fflush (file);
        }
    }

    // Formatted variant — callers build the string themselves
    void writef (const char* fmt, ...) const
    {
        char buffer[512] {};

        va_list args;
        va_start (args, fmt);
        vsnprintf (buffer, sizeof (buffer), fmt, args);
        va_end (args);

        write (buffer);
    }
};

// ---------------------------------------------------------------------------
// DiagCallbacks — minimal IDebugEventCallbacks implementation.
//
// Logs every callback. Tracks breakpoint hit state.
// Needs IDebugSystemObjects4* and DiagLog* set before events fire.
// ---------------------------------------------------------------------------

class DiagCallbacks : public IDebugEventCallbacks
{
public:
    // Set before first event fires
    IDebugSystemObjects4* systemObjects { nullptr };
    DiagLog*              log           { nullptr };

    // Results populated by callback
    bool  isBreakpointCallbackFired    { false };
    ULONG breakpointCallbackEngineId   { 0 };
    ULONG breakpointCallbackThreadId   { 0 };
    bool  isInitialBreakpointSeen      { false };

    // ---------------------------------------------------------------------------
    // IUnknown
    // ---------------------------------------------------------------------------

    STDMETHOD (QueryInterface) (REFIID iid, PVOID* ppv)
    {
        if (iid == __uuidof (IUnknown) or iid == __uuidof (IDebugEventCallbacks))
        {
            *ppv = static_cast<IDebugEventCallbacks*> (this);
            AddRef ();
            return S_OK;
        }

        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHOD_ (ULONG, AddRef) ()
    {
        return ++refCount;
    }

    STDMETHOD_ (ULONG, Release) ()
    {
        ULONG count { --refCount };
        return count;
    }

    // ---------------------------------------------------------------------------
    // IDebugEventCallbacks
    // ---------------------------------------------------------------------------

    STDMETHOD (GetInterestMask) (PULONG mask)
    {
        *mask = DEBUG_EVENT_BREAKPOINT
              | DEBUG_EVENT_EXCEPTION
              | DEBUG_EVENT_CREATE_PROCESS
              | DEBUG_EVENT_EXIT_PROCESS
              | DEBUG_EVENT_LOAD_MODULE
              | DEBUG_EVENT_UNLOAD_MODULE
              | DEBUG_EVENT_CREATE_THREAD
              | DEBUG_EVENT_EXIT_THREAD
              | DEBUG_EVENT_SYSTEM_ERROR
              | DEBUG_EVENT_SESSION_STATUS
              | DEBUG_EVENT_CHANGE_DEBUGGEE_STATE
              | DEBUG_EVENT_CHANGE_ENGINE_STATE
              | DEBUG_EVENT_CHANGE_SYMBOL_STATE;
        return S_OK;
    }

    // Called when a breakpoint is hit.
    // Mirrors the calls the real adapter makes: QueryInterface → IDebugBreakpoint2,
    // GetId, GetCurrentThreadId.
    STDMETHOD (Breakpoint) (PDEBUG_BREAKPOINT bp)
    {
        ULONG engineId { 0 };

        if (bp != nullptr)
        {
            IDebugBreakpoint2* bp2 { nullptr };
            HRESULT hrQI { bp->QueryInterface (__uuidof (IDebugBreakpoint2),
                                               reinterpret_cast<void**> (&bp2)) };

            if (SUCCEEDED (hrQI) and bp2 != nullptr)
            {
                bp2->GetId (&engineId);
                bp2->Release ();
            }
            else
            {
                // QueryInterface failed — fall back to the original pointer.
                bp->GetId (&engineId);
            }
        }

        ULONG threadId { 0 };

        if (systemObjects != nullptr)
        {
            systemObjects->GetCurrentThreadId (&threadId);
        }

        isBreakpointCallbackFired  = true;
        breakpointCallbackEngineId = engineId;
        breakpointCallbackThreadId = threadId;

        if (log != nullptr)
        {
            log->writef ("CALLBACK: Breakpoint fired — engineId=%lu threadId=%lu",
                         static_cast<unsigned long> (engineId),
                         static_cast<unsigned long> (threadId));
        }

        return DEBUG_STATUS_BREAK;
    }

    // Called for exceptions.
    // INT3 (initial loader breakpoint) gets BREAK so the engine stops and the
    // symbol engine becomes ready.  All other exceptions get NO_CHANGE.
    STDMETHOD (Exception) (PEXCEPTION_RECORD64 exception, ULONG firstChance)
    {
        if (exception != nullptr)
        {
            if (exception->ExceptionCode == kExceptionBreakpoint
                and isInitialBreakpointSeen == false)
            {
                isInitialBreakpointSeen = true;

                if (log != nullptr)
                {
                    log->writef ("CALLBACK: Exception — initial INT3 (code=0x%08lX firstChance=%lu) → BREAK",
                                 static_cast<unsigned long> (exception->ExceptionCode),
                                 static_cast<unsigned long> (firstChance));
                }

                return DEBUG_STATUS_BREAK;
            }

            if (log != nullptr)
            {
                log->writef ("CALLBACK: Exception — code=0x%08lX firstChance=%lu → NO_CHANGE",
                             static_cast<unsigned long> (exception->ExceptionCode),
                             static_cast<unsigned long> (firstChance));
            }
        }

        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD (CreateProcess) (ULONG64 imageFileHandle,
                               ULONG64 handle,
                               ULONG64 baseOffset,
                               ULONG   moduleSize,
                               PCSTR   moduleName,
                               PCSTR   imageName,
                               ULONG   checkSum,
                               ULONG   timeDateStamp,
                               ULONG64 initialThreadHandle,
                               ULONG64 threadDataOffset,
                               ULONG64 startOffset)
    {
        (void) imageFileHandle;
        (void) handle;
        (void) moduleSize;
        (void) checkSum;
        (void) timeDateStamp;
        (void) initialThreadHandle;
        (void) threadDataOffset;
        (void) startOffset;

        if (log != nullptr)
        {
            log->writef ("CALLBACK: CreateProcess — base=0x%llX name=%s image=%s",
                         static_cast<unsigned long long> (baseOffset),
                         moduleName != nullptr ? moduleName : "(null)",
                         imageName  != nullptr ? imageName  : "(null)");
        }

        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD (ExitProcess) (ULONG exitCode)
    {
        if (log != nullptr)
        {
            log->writef ("CALLBACK: ExitProcess — exitCode=%lu",
                         static_cast<unsigned long> (exitCode));
        }

        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD (LoadModule) (ULONG64 imageFileHandle,
                            ULONG64 baseOffset,
                            ULONG   moduleSize,
                            PCSTR   moduleName,
                            PCSTR   imageName,
                            ULONG   checkSum,
                            ULONG   timeDateStamp)
    {
        (void) imageFileHandle;
        (void) moduleSize;
        (void) checkSum;
        (void) timeDateStamp;

        if (log != nullptr)
        {
            log->writef ("CALLBACK: LoadModule — base=0x%llX name=%s image=%s",
                         static_cast<unsigned long long> (baseOffset),
                         moduleName != nullptr ? moduleName : "(null)",
                         imageName  != nullptr ? imageName  : "(null)");
        }

        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD (UnloadModule) (PCSTR imageBaseName, ULONG64 baseOffset)
    {
        if (log != nullptr)
        {
            log->writef ("CALLBACK: UnloadModule — base=0x%llX name=%s",
                         static_cast<unsigned long long> (baseOffset),
                         imageBaseName != nullptr ? imageBaseName : "(null)");
        }

        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD (CreateThread) (ULONG64 handle, ULONG64 dataOffset, ULONG64 startOffset)
    {
        (void) handle;
        (void) dataOffset;
        (void) startOffset;

        if (log != nullptr)
        {
            log->writef ("CALLBACK: CreateThread — start=0x%llX",
                         static_cast<unsigned long long> (startOffset));
        }

        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD (ExitThread) (ULONG exitCode)
    {
        if (log != nullptr)
        {
            log->writef ("CALLBACK: ExitThread — exitCode=%lu",
                         static_cast<unsigned long> (exitCode));
        }

        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD (SystemError) (ULONG error, ULONG level)
    {
        if (log != nullptr)
        {
            log->writef ("CALLBACK: SystemError — error=0x%08lX level=%lu",
                         static_cast<unsigned long> (error),
                         static_cast<unsigned long> (level));
        }

        return DEBUG_STATUS_NO_CHANGE;
    }

    STDMETHOD (SessionStatus) (ULONG status)
    {
        if (log != nullptr)
        {
            log->writef ("CALLBACK: SessionStatus — status=%lu", static_cast<unsigned long> (status));
        }

        return S_OK;
    }

    STDMETHOD (ChangeDebuggeeState) (ULONG flags, ULONG64 argument)
    {
        if (log != nullptr)
        {
            log->writef ("CALLBACK: ChangeDebuggeeState — flags=0x%08lX arg=0x%llX",
                         static_cast<unsigned long> (flags),
                         static_cast<unsigned long long> (argument));
        }

        return S_OK;
    }

    STDMETHOD (ChangeEngineState) (ULONG flags, ULONG64 argument)
    {
        if (log != nullptr)
        {
            log->writef ("CALLBACK: ChangeEngineState — flags=0x%08lX arg=0x%llX",
                         static_cast<unsigned long> (flags),
                         static_cast<unsigned long long> (argument));
        }

        return S_OK;
    }

    STDMETHOD (ChangeSymbolState) (ULONG flags, ULONG64 argument)
    {
        if (log != nullptr)
        {
            log->writef ("CALLBACK: ChangeSymbolState — flags=0x%08lX arg=0x%llX",
                         static_cast<unsigned long> (flags),
                         static_cast<unsigned long long> (argument));
        }

        return S_OK;
    }

private:
    ULONG refCount { 1 };
};

// ---------------------------------------------------------------------------
// buildTargetPath — derive bp_test_target.exe path from argv[0]
//
// Replaces the filename component of argv[0] with bp_test_target.exe.
// ---------------------------------------------------------------------------

static std::string buildTargetPath (const char* argv0)
{
    std::string path { argv0 };

    // Normalize slashes for rfind
    for (char& c : path)
    {
        if (c == '/')
        {
            c = '\\';
        }
    }

    const size_t lastSep { path.rfind ('\\') };
    std::string  result;

    if (lastSep != std::string::npos)
    {
        result = path.substr (0, lastSep + 1) + "bp_test_target.exe";
    }
    else
    {
        result = "bp_test_target.exe";
    }

    return result;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main (int argc, char* argv[])
{
    (void) argc;

    // -----------------------------------------------------------------------
    // Setup — log file
    // -----------------------------------------------------------------------

    DiagLog log;
    log.open (kLogFileName);

    log.write ("SECTION: bp_diagnose starting");
    log.writef ("SECTION: target file = %s  target line = %lu",
                kTargetFile,
                static_cast<unsigned long> (kTargetLine));

    // -----------------------------------------------------------------------
    // Step 1 — derive target executable path
    // -----------------------------------------------------------------------

    std::string targetPath { buildTargetPath (argv[0]) };
    log.writef ("SECTION: target exe = %s", targetPath.c_str ());

    // -----------------------------------------------------------------------
    // Step 2 — initialize DbgEngSession
    // -----------------------------------------------------------------------

    log.write ("SECTION: initializing DbgEngSession");

    DbgEngSession session;
    bool isSessionOk { session.initialize () };

    log.writef ("SECTION: session.initialize() → %s",
                isSessionOk ? "OK" : "FAILED");

    // Track per-test pass/fail
    bool isPassA { false };
    bool isPassB { false };
    bool isPassC { false };
    bool isPassD { false };
    bool isPassE { false };
    bool isPassF { false };

    if (isSessionOk == true)
    {
        // -------------------------------------------------------------------
        // Step 3 — register callbacks
        // -------------------------------------------------------------------

        log.write ("SECTION: registering callbacks");

        DiagCallbacks callbacks;
        callbacks.log = &log;

        HRESULT hrCb { session.client ()->SetEventCallbacks (&callbacks) };
        log.writef ("SECTION: SetEventCallbacks → hr=0x%08lX",
                    static_cast<unsigned long> (hrCb));

        if (SUCCEEDED (hrCb))
        {
            // systemObjects pointer injected after session init, before events fire
            callbacks.systemObjects = session.systemObjects ();

            // ---------------------------------------------------------------
            // Step 4 — launch target
            // ---------------------------------------------------------------

            log.writef ("SECTION: launching %s", targetPath.c_str ());

            DEBUG_CREATE_PROCESS_OPTIONS options {};
            options.CreateFlags   = DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE;
            options.EngCreateFlags = 0;
            options.VerifierFlags  = 0;
            options.Reserved       = 0;

            HRESULT hrCreate { session.client ()->CreateProcess2 (
                0,
                const_cast<PSTR> (targetPath.c_str ()),
                &options,
                sizeof (options),
                nullptr,
                nullptr) };

            log.writef ("SECTION: CreateProcess2 → hr=0x%08lX",
                        static_cast<unsigned long> (hrCreate));

            if (SUCCEEDED (hrCreate))
            {
                session.setHasTarget ();

                // -----------------------------------------------------------
                // Step 5 — WaitForEvent: initial loader breakpoint
                // -----------------------------------------------------------

                log.write ("SECTION: WaitForEvent (initial loader breakpoint)");

                HRESULT hrWait { session.control ()->WaitForEvent (0, kLaunchTimeout) };

                ULONG execStatus { 0 };
                session.control ()->GetExecutionStatus (&execStatus);

                log.writef ("SECTION: initial WaitForEvent → hr=0x%08lX execStatus=%lu",
                            static_cast<unsigned long> (hrWait),
                            static_cast<unsigned long> (execStatus));

                if (SUCCEEDED (hrWait))
                {
                    // -------------------------------------------------------
                    // Step 6 — set source path and reload symbols
                    // -------------------------------------------------------

                    log.writef ("SECTION: setting source path: %s", kSourceDir);

                    HRESULT hrSrc { session.symbols ()->AppendSourcePath (kSourceDir) };
                    log.writef ("SECTION: AppendSourcePath → hr=0x%08lX",
                                static_cast<unsigned long> (hrSrc));

                    HRESULT hrSym { session.symbols ()->AppendSymbolPath (kSymbolDir) };
                    log.writef ("SECTION: AppendSymbolPath → hr=0x%08lX",
                                static_cast<unsigned long> (hrSym));

                    // Log current symbol options
                    // SYMOPT_DEFERRED_LOADS = 0x00000004 (from dbghelp.h)
                    static constexpr ULONG kSymoptDeferredLoads { 0x00000004 };
                    ULONG symOpts { 0 };
                    session.symbols ()->GetSymbolOptions (&symOpts);
                    log.writef ("SECTION: SymbolOptions = 0x%08lX  DEFERRED_LOADS=%s",
                                static_cast<unsigned long> (symOpts),
                                (symOpts & kSymoptDeferredLoads) != 0 ? "ON" : "OFF");

                    // Force-reload all module symbols (the /f flag overrides deferred loading)
                    HRESULT hrReload { session.symbols ()->Reload ("/f") };
                    log.writef ("SECTION: Reload(\"/f\") → hr=0x%08lX",
                                static_cast<unsigned long> (hrReload));

                    // -------------------------------------------------------
                    // TEST A — GetOffsetByLine (full path + basename)
                    // -------------------------------------------------------

                    log.write ("SECTION: TEST A — GetOffsetByLine");

                    // A1: full path (what nvim-dap sends)
                    ULONG64 resolvedOffset { 0 };
                    HRESULT hrOBL { session.symbols ()->GetOffsetByLine (
                        kTargetLine,
                        kTargetFile,
                        &resolvedOffset) };

                    log.writef ("TEST A1: GetOffsetByLine (full) %s:%lu → offset=0x%016llX (hr=0x%08lX)",
                                kTargetFile,
                                static_cast<unsigned long> (kTargetLine),
                                static_cast<unsigned long long> (resolvedOffset),
                                static_cast<unsigned long> (hrOBL));

                    // A2: basename fallback (what tryResolve uses as fallback)
                    ULONG64 basenameOffset { 0 };
                    HRESULT hrBase { session.symbols ()->GetOffsetByLine (
                        kTargetLine,
                        kTargetBasename,
                        &basenameOffset) };

                    log.writef ("TEST A2: GetOffsetByLine (basename) %s:%lu → offset=0x%016llX (hr=0x%08lX)",
                                kTargetBasename,
                                static_cast<unsigned long> (kTargetLine),
                                static_cast<unsigned long long> (basenameOffset),
                                static_cast<unsigned long> (hrBase));

                    // Use whichever succeeded (prefer full path)
                    if (SUCCEEDED (hrOBL) and resolvedOffset != 0)
                    {
                        isPassA = true;
                    }
                    else if (SUCCEEDED (hrBase) and basenameOffset != 0)
                    {
                        resolvedOffset = basenameOffset;
                        isPassA = true;
                        log.write ("TEST A: full path failed, basename succeeded — using basename offset");
                    }

                    log.writef ("RESULT: TEST A: %s", isPassA ? "PASS" : "FAIL");

                    if (isPassA == true)
                    {
                        // ---------------------------------------------------
                        // TEST B — AddBreakpoint2 + SetOffset + AddFlags
                        // ---------------------------------------------------

                        log.write ("SECTION: TEST B — AddBreakpoint2 + SetOffset");

                        IDebugBreakpoint2* bp { nullptr };
                        HRESULT hrAdd { session.control ()->AddBreakpoint2 (
                            DEBUG_BREAKPOINT_CODE,
                            DEBUG_ANY_ID,
                            &bp) };

                        ULONG engineId { 0 };
                        HRESULT hrOffset { E_FAIL };
                        HRESULT hrFlags  { E_FAIL };

                        if (SUCCEEDED (hrAdd) and bp != nullptr)
                        {
                            hrOffset = bp->SetOffset (resolvedOffset);
                            hrFlags  = bp->AddFlags (DEBUG_BREAKPOINT_ENABLED);
                            bp->GetId (&engineId);
                        }

                        log.writef ("TEST B: AddBreakpoint2 → engineId=%lu (hr=0x%08lX)  SetOffset → hr=0x%08lX  AddFlags → hr=0x%08lX",
                                    static_cast<unsigned long> (engineId),
                                    static_cast<unsigned long> (hrAdd),
                                    static_cast<unsigned long> (hrOffset),
                                    static_cast<unsigned long> (hrFlags));

                        isPassB = SUCCEEDED (hrAdd) and bp != nullptr
                                  and SUCCEEDED (hrOffset) and SUCCEEDED (hrFlags);
                        log.writef ("RESULT: TEST B: %s", isPassB ? "PASS" : "FAIL");

                        if (isPassB == true)
                        {
                            // -----------------------------------------------
                            // TEST C — verify breakpoint state
                            // -----------------------------------------------

                            log.write ("SECTION: TEST C — verify breakpoint state");

                            ULONG  bpFlags   { 0 };
                            ULONG64 bpOffset { 0 };

                            HRESULT hrGetFlags  { bp->GetFlags (&bpFlags) };
                            HRESULT hrGetOffset { bp->GetOffset (&bpOffset) };

                            IDebugBreakpoint2* verifyBp { nullptr };
                            HRESULT hrGetById { session.control ()->GetBreakpointById2 (
                                engineId, &verifyBp) };

                            bool isEnabledFlagSet { (bpFlags & DEBUG_BREAKPOINT_ENABLED) != 0 };
                            bool isOffsetMatch    { bpOffset == resolvedOffset };
                            bool isFoundById      { SUCCEEDED (hrGetById) and verifyBp != nullptr };

                            log.writef ("TEST C: GetFlags → flags=0x%08lX enabled=%s (hr=0x%08lX)",
                                        static_cast<unsigned long> (bpFlags),
                                        isEnabledFlagSet ? "YES" : "NO",
                                        static_cast<unsigned long> (hrGetFlags));

                            log.writef ("TEST C: GetOffset → offset=0x%016llX match=%s (hr=0x%08lX)",
                                        static_cast<unsigned long long> (bpOffset),
                                        isOffsetMatch ? "YES" : "NO",
                                        static_cast<unsigned long> (hrGetOffset));

                            log.writef ("TEST C: GetBreakpointById2 engineId=%lu → found=%s (hr=0x%08lX)",
                                        static_cast<unsigned long> (engineId),
                                        isFoundById ? "YES" : "NO",
                                        static_cast<unsigned long> (hrGetById));

                            isPassC = isEnabledFlagSet and isOffsetMatch and isFoundById;
                            log.writef ("RESULT: TEST C: %s", isPassC ? "PASS" : "FAIL");

                            // -----------------------------------------------
                            // TEST D — resume and wait for hit
                            // -----------------------------------------------

                            log.write ("SECTION: TEST D — SetExecutionStatus(GO) + WaitForEvent");

                            HRESULT hrGo { session.control ()->SetExecutionStatus (
                                DEBUG_STATUS_GO) };
                            log.writef ("TEST D: SetExecutionStatus(GO) → hr=0x%08lX",
                                        static_cast<unsigned long> (hrGo));

                            HRESULT hrWaitResume { session.control ()->WaitForEvent (
                                0, kResumeTimeout) };

                            ULONG execStatusAfter { 0 };
                            session.control ()->GetExecutionStatus (&execStatusAfter);

                            log.writef ("TEST D: WaitForEvent → hr=0x%08lX execStatus=%lu",
                                        static_cast<unsigned long> (hrWaitResume),
                                        static_cast<unsigned long> (execStatusAfter));

                            // S_OK = event processed (breakpoint fired or other event)
                            // S_FALSE = timeout (nothing fired)
                            isPassD = hrWaitResume == S_OK;
                            log.writef ("RESULT: TEST D: %s  (S_OK=%s S_FALSE=%s)",
                                        isPassD ? "PASS" : "FAIL",
                                        hrWaitResume == S_OK    ? "YES" : "NO",
                                        hrWaitResume == S_FALSE ? "YES" : "NO");

                            // -----------------------------------------------
                            // TEST E — check callback state
                            // -----------------------------------------------

                            log.write ("SECTION: TEST E — callback state");

                            log.writef ("TEST E: isBreakpointCallbackFired=%s  callbackEngineId=%lu  callbackThreadId=%lu",
                                        callbacks.isBreakpointCallbackFired ? "YES" : "NO",
                                        static_cast<unsigned long> (callbacks.breakpointCallbackEngineId),
                                        static_cast<unsigned long> (callbacks.breakpointCallbackThreadId));

                            bool isCallbackEngineIdMatch { callbacks.breakpointCallbackEngineId == engineId };

                            log.writef ("TEST E: callbackEngineId matches expected=%s  (expected=%lu got=%lu)",
                                        isCallbackEngineIdMatch ? "YES" : "NO",
                                        static_cast<unsigned long> (engineId),
                                        static_cast<unsigned long> (callbacks.breakpointCallbackEngineId));

                            isPassE = callbacks.isBreakpointCallbackFired == true
                                      and isCallbackEngineIdMatch == true;
                            log.writef ("RESULT: TEST E: %s", isPassE ? "PASS" : "FAIL");

                            // -----------------------------------------------
                            // TEST F — thread state when breakpoint fired
                            // -----------------------------------------------

                            log.write ("SECTION: TEST F — thread state at breakpoint");

                            if (callbacks.isBreakpointCallbackFired == true)
                            {
                                ULONG currentThreadId { 0 };
                                HRESULT hrThread { session.systemObjects ()->GetCurrentThreadId (
                                    &currentThreadId) };

                                ULONG finalExecStatus { 0 };
                                session.control ()->GetExecutionStatus (&finalExecStatus);

                                bool isBreakState { finalExecStatus == DEBUG_STATUS_BREAK };

                                log.writef ("TEST F: GetCurrentThreadId → threadId=%lu (hr=0x%08lX)",
                                            static_cast<unsigned long> (currentThreadId),
                                            static_cast<unsigned long> (hrThread));

                                log.writef ("TEST F: GetExecutionStatus → status=%lu isBreakState=%s",
                                            static_cast<unsigned long> (finalExecStatus),
                                            isBreakState ? "YES" : "NO");

                                isPassF = SUCCEEDED (hrThread) and isBreakState;
                                log.writef ("RESULT: TEST F: %s", isPassF ? "PASS" : "FAIL");
                            }
                            else
                            {
                                log.write ("TEST F: SKIPPED — breakpoint callback did not fire");
                                log.write ("RESULT: TEST F: SKIP");
                            }
                        }
                    }
                }
            }

            // Unregister callbacks before the session shuts down
            session.client ()->SetEventCallbacks (nullptr);
        }

        // -------------------------------------------------------------------
        // Cleanup
        // -------------------------------------------------------------------

        log.write ("SECTION: cleanup — DetachProcesses");

        HRESULT hrDetach { session.client ()->DetachProcesses () };
        log.writef ("SECTION: DetachProcesses → hr=0x%08lX",
                    static_cast<unsigned long> (hrDetach));

        session.clearTarget ();
        session.shutdown ();
        log.write ("SECTION: session shutdown complete");
    }

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------

    log.write ("SECTION: === SUMMARY ===");
    log.writef ("RESULT: TEST A (GetOffsetByLine):     %s", isPassA ? "PASS" : "FAIL");
    log.writef ("RESULT: TEST B (AddBreakpoint2):      %s", isPassB ? "PASS" : "FAIL");
    log.writef ("RESULT: TEST C (verify state):        %s", isPassC ? "PASS" : "FAIL");
    log.writef ("RESULT: TEST D (resume + WaitFor):    %s", isPassD ? "PASS" : "FAIL");
    log.writef ("RESULT: TEST E (callback fired):      %s", isPassE ? "PASS" : "FAIL");
    log.writef ("RESULT: TEST F (thread state):        %s", isPassF ? "PASS" : "SKIP/FAIL");

    bool isAllPass { isPassA and isPassB and isPassC and isPassD and isPassE and isPassF };

    log.writef ("RESULT: OVERALL: %s", isAllPass ? "PASS — breakpoint lifecycle works" : "FAIL — see individual results above");

    log.close ();

    return isAllPass ? 0 : 1;
}
