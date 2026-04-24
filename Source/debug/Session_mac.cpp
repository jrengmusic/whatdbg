/** @file Session_mac.cpp
 *  @brief macOS debug session implementation using LLVM's liblldb SB API.
 *
 *  Platform counterpart to Session.cpp (Windows dbgeng). Implements the Session
 *  interface declared in Session.h using lldb::SBDebugger, SBTarget, SBProcess,
 *  SBThread, and SBFrame.
 *
 *  Threading model: single-threaded. All SB API calls happen on the main thread.
 *  Event polling via SBListener::WaitForEvent drives the deferred-event state
 *  machine defined in Session.h (State struct).
 *
 *  Lifecycle: initialize → (launch | attach) → pollEvents loop → shutdown.
 */
#include <JuceHeader.h>
#include "Session.h"
#include "State.h"
#include "../dap/Types.h"
#include "../Log.h"
#include <cerrno>
#include <cstdio>
#include <limits>
#include <signal.h>
#include <unordered_map>

namespace debug
{

// ---------------------------------------------------------------------------
// Stop-reason handlers (TU-local)
// ---------------------------------------------------------------------------

/** Handle breakpoint stop — read hit breakpoint ID from stop data index 0, set stopped state. */
static void handleBreakpointStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    state->hasBreakpointHit = true;
    state->breakpointEngineId =
        static_cast<std::uint32_t> (thread.GetStopReasonDataAtIndex (0));
    state->executionState = debug::ExecutionState::stopped;
}

/** Handle step-complete stop (eStopReasonTrace / eStopReasonPlanComplete) — signal step done, set stopped state. */
static void handleStepStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    juce::ignoreUnused (thread);
    state->hasStepCompleted = true;
    state->executionState = debug::ExecutionState::stopped;
}

/** Handle async-interrupt stop — transition execution state to stopped; no hit data to record. */
static void handleInterruptStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    juce::ignoreUnused (thread);
    state->executionState = debug::ExecutionState::stopped;
}

/** Handle POSIX signal stop — record signal number and faulting PC as a non-Mach exception. */
static void handleSignalStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    state->hasExceptionStopped = true;
    state->isMachException     = false;
    state->exceptionCode       =
        static_cast<std::uint32_t> (thread.GetStopReasonDataAtIndex (0));
    state->exceptionAddress    = thread.GetSelectedFrame ().GetPC ();
    state->executionState      = debug::ExecutionState::stopped;
}

/** Handle Mach exception stop — record exception code and faulting PC, flag as Mach-layer fault. */
static void handleExceptionStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    state->hasExceptionStopped = true;
    state->isMachException     = true;
    state->exceptionCode       =
        static_cast<std::uint32_t> (thread.GetStopReasonDataAtIndex (0));
    state->exceptionAddress    = thread.GetSelectedFrame ().GetPC ();
    state->executionState      = debug::ExecutionState::stopped;
}

// ---------------------------------------------------------------------------
// Stdio drain (TU-local)
// ---------------------------------------------------------------------------

static constexpr std::size_t stdioReadBufferSize { 1024 };

/** Drain available stdout or stderr from the process into State::debuggeeOutputText in a read loop. */
static void drainProcessStdio (debug::State* state,
                               lldb::SBProcess& process,
                               bool isStderr) noexcept
{
    char buffer [stdioReadBufferSize];
    std::size_t bytesRead { 0 };

    do
    {
        bytesRead = isStderr
            ? process.GetSTDERR (buffer, sizeof (buffer))
            : process.GetSTDOUT (buffer, sizeof (buffer));

        if (bytesRead > 0)
        {
            state->debuggeeOutputText += juce::String (juce::CharPointer_UTF8 (buffer),
                                                       juce::CharPointer_UTF8 (buffer + bytesRead));
            state->hasDebuggeeOutput   = true;
        }
    }
    while (bytesRead > 0);
}

static const std::unordered_map<lldb::StopReason,
                                void (*)(debug::State*, lldb::SBThread&)>
    stopReasonHandlers
    {
        { lldb::eStopReasonBreakpoint,   handleBreakpointStop   },
        { lldb::eStopReasonTrace,        handleStepStop         },
        { lldb::eStopReasonPlanComplete, handleStepStop         },
        { lldb::eStopReasonInterrupt,    handleInterruptStop    },
        { lldb::eStopReasonSignal,       handleSignalStop       },
        { lldb::eStopReasonException,    handleExceptionStop    }
    };

// ---------------------------------------------------------------------------
// Broadcaster-level dispatch (TU-local)
// ---------------------------------------------------------------------------

/** Dispatch a process broadcaster event — drain stdio, then fan out by process state or stop reason. */
static void handleProcessEvent (debug::State* state,
                                lldb::SBProcess& process,
                                lldb::SBEvent& event) noexcept
{
    const std::uint32_t mask { event.GetType () };

    if ((mask bitand lldb::SBProcess::eBroadcastBitSTDOUT) != 0)
    {
        drainProcessStdio (state, process, false);
    }

    if ((mask bitand lldb::SBProcess::eBroadcastBitSTDERR) != 0)
    {
        drainProcessStdio (state, process, true);
    }

    const auto processState { lldb::SBProcess::GetStateFromEvent (event) };

    switch (processState)
    {
        case lldb::eStateStopped:
        case lldb::eStateCrashed:
        case lldb::eStateSuspended:
        {
            if (not lldb::SBProcess::GetRestartedFromEvent (event))
            {
                auto thread { process.GetSelectedThread () };
                const auto stopReason { thread.GetStopReason () };

                if (state->initialBreakPhase == debug::InitialBreakPhase::notHit)
                {
                    state->initialBreakPhase = debug::InitialBreakPhase::pending;
                    state->executionState    = debug::ExecutionState::stopped;
                }
                else
                {
                    const auto handlerEntry { stopReasonHandlers.find (stopReason) };

                    if (handlerEntry != stopReasonHandlers.end ())
                    {
                        handlerEntry->second (state, thread);
                    }
                }
            }
            break;
        }
        case lldb::eStateExited:
            state->processExitCode  = process.GetExitStatus ();
            state->hasProcessExited = true;
            state->executionState   = debug::ExecutionState::exited;
            break;
        case lldb::eStateRunning:
        case lldb::eStateStepping:
            state->executionState = debug::ExecutionState::running;
            break;
        default:
            break;
    }
}

/** Handle breakpoint broadcaster event — on locations-resolved/added, extract resolved line and engine ID into State. */
static void handleBreakpointEvent (debug::State* state, lldb::SBEvent& event) noexcept
{
    const auto eventType { lldb::SBBreakpoint::GetBreakpointEventTypeFromEvent (event) };

    const bool isResolved { (eventType == lldb::eBreakpointEventTypeLocationsResolved)
                         or (eventType == lldb::eBreakpointEventTypeLocationsAdded) };

    if (isResolved)
    {
        auto bp { lldb::SBBreakpoint::GetBreakpointFromEvent (event) };

        if (bp.IsValid () and bp.GetNumLocations () > 0)
        {
            auto loc { bp.GetLocationAtIndex (0) };
            auto lineEntry { loc.GetAddress ().GetLineEntry () };

            state->resolvedBreakpointEngineId = static_cast<std::uint32_t> (bp.GetID ());
            state->resolvedBreakpointLine     = lineEntry.IsValid () ? lineEntry.GetLine () : 0u;
            state->hasBreakpointLocationsResolved = true;
        }
    }
}

/** Handle target broadcaster event — on eBroadcastBitModulesLoaded, record the first loaded module filename in State. */
static void handleTargetEvent (debug::State* state, lldb::SBEvent& event) noexcept
{
    const std::uint32_t mask { event.GetType () };

    if ((mask bitand lldb::SBTarget::eBroadcastBitModulesLoaded) != 0)
    {
        const std::uint32_t numModules {
            static_cast<std::uint32_t> (
                lldb::SBTarget::GetNumModulesFromEvent (event)) };

        if (numModules > 0)
        {
            auto moduleRef { lldb::SBTarget::GetModuleAtIndexFromEvent (0, event) };
            state->lastLoadedImageName =
                juce::String (moduleRef.GetFileSpec ().GetFilename ());
            state->hasNewModuleLoaded = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Session::~Session
// ---------------------------------------------------------------------------

/** Destructor — passive shutdown: destroys SBDebugger without killing or detaching the target. */
Session::~Session ()
{
    shutdown (EndMode::passive);
}

// ---------------------------------------------------------------------------
// Session::initialize
// ---------------------------------------------------------------------------

/** Initialize the debug session — create SBDebugger in async mode, redirect stdin to /dev/null, acquire listener. */
bool Session::initialize (const juce::File& sidecarDir) noexcept
{
    juce::ignoreUnused (sidecarDir);

    lldb::SBDebugger::Initialize ();
    debugger = lldb::SBDebugger::Create (false);
    debugger.SetAsync (true);

    // Redirect the debugger's own stdin to /dev/null. Mirrors
    // lldb-dap DAP::ConfigureIO — prevents liblldb from competing with the
    // DAP reader thread for whatdbg's stdin.
    debugger.SetInputFile (lldb::SBFile (std::fopen ("/dev/null", "r"), true));

    listener = debugger.GetListener ();

    return debugger.IsValid ();
}

// ---------------------------------------------------------------------------
// Session::launch
// ---------------------------------------------------------------------------

/** Launch the target under the debugger — creates target, subscribes listener, launches with eLaunchFlagStopAtEntry. */
bool Session::launch (const juce::String& program) noexcept
{
    target = debugger.CreateTarget (program.toRawUTF8 ());

    if (target.IsValid ())
    {
        listener.StartListeningForEvents (target.GetBroadcaster (), ~0u);

        lldb::SBLaunchInfo launchInfo { nullptr };
        launchInfo.SetLaunchFlags (lldb::eLaunchFlagDebug
                                 | lldb::eLaunchFlagStopAtEntry);

        lldb::SBError error;
        process = target.Launch (launchInfo, error);

        if (error.Success () and process.IsValid ())
        {
            State::getContext ()->targetProcessId =
                static_cast<std::uint32_t> (process.GetProcessID ());
        }
    }

    return target.IsValid () and process.IsValid ();
}

// ---------------------------------------------------------------------------
// Session::attach
// ---------------------------------------------------------------------------

/** Attach to a running process by PID — creates an empty target, attaches via listener, records PID in State. */
bool Session::attach (std::uint32_t processId) noexcept
{
    target = debugger.CreateTarget ("");

    if (target.IsValid ())
    {
        listener.StartListeningForEvents (target.GetBroadcaster (), ~0u);

        lldb::SBError error;
        process = target.AttachToProcessWithID (listener,
                                                static_cast<lldb::pid_t> (processId),
                                                error);

        if (error.Success () and process.IsValid ())
        {
            State::getContext ()->targetProcessId = processId;
        }
    }

    return target.IsValid () and process.IsValid ();
}

// ---------------------------------------------------------------------------
// Session::resume
// ---------------------------------------------------------------------------

/** Resume execution — forwards to SBProcess::Continue. */
void Session::resume () noexcept
{
    process.Continue ();
}

// ---------------------------------------------------------------------------
// Session::pollEvents
// ---------------------------------------------------------------------------

/** Poll the SBListener for one event (non-blocking, timeout=0) and dispatch to the appropriate handler. */
juce::Result Session::pollEvents (std::uint32_t timeoutMs, bool& outHadEvent) noexcept
{
    // D-5-A: WaitForEvent takes seconds (uint32_t); caller's ms-granularity timeout doesn't map.
    // Non-blocking poll; main-loop pacing handles cadence (PLAN line 88-96).
    juce::ignoreUnused (timeoutMs);

    lldb::SBEvent event;
    const bool hasEvent { listener.WaitForEvent (0, event) };
    outHadEvent = hasEvent;

    if (hasEvent)
    {
        auto* debugState { State::getContext () };

        if (lldb::SBProcess::EventIsProcessEvent (event))
        {
            handleProcessEvent (debugState, process, event);
        }
        else if (lldb::SBTarget::EventIsTargetEvent (event))
        {
            handleTargetEvent (debugState, event);
        }
        else if (lldb::SBBreakpoint::EventIsBreakpointEvent (event))
        {
            handleBreakpointEvent (debugState, event);
        }
    }

    return juce::Result::ok ();
}

// ---------------------------------------------------------------------------
// Session::shutdown
// ---------------------------------------------------------------------------

/** Shut down the session — terminate via SIGKILL, detach, or passive destroy depending on EndMode. */
void Session::shutdown (EndMode mode) noexcept
{
    switch (mode)
    {
        case EndMode::terminate:
        {
            const auto pid { State::getContext ()->targetProcessId };

            if (pid != 0)
            {
                ::kill (static_cast<pid_t> (pid), SIGKILL);
            }
            break;
        }
        case EndMode::detach:
            if (process.IsValid ())
            {
                process.Detach ();
            }
            break;
        case EndMode::passive:
        default:
            break;
    }

    lldb::SBDebugger::Destroy (debugger);
    lldb::SBDebugger::Terminate ();
}

// ---------------------------------------------------------------------------
// Session::stepOver / stepInto / stepOut / interrupt
// ---------------------------------------------------------------------------

/** Step over the current source line on the selected thread, staying within the current frame. */
void Session::stepOver () noexcept
{
    process.GetSelectedThread ().StepOver (lldb::eOnlyDuringStepping);
}

/** Step into the next call on the selected thread. */
void Session::stepInto () noexcept
{
    process.GetSelectedThread ().StepInto ();
}

/** Step out of the current frame — uses StepOutOfFrame so the exact return address is the step target. */
void Session::stepOut () noexcept
{
    auto thread { process.GetSelectedThread () };
    auto frame  { thread.GetSelectedFrame () };
    thread.StepOutOfFrame (frame);
}

/** Async-interrupt the running process — processId unused on macOS; SBProcess already holds the target. */
void Session::interrupt (std::uint32_t processId) noexcept
{
    // macOS: the bound process member already knows the target; processId is unused.
    juce::ignoreUnused (processId);
    process.SendAsyncInterrupt ();
}

// ---------------------------------------------------------------------------
// Session::addBreakpoint / removeBreakpoint
// ---------------------------------------------------------------------------

/** Set a breakpoint at an absolute load address — assigns outEngineId to the SBBreakpoint ID on success. */
juce::Result Session::addBreakpoint (std::uint64_t offset, std::uint32_t* outEngineId) noexcept
{
    jassert (outEngineId != nullptr);
    auto bp { target.BreakpointCreateByAddress (static_cast<lldb::addr_t> (offset)) };
    juce::Result result { juce::Result::fail ("BreakpointCreateByAddress failed") };

    if (bp.IsValid ())
    {
        *outEngineId = static_cast<std::uint32_t> (bp.GetID ());
        result = juce::Result::ok ();
    }

    return result;
}

/** Set a source-level breakpoint; writes engine ID and DWARF-resolved line to out-params (0 if unresolved at call time). */
juce::Result Session::addBreakpointByLocation (const juce::String& filePath,
                                               std::uint32_t       line,
                                               std::uint32_t*      outEngineId,
                                               std::uint32_t*      outResolvedLine) noexcept
{
    jassert (outEngineId != nullptr);
    jassert (outResolvedLine != nullptr);

    auto bp { target.BreakpointCreateByLocation (filePath.toRawUTF8 (), line) };
    juce::Result result { juce::Result::fail ("BreakpointCreateByLocation failed") };

    if (bp.IsValid ())
    {
        *outEngineId = static_cast<std::uint32_t> (bp.GetID ());

        const std::uint32_t numLocations { static_cast<std::uint32_t> (bp.GetNumLocations ()) };

        if (numLocations > 0)
        {
            auto loc { bp.GetLocationAtIndex (0) };
            auto lineEntry { loc.GetAddress ().GetLineEntry () };

            if (lineEntry.IsValid ())
                *outResolvedLine = lineEntry.GetLine ();
            else
                *outResolvedLine = line;
        }
        else
        {
            *outResolvedLine = 0;
        }

        result = juce::Result::ok ();
    }

    return result;
}

/** Remove a breakpoint by its SBBreakpoint ID — forwards to SBTarget::BreakpointDelete. */
juce::Result Session::removeBreakpoint (std::uint32_t engineId) noexcept
{
    const bool removed { target.BreakpointDelete (static_cast<lldb::break_id_t> (engineId)) };
    return removed ? juce::Result::ok ()
                   : juce::Result::fail ("BreakpointDelete failed");
}

// ---------------------------------------------------------------------------
// Session::getOffsetByLine / getLineByOffset
// ---------------------------------------------------------------------------

/** Walk all loaded modules' compile units via DWARF to find the load address for a given source line. */
debug::ResolveStatus Session::getOffsetByLine (const juce::String& filePath,
                                               std::uint32_t       line,
                                               std::uint64_t*      outOffset) noexcept
{
    jassert (outOffset != nullptr);
    // WHY: engineBusy maps to a dbgeng-specific transient HRESULT (E_UNEXPECTED);
    // liblldb has no equivalent transient state — lookups are resolved or not (PLAN A.1).
    lldb::SBFileSpec fileSpec { filePath.toRawUTF8 (), true };
    debug::ResolveStatus status { debug::ResolveStatus::notFound };
    bool found { false };

    const std::uint32_t numModules { target.GetNumModules () };

    for (std::uint32_t moduleIndex { 0 }; moduleIndex < numModules and not found; ++moduleIndex)
    {
        auto moduleRef { target.GetModuleAtIndex (moduleIndex) };
        const std::uint32_t numUnits { moduleRef.GetNumCompileUnits () };

        for (std::uint32_t unitIndex { 0 }; unitIndex < numUnits and not found; ++unitIndex)
        {
            auto compileUnit { moduleRef.GetCompileUnitAtIndex (unitIndex) };
            const std::uint32_t entryIndex {
                compileUnit.FindLineEntryIndex (0, line, &fileSpec) };

            if (entryIndex != UINT32_MAX)
            {
                auto lineEntry { compileUnit.GetLineEntryAtIndex (entryIndex) };

                if (lineEntry.IsValid ())
                {
                    *outOffset = static_cast<std::uint64_t> (
                        lineEntry.GetStartAddress ().GetLoadAddress (target));
                    status = debug::ResolveStatus::resolved;
                    found  = true;
                }
            }
        }
    }

    return status;
}

/** Resolve a load address to source file and line via SBAddress::GetLineEntry. */
juce::Result Session::getLineByOffset (std::uint64_t offset,
                                       juce::String& outFilePath,
                                       std::uint32_t* outLine) noexcept
{
    jassert (outLine != nullptr);
    lldb::SBAddress address { static_cast<lldb::addr_t> (offset), target };
    auto lineEntry { address.GetLineEntry () };
    juce::Result result { juce::Result::fail ("no source info at offset") };

    if (lineEntry.IsValid ())
    {
        outFilePath = juce::String (lineEntry.GetFileSpec ().GetFilename ());
        *outLine    = lineEntry.GetLine ();
        result      = juce::Result::ok ();
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::loadModuleSymbols / forceReloadAllSymbols
// ---------------------------------------------------------------------------

/** No-op on macOS — liblldb loads symbols automatically on module-load events. */
juce::Result Session::loadModuleSymbols (const juce::String& imageName) noexcept
{
    // WHY: liblldb loads symbols on module-load events automatically; no explicit reload needed (PLAN A.1, Phase 3.5).
    juce::ignoreUnused (imageName);
    return juce::Result::ok ();
}

/** No-op on macOS — liblldb loads symbols automatically on module-load events. */
juce::Result Session::forceReloadAllSymbols () noexcept
{
    // WHY: liblldb loads symbols on module-load events automatically; no explicit reload needed (PLAN A.1, Phase 3.5).
    return juce::Result::ok ();
}

// ---------------------------------------------------------------------------
// Session::appendSymbolPath / appendSourcePath
// ---------------------------------------------------------------------------

/** Append a directory to target.debug-file-search-paths via the LLDB command interpreter. */
void Session::appendSymbolPath (const juce::String& path) noexcept
{
    const juce::String command { "settings append target.debug-file-search-paths " + path };
    debugger.HandleCommand (command.toRawUTF8 ());
}

/** Set target.source-map "." → path via command interpreter; mirrors lldb-dap single-root remap convention. */
void Session::appendSourcePath (const juce::String& path) noexcept
{
    // LLDB has no additive source search path; target.source-map is from->to remap.
    // Mirrors lldb-dap DAP.cpp:1101 — remap "." to the given dir when caller supplies
    // a single path. "settings set" replaces the full map; current callers invoke once.
    const juce::String command { "settings set target.source-map \".\" \"" + path + "\"" };
    debugger.HandleCommand (command.toRawUTF8 ());
}

// ---------------------------------------------------------------------------
// Session::getThreads
// ---------------------------------------------------------------------------

/** Build a DAP threads array from all SBProcess threads — id (uint32) and name per entry. */
juce::Array<juce::var> Session::getThreads () noexcept
{
    juce::Array<juce::var> threads;
    const std::uint32_t numThreads { process.GetNumThreads () };

    for (std::uint32_t i { 0 }; i < numThreads; ++i)
    {
        auto thread { process.GetThreadAtIndex (i) };
        const char* rawName { thread.GetName () };
        const lldb::tid_t tid { thread.GetThreadID () };
        jassert (tid <= std::numeric_limits<std::uint32_t>::max ());

        dap::DynObj threadEntry { new juce::DynamicObject () };
        threadEntry->setProperty ("id",   static_cast<int> (static_cast<std::uint32_t> (tid)));
        threadEntry->setProperty ("name", juce::String (rawName != nullptr ? rawName : ""));
        threads.add (juce::var (threadEntry));
    }

    return threads;
}

// ---------------------------------------------------------------------------
// Session::getEventThreadSystemId
// ---------------------------------------------------------------------------

/** Return the thread ID of the currently selected (event-triggering) thread. */
std::uint32_t Session::getEventThreadSystemId () noexcept
{
    const lldb::tid_t tid { process.GetSelectedThread ().GetThreadID () };
    jassert (tid <= std::numeric_limits<std::uint32_t>::max ());
    return static_cast<std::uint32_t> (tid);
}

// ---------------------------------------------------------------------------
// Session::setCurrentThreadBySystemId
// ---------------------------------------------------------------------------

/** Select the active thread by system TID — forwards to SBProcess::SetSelectedThreadByID. */
void Session::setCurrentThreadBySystemId (std::uint32_t systemId) noexcept
{
    process.SetSelectedThreadByID (static_cast<lldb::tid_t> (systemId));
}

// ---------------------------------------------------------------------------
// Session::resetSymbolGroupCache
// ---------------------------------------------------------------------------

/** Invalidate the cached SBValueList and frame index so the next variable query re-fetches from liblldb. */
void Session::resetSymbolGroupCache () noexcept
{
    cachedFrameVariables.Clear ();
    cachedFrameIndex = -1;
}

// ---------------------------------------------------------------------------
// Signal / exception name table + getExceptionName (macOS impl)
// ---------------------------------------------------------------------------

static const std::unordered_map<std::uint32_t, const char*> signalNames
{
    { 1,  "SIGHUP"               },
    { 2,  "SIGINT"               },
    { 3,  "SIGQUIT"              },
    { 4,  "EXC_BAD_INSTRUCTION"  },  // SIGILL
    { 5,  "EXC_BREAKPOINT"       },  // SIGTRAP
    { 6,  "SIGABRT"              },
    { 8,  "EXC_ARITHMETIC"       },  // SIGFPE
    { 9,  "SIGKILL"              },
    { 10, "EXC_BAD_ACCESS"       },  // SIGBUS
    { 11, "EXC_BAD_ACCESS"       },  // SIGSEGV
    { 13, "SIGPIPE"              },
    { 14, "SIGALRM"              },
    { 15, "SIGTERM"              }
};

static const std::unordered_map<std::uint32_t, const char*> machExceptionNames
{
    { 1,  "EXC_BAD_ACCESS"      },
    { 2,  "EXC_BAD_INSTRUCTION" },
    { 3,  "EXC_ARITHMETIC"      },
    { 4,  "EXC_EMULATION"       },
    { 5,  "EXC_SOFTWARE"        },
    { 6,  "EXC_BREAKPOINT"      },
    { 7,  "EXC_SYSCALL"         },
    { 8,  "EXC_MACH_SYSCALL"    },
    { 9,  "EXC_RPC_ALERT"       },
    { 10, "EXC_CRASH"           }
};

/** Resolve an exception/signal code to a human-readable name — selects Mach or POSIX table from State::isMachException. */
juce::String getExceptionName (std::uint32_t code) noexcept
{
    const auto* state { State::getContext () };
    const auto& table { (state != nullptr and state->isMachException)
        ? machExceptionNames
        : signalNames };

    const auto entry { table.find (code) };

    juce::String result;

    if (entry != table.end ())
    {
        result = juce::String { entry->second };
    }
    else
    {
        result = "0x" + juce::String::toHexString (static_cast<juce::int64> (code));
    }

    return result;
}

} // namespace debug
