#include <JuceHeader.h>
#include "Session.h"
#include "State.h"
#include "../Log.h"
#include "../dap/Types.h"
#include <limits>
#include <unordered_map>

namespace debug
{

// ---------------------------------------------------------------------------
// Stop-reason handlers (TU-local)
// ---------------------------------------------------------------------------

static void handleBreakpointStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    state->hasBreakpointHit = true;
    state->breakpointEngineId =
        static_cast<std::uint32_t> (thread.GetStopReasonDataAtIndex (0));
    state->executionState = debug::ExecutionState::stopped;
}

static void handleStepStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    juce::ignoreUnused (thread);
    state->hasStepCompleted = true;
    state->executionState = debug::ExecutionState::stopped;
}

static void handleInterruptStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    juce::ignoreUnused (thread);
    state->executionState = debug::ExecutionState::stopped;
}

static void handleSignalStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    state->hasExceptionStopped = true;
    state->exceptionCode =
        static_cast<std::uint32_t> (thread.GetStopReasonDataAtIndex (0));
    state->exceptionAddress = thread.GetSelectedFrame ().GetPC ();
    state->executionState = debug::ExecutionState::stopped;
}

static void handleExceptionStop (debug::State* state, lldb::SBThread& thread) noexcept
{
    state->hasExceptionStopped = true;
    state->exceptionCode =
        static_cast<std::uint32_t> (thread.GetStopReasonDataAtIndex (0));
    state->exceptionAddress = thread.GetSelectedFrame ().GetPC ();
    state->executionState = debug::ExecutionState::stopped;
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

static void handleProcessEvent (debug::State* state,
                                lldb::SBProcess& process,
                                lldb::SBEvent& event) noexcept
{
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
                const auto stopReason   { thread.GetStopReason () };
                const auto handlerEntry { stopReasonHandlers.find (stopReason) };

                if (handlerEntry != stopReasonHandlers.end ())
                {
                    handlerEntry->second (state, thread);
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

Session::~Session ()
{
    shutdown (EndMode::passive);
}

// ---------------------------------------------------------------------------
// Session::initialize
// ---------------------------------------------------------------------------

bool Session::initialize (const juce::File& sidecarDir) noexcept
{
    juce::ignoreUnused (sidecarDir);

    lldb::SBDebugger::Initialize ();
    debugger = lldb::SBDebugger::Create (false);
    debugger.SetAsync (true);

    listener = debugger.GetListener ();

    listener.StartListeningForEventClass (
        debugger,
        lldb::SBProcess::GetBroadcasterClassName (),
        lldb::SBProcess::eBroadcastBitStateChanged
        | lldb::SBProcess::eBroadcastBitSTDOUT
        | lldb::SBProcess::eBroadcastBitSTDERR);

    listener.StartListeningForEventClass (
        debugger,
        lldb::SBTarget::GetBroadcasterClassName (),
        lldb::SBTarget::eBroadcastBitModulesLoaded
        | lldb::SBTarget::eBroadcastBitBreakpointChanged);

    return debugger.IsValid ();
}

// ---------------------------------------------------------------------------
// Session::launch
// ---------------------------------------------------------------------------

bool Session::launch (const juce::String& program) noexcept
{
    target = debugger.CreateTarget (program.toRawUTF8 ());

    if (target.IsValid ())
    {
        lldb::SBLaunchInfo launchInfo { nullptr };
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

bool Session::attach (std::uint32_t processId) noexcept
{
    target = debugger.CreateTarget ("");

    if (target.IsValid ())
    {
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

void Session::resume () noexcept
{
    process.Continue ();
}

// ---------------------------------------------------------------------------
// Session::pollEvents
// ---------------------------------------------------------------------------

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
    }

    return juce::Result::ok ();
}

// ---------------------------------------------------------------------------
// Session::shutdown
// ---------------------------------------------------------------------------

void Session::shutdown (EndMode mode) noexcept
{
    switch (mode)
    {
        case EndMode::terminate:
            if (process.IsValid ())
                process.Kill ();
            break;
        case EndMode::detach:
            if (process.IsValid ())
                process.Detach ();
            break;
        case EndMode::passive:
        default:
            break;
    }

    lldb::SBDebugger::Destroy (debugger);
}

// ---------------------------------------------------------------------------
// Session::stepOver / stepInto / stepOut / interrupt
// ---------------------------------------------------------------------------

void Session::stepOver () noexcept
{
    process.GetSelectedThread ().StepOver (lldb::eOnlyDuringStepping);
}

void Session::stepInto () noexcept
{
    process.GetSelectedThread ().StepInto ();
}

void Session::stepOut () noexcept
{
    auto thread { process.GetSelectedThread () };
    auto frame  { thread.GetSelectedFrame () };
    thread.StepOutOfFrame (frame);
}

void Session::interrupt (std::uint32_t processId) noexcept
{
    // macOS: the bound process member already knows the target; processId is unused.
    juce::ignoreUnused (processId);
    process.SendAsyncInterrupt ();
}

// ---------------------------------------------------------------------------
// Session::addBreakpoint / removeBreakpoint
// ---------------------------------------------------------------------------

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

juce::Result Session::removeBreakpoint (std::uint32_t engineId) noexcept
{
    const bool removed { target.BreakpointDelete (static_cast<lldb::break_id_t> (engineId)) };
    return removed ? juce::Result::ok ()
                   : juce::Result::fail ("BreakpointDelete failed");
}

// ---------------------------------------------------------------------------
// Session::getOffsetByLine / getLineByOffset
// ---------------------------------------------------------------------------

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

juce::Result Session::loadModuleSymbols (const juce::String& imageName) noexcept
{
    // WHY: liblldb loads symbols on module-load events automatically; no explicit reload needed (PLAN A.1, Phase 3.5).
    juce::ignoreUnused (imageName);
    return juce::Result::ok ();
}

juce::Result Session::forceReloadAllSymbols () noexcept
{
    // WHY: liblldb loads symbols on module-load events automatically; no explicit reload needed (PLAN A.1, Phase 3.5).
    return juce::Result::ok ();
}

// ---------------------------------------------------------------------------
// Session::appendSymbolPath / appendSourcePath
// ---------------------------------------------------------------------------

void Session::appendSymbolPath (const juce::String& path) noexcept
{
    const juce::String command { "settings append target.debug-file-search-paths " + path };
    debugger.HandleCommand (command.toRawUTF8 ());
}

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

std::uint32_t Session::getEventThreadSystemId () noexcept
{
    const lldb::tid_t tid { process.GetSelectedThread ().GetThreadID () };
    jassert (tid <= std::numeric_limits<std::uint32_t>::max ());
    return static_cast<std::uint32_t> (tid);
}

// ---------------------------------------------------------------------------
// Session::setCurrentThreadBySystemId
// ---------------------------------------------------------------------------

void Session::setCurrentThreadBySystemId (std::uint32_t systemId) noexcept
{
    process.SetSelectedThreadByID (static_cast<lldb::tid_t> (systemId));
}

// ---------------------------------------------------------------------------
// Session::resetSymbolGroupCache
// ---------------------------------------------------------------------------

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

juce::String getExceptionName (std::uint32_t code) noexcept
{
    const auto entry { signalNames.find (code) };

    juce::String result;

    if (entry != signalNames.end ())
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
