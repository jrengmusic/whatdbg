/** @file Session.cpp
 *  @brief Windows debug session implementation using Microsoft's dbgeng COM API.
 *
 *  Platform counterpart to Session_mac.cpp (macOS liblldb). Implements the Session
 *  interface declared in Session.h using IDebugClient5, IDebugControl4,
 *  IDebugSymbols3, IDebugDataSpaces4, and IDebugSystemObjects.
 *
 *  Threading model: single-threaded main loop. COM callbacks (Callbacks.cpp) fire
 *  on the dbgeng thread and store deferred flags on State. Main thread polls and
 *  processes those flags.
 *
 *  Lifecycle: initialize (Loader → COM) → (launch | attach) → pollEvents loop → shutdown.
 */

#include <JuceHeader.h>
#include "Session.h"
#include "State.h"
#include "../dap/Types.h"
#include "../Log.h"

#if JUCE_WINDOWS
#include <dbghelp.h>

namespace debug
{

using dap::DynObj;

// ---------------------------------------------------------------------------
// Session::~Session
// ---------------------------------------------------------------------------

/** Delegates to shutdown (EndMode::passive) — releases all COM interfaces. */
Session::~Session ()
{
    shutdown (EndMode::passive);
}

// ---------------------------------------------------------------------------
// Session::initialize
// ---------------------------------------------------------------------------

/** CoInitializes COM, loads dbgeng via Loader, creates IDebugClient5, and
 *  QIs all required interfaces. Calls shutdown on any failure. */
bool Session::initialize (const juce::File& sidecarDir) noexcept
{
    const HRESULT comResult { CoInitializeEx (nullptr, COINIT_MULTITHREADED) };
    const bool isComOk { comResult == S_OK or comResult == RPC_E_CHANGED_MODE };

    if (isComOk)
    {
        isComOwned = (comResult == S_OK);
        const bool isLoaderOk { loader.load (sidecarDir) };

        if (isLoaderOk)
        {
            IDebugClient5* rawClient { nullptr };
            const HRESULT createResult { loader.createDebugClient (&rawClient) };

            if (SUCCEEDED (createResult) and rawClient != nullptr)
            {
                client.Attach (rawClient);

                const HRESULT qiControlResult { client->QueryInterface (
                    __uuidof (IDebugControl4),
                    reinterpret_cast<PVOID*> (control.GetAddressOf ())) };

                if (SUCCEEDED (qiControlResult) and control != nullptr)
                {
                    const HRESULT cbResult { client->SetOutputCallbacks (
                        reinterpret_cast<IDebugOutputCallbacks*> (&outputCallbacks)) };
                    juce::ignoreUnused (cbResult);

                    client->SetOutputMask (
                        DEBUG_OUTPUT_NORMAL
                        | DEBUG_OUTPUT_WARNING
                        | DEBUG_OUTPUT_ERROR
                        | DEBUG_OUTPUT_DEBUGGEE);

                    const HRESULT qiSymbolsResult { client->QueryInterface (
                        __uuidof (IDebugSymbols3),
                        reinterpret_cast<PVOID*> (symbols.GetAddressOf ())) };
                    juce::ignoreUnused (qiSymbolsResult);

                    const HRESULT qiDataResult { client->QueryInterface (
                        __uuidof (IDebugDataSpaces4),
                        reinterpret_cast<PVOID*> (dataSpaces.GetAddressOf ())) };
                    juce::ignoreUnused (qiDataResult);

                    const HRESULT qiSysObjResult { client->QueryInterface (
                        __uuidof (IDebugSystemObjects),
                        reinterpret_cast<PVOID*> (systemObjects.GetAddressOf ())) };
                    juce::ignoreUnused (qiSysObjResult);

                    if (symbols != nullptr)
                    {
                        symbols->AddSymbolOptions (SYMOPT_LOAD_LINES);
                        control->SetCodeLevel (DEBUG_LEVEL_SOURCE);
                    }

                    control->AddEngineOptions (DEBUG_ENGOPT_INITIAL_BREAK);
                    client->SetEventCallbacks (&eventCallbacks);
                }
            }
        }
    }

    const bool isInitialized { client != nullptr
                                and control != nullptr
                                and symbols != nullptr
                                and dataSpaces != nullptr
                                and systemObjects != nullptr };

    if (not isInitialized)
    {
        shutdown (EndMode::passive);
        logWrite ("WHATDBG: initialization failed\n");
    }

    return isInitialized;
}

// ---------------------------------------------------------------------------
// Session::launch
// ---------------------------------------------------------------------------

/** Normalizes path separators, quotes paths with spaces, and calls
 *  IDebugClient5::CreateProcess2 with DEBUG_ONLY_THIS_PROCESS. */
bool Session::launch (const juce::String& program) noexcept
{
    jassert (client != nullptr);

    juce::String normalized { program.replace ("/", "\\") };

    if (normalized.containsChar (' ') and not normalized.startsWithChar ('"'))
        normalized = "\"" + normalized + "\"";

    logWrite ("WHATDBG: CreateProcess2 commandLine: %s\n", normalized.toRawUTF8 ());

    std::string commandLineBuffer { normalized.toStdString () };

    DEBUG_CREATE_PROCESS_OPTIONS options {};
    options.CreateFlags    = DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE;
    options.EngCreateFlags = 0;
    options.VerifierFlags  = 0;
    options.Reserved       = 0;

    const HRESULT result { client->CreateProcess2 (
        0,
        commandLineBuffer.data (),
        &options,
        sizeof (options),
        nullptr,
        nullptr) };

    const bool launched { SUCCEEDED (result) };

    if (launched)
    {
        logWrite ("WHATDBG: launched process: %s\n", program.toRawUTF8 ());
    }
    else
    {
        logWrite ("WHATDBG: CreateProcess2 failed, hr=0x%08lX\n", static_cast<unsigned long> (result));
    }

    return launched;
}

// ---------------------------------------------------------------------------
// Session::attach
// ---------------------------------------------------------------------------

/** Attaches to a running process via IDebugClient5::AttachProcess with no
 *  special flags (non-invasive attach not requested). */
bool Session::attach (std::uint32_t processId) noexcept
{
    jassert (client != nullptr);

    const HRESULT attachResult { client->AttachProcess (0, static_cast<ULONG> (processId), 0) };
    const bool attached { SUCCEEDED (attachResult) };

    if (attached)
    {
        logWrite ("WHATDBG: attached to process %lu\n", static_cast<unsigned long> (processId));
    }
    else
    {
        logWrite ("WHATDBG: AttachProcess failed, hr=0x%08lX\n", static_cast<unsigned long> (attachResult));
    }

    return attached;
}

// ---------------------------------------------------------------------------
// Session::resume
// ---------------------------------------------------------------------------

/** Sets execution status to DEBUG_STATUS_GO — resumes all threads. */
void Session::resume () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_GO);
    }
}

// ---------------------------------------------------------------------------
// Session::pollEvents
// ---------------------------------------------------------------------------

/** Calls IDebugControl4::WaitForEvent with the given timeout. S_OK → event
 *  consumed; S_FALSE → timeout with no event; other HRESULT → failure result. */
juce::Result Session::pollEvents (std::uint32_t timeoutMs, bool& outHadEvent) noexcept
{
    outHadEvent = false;
    juce::Result result { juce::Result::ok () };

    if (control != nullptr)
    {
        const HRESULT hr { control->WaitForEvent (0, static_cast<ULONG> (timeoutMs)) };

        if (hr == S_OK)
        {
            outHadEvent = true;
        }
        else if (hr == S_FALSE)
        {
            outHadEvent = false;
        }
        else
        {
            result = juce::Result::fail ("pollEvents: HRESULT 0x"
                + juce::String::toHexString (static_cast<int> (hr)));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::shutdown
// ---------------------------------------------------------------------------

/** Ends the debug session with the appropriate DEBUG_END_* flag, resets all
 *  COM interface ComPtrs, and CoUninitializes only if this instance owns COM. */
void Session::shutdown (EndMode mode) noexcept
{
    if (client != nullptr)
    {
        ULONG endFlag { DEBUG_END_PASSIVE };

        if (mode == EndMode::terminate)
            endFlag = DEBUG_END_ACTIVE_TERMINATE;
        else if (mode == EndMode::detach)
            endFlag = DEBUG_END_ACTIVE_DETACH;

        client->EndSession (endFlag);
    }

    resetSymbolGroupCache ();
    systemObjects.Reset ();
    dataSpaces.Reset ();
    symbols.Reset ();
    control.Reset ();
    client.Reset ();

    if (isComOwned)
    {
        CoUninitialize ();
        isComOwned = false;
    }
}

// ---------------------------------------------------------------------------
// Session::appendSymbolPath / appendSourcePath
// ---------------------------------------------------------------------------

/** Appends a directory to dbgeng's symbol search path via IDebugSymbols3::AppendSymbolPath. */
void Session::appendSymbolPath (const juce::String& path) noexcept
{
    if (symbols != nullptr)
    {
        symbols->AppendSymbolPath (path.toRawUTF8 ());
        logWrite ("WHATDBG: appended symbol path: %s\n", path.toRawUTF8 ());
    }
}

/** Appends a directory to dbgeng's source search path via IDebugSymbols3::AppendSourcePath. */
void Session::appendSourcePath (const juce::String& path) noexcept
{
    if (symbols != nullptr)
    {
        symbols->AppendSourcePath (path.toRawUTF8 ());
        logWrite ("WHATDBG: appended source path: %s\n", path.toRawUTF8 ());
    }
}

// ---------------------------------------------------------------------------
// Session::stepOver / stepInto / stepOut / interrupt
// ---------------------------------------------------------------------------

/** Sets execution status to DEBUG_STATUS_STEP_OVER. */
void Session::stepOver () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_STEP_OVER);
    }
}

/** Sets execution status to DEBUG_STATUS_STEP_INTO. */
void Session::stepInto () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_STEP_INTO);
    }
}

/** Executes the "gu" (go up) dbgeng command — no native step-out status flag exists. */
void Session::stepOut () noexcept
{
    if (control != nullptr)
    {
        control->Execute (DEBUG_OUTCTL_IGNORE, "gu", DEBUG_EXECUTE_NOT_LOGGED);
    }
}

/** Opens the target process and calls DebugBreakProcess to inject a
 *  breakpoint interrupt; closes the handle immediately after. */
void Session::interrupt (std::uint32_t processId) noexcept
{
    if (processId != 0)
    {
        const HANDLE handle { OpenProcess (PROCESS_ALL_ACCESS, FALSE, static_cast<DWORD> (processId)) };

        if (handle != nullptr)
        {
            const BOOL result { DebugBreakProcess (handle) };
            CloseHandle (handle);

            if (result)
            {
                logWrite ("WHATDBG: DebugBreakProcess success, PID=%lu\n",
                          static_cast<unsigned long> (processId));
            }
            else
            {
                logWrite ("WHATDBG: DebugBreakProcess failed, PID=%lu error=%lu\n",
                          static_cast<unsigned long> (processId), GetLastError ());
            }
        }
        else
        {
            logWrite ("WHATDBG: OpenProcess failed, PID=%lu error=%lu\n",
                      static_cast<unsigned long> (processId), GetLastError ());
        }
    }
}

// ---------------------------------------------------------------------------
// Session::loadModuleSymbols / forceReloadAllSymbols
// ---------------------------------------------------------------------------

/** Issues ".reload /f <basename>" to force-load PDB for a single module image. */
juce::Result Session::loadModuleSymbols (const juce::String& imageName) noexcept
{
    juce::Result result { juce::Result::fail ("loadModuleSymbols: control is null") };

    if (control != nullptr)
    {
        const juce::String basename { juce::File (imageName).getFileName () };
        const juce::String command { ".reload /f " + basename.quoted () };
        const HRESULT hr { control->Execute (DEBUG_OUTCTL_IGNORE,
                                             command.toRawUTF8 (),
                                             DEBUG_EXECUTE_NOT_LOGGED) };
        logWrite ("WHATDBG: .reload /f %s hr=0x%08lX\n",
                  basename.toRawUTF8 (),
                  static_cast<unsigned long> (hr));
        result = hr == S_OK ? juce::Result::ok ()
                            : juce::Result::fail ("loadModuleSymbols: HRESULT 0x"
                                + juce::String::toHexString (static_cast<int> (hr)));
    }

    return result;
}

/** Issues ".reload /f" with no argument to force-reload PDBs for all loaded modules. */
juce::Result Session::forceReloadAllSymbols () noexcept
{
    juce::Result result { juce::Result::fail ("forceReloadAllSymbols: control is null") };

    if (control != nullptr)
    {
        const HRESULT hr { control->Execute (DEBUG_OUTCTL_IGNORE,
                                             ".reload /f",
                                             DEBUG_EXECUTE_NOT_LOGGED) };
        logWrite ("WHATDBG: .reload /f (all) hr=0x%08lX\n", static_cast<unsigned long> (hr));
        result = hr == S_OK ? juce::Result::ok ()
                            : juce::Result::fail ("forceReloadAllSymbols: HRESULT 0x"
                                + juce::String::toHexString (static_cast<int> (hr)));
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::getOffsetByLine / getLineByOffset
// ---------------------------------------------------------------------------

/** Resolves a source file/line to a code offset. Returns engineBusy when
 *  E_UNEXPECTED is returned (symbols not yet loaded), notFound otherwise. */
ResolveStatus Session::getOffsetByLine (const juce::String& filePath,
                                        std::uint32_t       line,
                                        std::uint64_t*      outOffset) noexcept
{
    ResolveStatus status { ResolveStatus::notFound };

    if (symbols != nullptr)
    {
        const HRESULT hr { symbols->GetOffsetByLine (static_cast<ULONG> (line),
                                                     filePath.toRawUTF8 (),
                                                     outOffset) };
        if (hr == S_OK)
        {
            status = ResolveStatus::resolved;
        }
        else if (hr == E_UNEXPECTED)
        {
            status = ResolveStatus::engineBusy;
        }
        else
        {
            status = ResolveStatus::notFound;
        }
    }

    return status;
}

/** Maps a code offset back to source file path and line number via IDebugSymbols3::GetLineByOffset. */
juce::Result Session::getLineByOffset (std::uint64_t offset, juce::String& outFilePath, std::uint32_t* outLine) noexcept
{
    juce::Result result { juce::Result::fail ("getLineByOffset: symbols is null") };

    if (symbols != nullptr)
    {
        char pathBuffer[MAX_PATH] {};
        ULONG pathSize { 0 };
        ULONG lineOut { 0 };

        const HRESULT hr { symbols->GetLineByOffset (offset, &lineOut, pathBuffer, MAX_PATH, &pathSize, nullptr) };

        if (hr == S_OK)
        {
            outFilePath = juce::String (pathBuffer);
            *outLine    = static_cast<std::uint32_t> (lineOut);
            result      = juce::Result::ok ();
        }
        else
        {
            result = juce::Result::fail ("getLineByOffset: HRESULT 0x"
                + juce::String::toHexString (static_cast<int> (hr)));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::addBreakpoint / removeBreakpoint
// ---------------------------------------------------------------------------

/** Creates a code breakpoint at an absolute offset via AddBreakpoint2 and
 *  enables it; writes the engine-assigned breakpoint ID to outEngineId. */
juce::Result Session::addBreakpoint (std::uint64_t offset, std::uint32_t* outEngineId) noexcept
{
    juce::Result result { juce::Result::fail ("addBreakpoint: control is null") };

    if (control != nullptr)
    {
        IDebugBreakpoint2* bp { nullptr };
        const HRESULT hr { control->AddBreakpoint2 (DEBUG_BREAKPOINT_CODE, DEBUG_ANY_ID, &bp) };

        if (hr == S_OK and bp != nullptr)
        {
            bp->SetOffset (offset);
            bp->AddFlags (DEBUG_BREAKPOINT_ENABLED);
            ULONG engineId { 0 };
            bp->GetId (&engineId);
            *outEngineId = static_cast<std::uint32_t> (engineId);
            result = juce::Result::ok ();
        }
        else
        {
            result = juce::Result::fail ("addBreakpoint: HRESULT 0x"
                + juce::String::toHexString (static_cast<int> (hr)));
        }
    }

    return result;
}

/** Resolves file/line to offset via getOffsetByLine then delegates to addBreakpoint.
 *  Returns failure if the offset cannot be resolved (symbols not yet loaded). */
juce::Result Session::addBreakpointByLocation (const juce::String& filePath,
                                               std::uint32_t       line,
                                               std::uint32_t*      outEngineId,
                                               std::uint32_t*      outResolvedLine) noexcept
{
    jassert (outEngineId != nullptr);
    jassert (outResolvedLine != nullptr);

    std::uint64_t offset { 0 };
    juce::Result result { juce::Result::fail ("getOffsetByLine failed") };

    const ResolveStatus status { getOffsetByLine (filePath, line, &offset) };

    if (status == ResolveStatus::resolved)
    {
        const juce::Result addResult { addBreakpoint (offset, outEngineId) };

        if (addResult.wasOk ())
        {
            *outResolvedLine = line;
            result = juce::Result::ok ();
        }
        else
        {
            result = addResult;
        }
    }

    return result;
}

/** Looks up the breakpoint by engine ID via GetBreakpointById2 and removes it
 *  via RemoveBreakpoint2. */
juce::Result Session::removeBreakpoint (std::uint32_t engineId) noexcept
{
    juce::Result result { juce::Result::fail ("removeBreakpoint: control is null") };

    if (control != nullptr)
    {
        IDebugBreakpoint2* bp { nullptr };
        const HRESULT hr { control->GetBreakpointById2 (static_cast<ULONG> (engineId), &bp) };

        if (hr == S_OK and bp != nullptr)
        {
            const HRESULT hrRemove { control->RemoveBreakpoint2 (bp) };
            result = hrRemove == S_OK ? juce::Result::ok ()
                                      : juce::Result::fail ("removeBreakpoint: HRESULT 0x"
                                          + juce::String::toHexString (static_cast<int> (hrRemove)));
        }
        else
        {
            result = juce::Result::fail ("removeBreakpoint: GetBreakpointById2 HRESULT 0x"
                + juce::String::toHexString (static_cast<int> (hr)));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::resetSymbolGroupCache / getOrCreateSymbolGroup
// ---------------------------------------------------------------------------

/** Releases the cached IDebugSymbolGroup2 and resets cachedFrameIndex to -1. */
void Session::resetSymbolGroupCache () noexcept
{
    cachedSymbolGroup.Reset ();
    cachedFrameIndex = -1;
}

/** Returns the cached symbol group if frameIndex matches; otherwise sets scope
 *  to frameIndex via SetScopeFrameByIndex and queries a new group via GetScopeSymbolGroup2. */
IDebugSymbolGroup2* Session::getOrCreateSymbolGroup (int frameIndex) noexcept
{
    IDebugSymbolGroup2* result { nullptr };

    if (symbols != nullptr)
    {
        if (cachedSymbolGroup != nullptr and cachedFrameIndex == frameIndex)
        {
            result = cachedSymbolGroup.Get ();
        }
        else
        {
            cachedSymbolGroup.Reset ();

            symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));

            IDebugSymbolGroup2* group { nullptr };
            const HRESULT hr { symbols->GetScopeSymbolGroup2 (
                DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

            if (SUCCEEDED (hr) and group != nullptr)
            {
                cachedSymbolGroup.Attach (group);
                cachedFrameIndex = frameIndex;
                result = cachedSymbolGroup.Get ();
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::getThreads
// ---------------------------------------------------------------------------

/** Enumerates all threads via GetThreadIdsByIndex, resolves each system thread
 *  ID, and queries the Win32 thread description via GetThreadDescription. */
juce::Array<juce::var> Session::getThreads () noexcept
{
    juce::Array<juce::var> threads;

    if (systemObjects != nullptr)
    {
        ULONG threadCount { 0 };
        systemObjects->GetNumberThreads (&threadCount);

        if (threadCount > 0)
        {
            std::vector<ULONG> engineIds (threadCount);
            std::vector<ULONG> systemIds (threadCount);
            systemObjects->GetThreadIdsByIndex (0, threadCount,
                                                engineIds.data (), systemIds.data ());

            ULONG savedEngineId { 0 };
            systemObjects->GetCurrentThreadId (&savedEngineId);

            for (ULONG i { 0 }; i < threadCount; ++i)
            {
                systemObjects->SetCurrentThreadId (engineIds.at (i));

                juce::String name { "Thread " + juce::String (systemIds.at (i)) };

                ULONG64 handle { 0 };
                systemObjects->GetCurrentThreadHandle (&handle);

                if (handle != 0)
                {
                    PWSTR desc { nullptr };
                    const HRESULT descResult { GetThreadDescription (
                        reinterpret_cast<HANDLE> (static_cast<ULONG_PTR> (handle)), &desc) };

                    if (SUCCEEDED (descResult) and desc != nullptr)
                    {
                        const juce::String threadName { desc };
                        LocalFree (desc);

                        if (threadName.isNotEmpty ())
                        {
                            name = threadName;
                        }
                    }
                }

                DynObj thread { new juce::DynamicObject () };
                thread->setProperty ("id",   static_cast<int> (systemIds.at (i)));
                thread->setProperty ("name", name);
                threads.add (juce::var (thread));
            }

            systemObjects->SetCurrentThreadId (savedEngineId);
        }
    }

    return threads;
}

// ---------------------------------------------------------------------------
// Session::getEventThreadSystemId
// ---------------------------------------------------------------------------

/** Returns the Win32 system thread ID of the thread that triggered the last
 *  event, using GetEventThread then GetCurrentThreadSystemId. */
std::uint32_t Session::getEventThreadSystemId () noexcept
{
    std::uint32_t systemId { 0 };

    if (systemObjects != nullptr)
    {
        ULONG eventEngineId { 0 };
        const HRESULT hr { systemObjects->GetEventThread (&eventEngineId) };

        if (SUCCEEDED (hr))
        {
            ULONG savedId { 0 };
            systemObjects->GetCurrentThreadId (&savedId);
            systemObjects->SetCurrentThreadId (eventEngineId);
            ULONG rawSystemId { 0 };
            systemObjects->GetCurrentThreadSystemId (&rawSystemId);
            systemObjects->SetCurrentThreadId (savedId);
            systemId = static_cast<std::uint32_t> (rawSystemId);
        }
    }

    return systemId;
}

// ---------------------------------------------------------------------------
// Session::setCurrentThreadBySystemId
// ---------------------------------------------------------------------------

/** Translates a Win32 system thread ID to a dbgeng engine thread ID via
 *  GetThreadIdBySystemId then sets it as current via SetCurrentThreadId. */
void Session::setCurrentThreadBySystemId (std::uint32_t systemId) noexcept
{
    if (systemObjects != nullptr and systemId != 0)
    {
        ULONG engineId { 0 };
        const HRESULT hr { systemObjects->GetThreadIdBySystemId (static_cast<ULONG> (systemId), &engineId) };

        if (SUCCEEDED (hr))
        {
            systemObjects->SetCurrentThreadId (engineId);
        }
    }
}

} // namespace debug

#endif // JUCE_WINDOWS
