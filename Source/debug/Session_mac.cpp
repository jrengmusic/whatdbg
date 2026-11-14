/** @file Session_mac.cpp
 *  @brief macOS debug session implementation using LLVM's liblldb SB API.
 *
 *  Platform counterpart to Session.cpp (Windows dbgeng). Implements the whole Session
 *  interface from Session.h using lldb::SBDebugger, SBTarget, SBProcess, SBThread,
 *  and SBFrame. The file holds the lifecycle, the event dispatch that pollEvents runs
 *  for process, stop, exit, and breakpoint events, the stop-reason and process-state
 *  handlers that fire events on Session::events, breakpoint creation and location
 *  lookup, thread and frame queries, variable inspection, expression evaluation, and
 *  getExceptionName.
 *
 *  Threading model: the engine context makes every SB API call, except wake and cancel,
 *  which the reader thread calls. pollEvents drives the event handlers, which fire
 *  events on Session::events.
 *
 *  Lifecycle: initialize → (launch | attach) → pollEvents loop → shutdown.
 */
#include <JuceHeader.h>
#include "Session.h"
#include "State.h"
#include "PrettyPrint.h"
#include "../dap/Types.h"
#include <csignal>

#if JUCE_MAC

namespace debug
{

static void onBreakpointStop (const lldb::StopReason& /*stopReason*/, debug::Events& events, lldb::SBThread& thread) noexcept
{
    events.get (Id::breakpoint, static_cast<int> (thread.GetStopReasonDataAtIndex (0)));
}

static void onStepStop (const lldb::StopReason& /*stopReason*/, debug::Events& events, lldb::SBThread& thread) noexcept
{
    juce::ignoreUnused (thread);
    events.get (Id::step);
}

static void onInterruptStop (const lldb::StopReason& /*stopReason*/, debug::Events& events, lldb::SBThread& thread) noexcept
{
    juce::ignoreUnused (thread);
    events.get (Id::pause);
}

static void onSignalStop (const lldb::StopReason& /*stopReason*/, debug::Events& events, lldb::SBThread& thread) noexcept
{
    events.get (Id::exception,
                static_cast<juce::int64> (static_cast<std::uint32_t> (thread.GetStopReasonDataAtIndex (0))),
                static_cast<juce::int64> (thread.GetSelectedFrame ().GetPC ()),
                0);
}

static void onExceptionStop (const lldb::StopReason& /*stopReason*/, debug::Events& events, lldb::SBThread& thread) noexcept
{
    events.get (Id::exception,
                static_cast<juce::int64> (static_cast<std::uint32_t> (thread.GetStopReasonDataAtIndex (0))),
                static_cast<juce::int64> (thread.GetSelectedFrame ().GetPC ()),
                1);
}

static constexpr std::size_t stdioReadBufferSize { 1024 };
static constexpr std::uint32_t allEventsMask { ~0u };
static constexpr std::uint32_t wakeEventMask { 1u << 0 };

static void drainProcessStdio (debug::Events& events,
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
            events.get (isStderr ? Id::standardError : Id::standardOutput,
                        juce::String (juce::CharPointer_UTF8 (buffer),
                                      juce::CharPointer_UTF8 (buffer + bytesRead)));
        }
    }
    while (bytesRead > 0);
}

static void onStopReason (WHATDBGModel& model, debug::Events& events, lldb::SBThread& thread, lldb::StopReason lldbStopReason) noexcept
{
    static const auto stopReasons ([] {
        jam::Function::Map<lldb::StopReason, jam::Function::pointerCapacity, void (debug::Events&, lldb::SBThread&)> stopReasons;

        stopReasons.add (lldb::eStopReasonBreakpoint,   onBreakpointStop);
        stopReasons.add (lldb::eStopReasonTrace,        onStepStop);
        stopReasons.add (lldb::eStopReasonPlanComplete, onStepStop);
        stopReasons.add (lldb::eStopReasonInterrupt,    onInterruptStop);
        stopReasons.add (lldb::eStopReasonSignal,       onSignalStop);
        stopReasons.add (lldb::eStopReasonException,    onExceptionStop);
        return stopReasons;
    } ());

    if (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isPausePending)->getValue () != 0)
    {
        onInterruptStop (lldbStopReason, events, thread);
    }
    else
    {
        const auto stopReasonEntry { stopReasons.find (lldbStopReason) };

        if (stopReasonEntry != stopReasons.end ())
        {
            const auto& [stopReasonCode, stopReasonCallback] { *stopReasonEntry };
            stopReasonCallback (stopReasonCode, events, thread);
        }
    }
}

static void onProcessStateStopped (const lldb::StateType& /*stateType*/,
                                   WHATDBGModel* model,
                                   debug::Events& events,
                                   lldb::SBProcess& process,
                                   lldb::SBEvent& event) noexcept
{
    if (not lldb::SBProcess::GetRestartedFromEvent (event))
    {
        auto thread { process.GetSelectedThread () };

        if (static_cast<debug::ExecutionState> (model->getParameter<jam::Parameter<int>> (model->sessionTag, Id::executionState)->getValue ()) == debug::ExecutionState::launching)
        {
            events.get (Id::initialBreak);
        }
        else
        {
            onStopReason (*model, events, thread, thread.GetStopReason ());
        }
    }
}

static void onProcessStateExited (const lldb::StateType& /*stateType*/,
                                  WHATDBGModel* model,
                                  debug::Events& events,
                                  lldb::SBProcess& process,
                                  lldb::SBEvent& event) noexcept
{
    juce::ignoreUnused (model, event);
    events.get (Id::exited, static_cast<int> (process.GetExitStatus ()));
}

static void onProcessStateRunning (const lldb::StateType& /*stateType*/,
                                   WHATDBGModel* model,
                                   debug::Events& events,
                                   lldb::SBProcess& process,
                                   lldb::SBEvent& event) noexcept
{
    juce::ignoreUnused (model, process, event);
    events.get (Id::running);
}

static void onProcessEvent (WHATDBGModel* model,
                            debug::Events& events,
                            lldb::SBProcess& process,
                            lldb::SBEvent& event) noexcept
{
    static constexpr bool isStandardOutput { false };
    static constexpr bool isStandardError  { true };

    static const auto processStates ([] {
        jam::Function::Map<lldb::StateType, jam::Function::pointerCapacity, void (WHATDBGModel*, debug::Events&, lldb::SBProcess&, lldb::SBEvent&)> processStates;

        processStates.add (lldb::eStateStopped,   onProcessStateStopped);
        processStates.add (lldb::eStateCrashed,   onProcessStateStopped);
        processStates.add (lldb::eStateSuspended, onProcessStateStopped);
        processStates.add (lldb::eStateExited,    onProcessStateExited);
        processStates.add (lldb::eStateRunning,   onProcessStateRunning);
        processStates.add (lldb::eStateStepping,  onProcessStateRunning);
        return processStates;
    } ());

    const std::uint32_t mask { event.GetType () };

    if ((mask bitand lldb::SBProcess::eBroadcastBitSTDOUT) != 0)
    {
        drainProcessStdio (events, process, isStandardOutput);
    }

    if ((mask bitand lldb::SBProcess::eBroadcastBitSTDERR) != 0)
    {
        drainProcessStdio (events, process, isStandardError);
    }

    const auto processState { lldb::SBProcess::GetStateFromEvent (event) };
    const auto processStateEntry { processStates.find (processState) };

    if (processStateEntry != processStates.end ())
    {
        const auto& [stateCode, stateCallback] { *processStateEntry };
        stateCallback (stateCode, model, events, process, event);
    }
}

static void onBreakpointEvent (debug::Events& events, lldb::SBEvent& event) noexcept
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

            events.get (Id::breakpointLocationFound,
                        static_cast<int> (bp.GetID ()),
                        static_cast<int> (lineEntry.IsValid () ? lineEntry.GetLine () : 0u));
        }
    }
}

Session::~Session ()
{
    shutdown (EndMode::passive);
}

bool Session::initialize (const juce::File& sidecarDir) noexcept
{
    juce::ignoreUnused (sidecarDir);

    lldb::SBDebugger::Initialize ();
    debugger = lldb::SBDebugger::Create (false);
    debugger.SetAsync (true);

    // Redirect the debugger's own stdin to /dev/null. Mirrors
    // lldb-dap DAP::ConfigureIO — prevents liblldb from competing with the
    // DAP reader thread for whatdbg's stdin.
    FILE* nullInput { std::fopen ("/dev/null", "r") };
    jassert (nullInput != nullptr);
    debugger.SetInputFile (lldb::SBFile (nullInput, true));

    listener = debugger.GetListener ();
    broadcaster.AddListener (listener, wakeEventMask);

    return debugger.IsValid ();
}

bool Session::launch (const juce::String& program) noexcept
{
    target = debugger.CreateTarget (program.toRawUTF8 ());

    if (target.IsValid ())
    {
        listener.StartListeningForEvents (target.GetBroadcaster (), allEventsMask);

        lldb::SBLaunchInfo launchInfo { nullptr };
        launchInfo.SetLaunchFlags (lldb::eLaunchFlagDebug
                                 | lldb::eLaunchFlagStopAtEntry);

        lldb::SBError error;
        process = target.Launch (launchInfo, error);

        if (error.Success () and process.IsValid ())
        {
            events.get (Id::targetProcessId, static_cast<int> (process.GetProcessID ()));
        }
    }

    return target.IsValid () and process.IsValid ();
}

bool Session::attach (std::uint32_t processId) noexcept
{
    target = debugger.CreateTarget ("");

    if (target.IsValid ())
    {
        listener.StartListeningForEvents (target.GetBroadcaster (), allEventsMask);

        lldb::SBError error;
        process = target.AttachToProcessWithID (listener,
                                                static_cast<lldb::pid_t> (processId),
                                                error);
    }

    // Processor::onAttach stores the target process id once this call returns true.
    return target.IsValid () and process.IsValid ();
}

void Session::resume () noexcept
{
    process.Continue ();
}

void Session::pollEvents (std::uint32_t timeoutMs) noexcept
{
    // SBListener::WaitForEvent only accepts whole-second timeouts. Round the
    // caller's millisecond timeout up so the wait actually blocks instead of
    // busy-spinning at timeoutSeconds == 0.
    static constexpr std::uint32_t millisecondsPerSecond { 1000 };
    const std::uint32_t timeoutSeconds {
        (timeoutMs + millisecondsPerSecond - 1) / millisecondsPerSecond };

    lldb::SBEvent event;
    const bool hasEvent { listener.WaitForEvent (timeoutSeconds, event) };

    if (hasEvent)
    {
        auto* model { WHATDBGModel::getInstance () };

        if (lldb::SBProcess::EventIsProcessEvent (event))
        {
            onProcessEvent (model, events, process, event);
        }
        else if (lldb::SBBreakpoint::EventIsBreakpointEvent (event))
        {
            onBreakpointEvent (events, event);
        }
    }
}

void Session::wake () noexcept
{
    broadcaster.BroadcastEventByType (wakeEventMask);
}

void Session::cancel () noexcept
{
    wake ();
}

void Session::resetCancel () noexcept
{
}

void Session::terminateDebuggee (std::uint32_t processId) noexcept
{
    juce::ignoreUnused (processId);
    const lldb::SBError error { process.Signal (SIGKILL) };

    if (error.Fail ())
    {
#if JUCE_DEBUG
        jam::debug::Log::write (juce::String ("[Session_mac] terminateDebuggee failed: ")
                                 + error.GetCString ());
#endif
        juce::ignoreUnused (process.Kill ());
    }
    else
    {
        resume ();
    }
}

void Session::shutdown (EndMode mode) noexcept
{
    // Idempotent: after the first call the debugger is invalid, so a second call
    // does nothing and does not terminate the SB subsystem twice.
    if (debugger.IsValid ())
    {
        switch (mode)
        {
            case EndMode::detach:
                process.Detach ();
                break;
            case EndMode::terminate:
                juce::ignoreUnused (process.Signal (SIGKILL));
                resume ();
                break;
            case EndMode::passive:
                break;
        }

        lldb::SBDebugger::Destroy (debugger);
        lldb::SBDebugger::Terminate ();
        debugger = lldb::SBDebugger ();
    }
}

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

std::optional<std::int32_t> Session::addBreakpoint (std::uint64_t offset) noexcept
{
    auto bp { target.BreakpointCreateByAddress (static_cast<lldb::addr_t> (offset)) };
    std::optional<std::int32_t> engineId;

    if (bp.IsValid ())
        engineId = bp.GetID ();

    return engineId;
}

std::optional<BreakpointLocation> Session::addBreakpointByLocation (const juce::String& filePath,
                                                                    std::uint16_t       line) noexcept
{
    auto bp { target.BreakpointCreateByLocation (filePath.toRawUTF8 (), line) };
    std::optional<BreakpointLocation> location;

    if (bp.IsValid ())
    {
        const std::int32_t   engineId     { bp.GetID () };
        const std::uint32_t  numLocations { static_cast<std::uint32_t> (bp.GetNumLocations ()) };
        std::uint16_t        resolvedLine { 0 };

        if (numLocations > 0)
        {
            auto loc { bp.GetLocationAtIndex (0) };
            auto lineEntry { loc.GetAddress ().GetLineEntry () };

            if (lineEntry.IsValid ())
                resolvedLine = static_cast<std::uint16_t> (lineEntry.GetLine ());
            else
                resolvedLine = line;
        }

        location = BreakpointLocation::pack (engineId, resolvedLine);
    }

    return location;
}

juce::Result Session::removeBreakpoint (std::int32_t engineId) noexcept
{
    const bool removed { target.BreakpointDelete (engineId) };
    return removed ? juce::Result::ok ()
                   : juce::Result::fail ("BreakpointDelete failed");
}

static lldb::SBLineEntry getLineEntry (lldb::SBTarget&     target,
                                       const juce::String& filePath,
                                       std::uint16_t       line) noexcept
{
    lldb::SBFileSpec fileSpec { filePath.toRawUTF8 (), true };
    lldb::SBLineEntry lineEntry;
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
                auto candidateEntry { compileUnit.GetLineEntryAtIndex (entryIndex) };

                if (candidateEntry.IsValid ())
                {
                    lineEntry = candidateEntry;
                    found     = true;
                }
            }
        }
    }

    return lineEntry;
}

debug::OffsetStatus Session::getOffsetStatus (const juce::String& filePath,
                                              std::uint16_t       line) noexcept
{
    return getLineEntry (target, filePath, line).IsValid ()
               ? debug::OffsetStatus::found
               : debug::OffsetStatus::notFound;
}

std::uint64_t Session::getOffset (const juce::String& filePath,
                                  std::uint16_t       line) noexcept
{
    auto lineEntry { getLineEntry (target, filePath, line) };

    return lineEntry.IsValid ()
               ? static_cast<std::uint64_t> (lineEntry.GetStartAddress ().GetLoadAddress (target))
               : 0;
}

juce::Result Session::loadModuleSymbols (const juce::String& imageName) noexcept
{
    juce::ignoreUnused (imageName);
    return juce::Result::ok ();
}

juce::Result Session::forceReloadAllSymbols () noexcept
{
    return juce::Result::ok ();
}

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

void Session::appendThreads (std::string& json) noexcept
{
    const std::uint32_t numThreads { process.GetNumThreads () };

    for (std::uint32_t i { 0 }; i < numThreads; ++i)
    {
        auto thread { process.GetThreadAtIndex (i) };
        const char* rawName { thread.GetName () };
        const lldb::tid_t tid { thread.GetThreadID () };
        jassert (tid <= std::numeric_limits<std::uint32_t>::max ());

        jam::JsonWriter::appendSeparator (json);
        jam::JsonWriter::appendObject (json, [tid, rawName] (std::string& threadEntry)
        {
            jam::JsonWriter::appendKey (threadEntry, Id::id);
            jam::Format::appendDecimal (threadEntry, static_cast<int> (static_cast<std::uint32_t> (tid)));
            jam::JsonWriter::appendKey (threadEntry, Id::name);
            jam::JsonWriter::appendString (threadEntry, rawName != nullptr ? rawName : "");
        });
    }
}

std::uint32_t Session::getEventThreadSystemId () noexcept
{
    const lldb::tid_t tid { process.GetSelectedThread ().GetThreadID () };
    jassert (tid <= std::numeric_limits<std::uint32_t>::max ());
    return static_cast<std::uint32_t> (tid);
}

void Session::setCurrentThreadBySystemId (std::uint32_t systemId) noexcept
{
    process.SetSelectedThreadByID (static_cast<lldb::tid_t> (systemId));
}

void Session::resetSymbolGroupCache () noexcept
{
    cachedFrameVariables.Clear ();
    cachedFrameIndex = -1;
}

juce::String getExceptionName (std::uint32_t code, bool isMachException) noexcept
{
    static const jam::HashMap<std::uint32_t, const char*> machExceptionNames
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

    static const jam::HashMap<std::uint32_t, const char*> signalNames
    {
        { 1,  "SIGHUP"                     },
        { 2,  "SIGINT"                     },
        { 3,  "SIGQUIT"                    },
        { 4,  machExceptionNames.at (2)    },
        { 5,  machExceptionNames.at (6)    },
        { 6,  "SIGABRT"                    },
        { 8,  machExceptionNames.at (3)    },
        { 9,  "SIGKILL"                    },
        { 10, machExceptionNames.at (1)    },
        { 11, machExceptionNames.at (1)    },
        { 13, "SIGPIPE"                    },
        { 14, "SIGALRM"                    },
        { 15, "SIGTERM"                    }
    };

    const auto& table { isMachException
        ? machExceptionNames
        : signalNames };

    const auto entry { table.find (code) };

    juce::String result;

    if (entry != table.end ())
    {
        const auto& [exceptionCode, exceptionName] { *entry };
        result = juce::String { exceptionName };
    }
    else
    {
        result = "0x" + juce::String::toHexString (static_cast<juce::int64> (code));
    }

    return result;
}

void Session::cacheFrameVariables (int frameIndex) noexcept
{
    if (cachedFrameIndex != frameIndex)
    {
        auto frame { process.GetSelectedThread ().GetFrameAtIndex (static_cast<std::uint32_t> (frameIndex)) };
        cachedFrameVariables = frame.GetVariables (true, true, true, true);
        cachedFrameIndex = frameIndex;
    }
}

static juce::String getDisplayValue (lldb::SBValue& value, const juce::String& typeName) noexcept
{
    const juce::String prettyValue { prettyPrint (value, typeName) };

    if (prettyValue.isNotEmpty ())
        return prettyValue;

    if (value.GetValue () != nullptr)
        return juce::String { value.GetValue () };

    if (value.GetSummary () != nullptr)
        return juce::String { value.GetSummary () };

    return juce::String ();
}

template <typename Function>
static void applyToVariable (lldb::SBValue& value, const juce::String& name, int symbolIndex, const Function& function) noexcept
{
    const char* rawType { value.GetTypeName () };
    const juce::String typeName { rawType != nullptr ? rawType : "" };
    const juce::String displayValue { getDisplayValue (value, typeName) };

    function (name, displayValue.isEmpty () ? juce::String ("<unavailable>") : displayValue, typeName, value.MightHaveChildren (), symbolIndex);
}

void Session::appendStackFrame (std::string& json, lldb::SBFrame frame, int frameId) noexcept
{
    static constexpr int firstColumn { 1 };
    const char* rawName { frame.GetFunctionName () };
    auto lineEntry { frame.GetLineEntry () };

    jam::JsonWriter::appendSeparator (json);
    jam::JsonWriter::appendObject (json, [rawName, frameId, &lineEntry] (std::string& frameEntry)
    {
        jam::JsonWriter::appendKey (frameEntry, Id::id);
        jam::Format::appendDecimal (frameEntry, frameId);
        jam::JsonWriter::appendKey (frameEntry, Id::name);
        jam::JsonWriter::appendString (frameEntry, rawName != nullptr ? rawName : "??");

        if (lineEntry.IsValid ())
        {
            const char* rawFileName { lineEntry.GetFileSpec ().GetFilename () };
            const char* rawFilePath { lineEntry.GetFileSpec ().GetDirectory () };
            const juce::String fileName { rawFileName != nullptr ? rawFileName : "" };
            const juce::String dirPath  { rawFilePath  != nullptr ? rawFilePath  : "" };
            const juce::String filePath { dirPath.isNotEmpty () ? dirPath + "/" + fileName : fileName };
            jam::JsonWriter::appendKey (frameEntry, Id::source);
            dap::appendSource (frameEntry, jam::Strings::getView (fileName), jam::Strings::getView (filePath));
            jam::JsonWriter::appendKey (frameEntry, Id::line);
            jam::Format::appendDecimal (frameEntry, static_cast<int> (lineEntry.GetLine ()));
            jam::JsonWriter::appendKey (frameEntry, Id::column);
            jam::Format::appendDecimal (frameEntry, firstColumn);
        }
    });
}

int Session::appendStackFrames (std::string& json, int maxFrames, const std::function<int (int frameIndex)>& addFrame) noexcept
{
    jassert (maxFrames > 0);

    auto thread { process.GetSelectedThread () };
    const int numFrames { static_cast<int> (thread.GetNumFrames ()) };
    const int clampedMax { juce::jmin (maxFrames, numFrames) };

    for (int i { 0 }; i < clampedMax; ++i)
        appendStackFrame (json, thread.GetFrameAtIndex (static_cast<std::uint32_t> (i)), addFrame (i));

    return clampedMax;
}

void Session::applyToLocals (int frameIndex, const std::function<void (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex)>& function) noexcept
{
    cacheFrameVariables (frameIndex);

    for (std::uint32_t i { 0 }; i < cachedFrameVariables.GetSize (); ++i)
    {
        auto value { cachedFrameVariables.GetValueAtIndex (i) };
        const char* rawName { value.GetName () };
        const juce::String name { rawName != nullptr ? rawName : "" };

        if (not shouldSkipSymbol (name))
        {
            applyToVariable (value, name, static_cast<int> (i), function);
        }
    }
}

void Session::applyToVariableChildren (int frameIndex, int symbolIndex, const std::function<void (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex)>& function) noexcept
{
    cacheFrameVariables (frameIndex);

    if (symbolIndex >= 0
        and symbolIndex < static_cast<int> (cachedFrameVariables.GetSize ()))
    {
        auto parent { cachedFrameVariables.GetValueAtIndex (static_cast<std::uint32_t> (symbolIndex)) };
        const std::uint32_t numChildren { parent.GetNumChildren () };

        for (std::uint32_t i { 0 }; i < numChildren; ++i)
        {
            auto child { parent.GetChildAtIndex (i) };
            const char* rawName { child.GetName () };
            const juce::String name { rawName != nullptr ? rawName : "" };

            if (not shouldSkipSymbol (name))
            {
                applyToVariable (child, name, static_cast<int> (i), function);
            }
        }
    }
}

juce::String Session::evaluateExpression (const juce::String& expression, int frameIndex) noexcept
{
    juce::String result;
    auto thread { process.GetSelectedThread () };
    auto frame { thread.GetFrameAtIndex (static_cast<std::uint32_t> (frameIndex)) };
    auto value { frame.EvaluateExpression (expression.toRawUTF8 ()) };

    if (value.IsValid () and value.GetError ().Success ())
    {
        const char* rawType { value.GetTypeName () };
        const juce::String typeName { rawType != nullptr ? rawType : "" };
        result = getDisplayValue (value, typeName);
    }
    else
    {
        if (value.GetError ().GetCString () != nullptr)
            result = juce::String { value.GetError ().GetCString () };
        else
            result = "evaluation failed";
    }

    return result;
}

} // namespace debug

#endif
