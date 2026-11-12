#pragma once
#include <JuceHeader.h>

namespace debug
{

/** Execution lifecycle states for the debug target.
 *
 *  Represents the coarse state machine of a debug session as seen by the DAP layer.
 *  Whatdbg and the engine event paths write the state. Whatdbg::run reads it.
 */
enum class ExecutionState : int
{
    idle      = 0,  ///< No target attached. Initial state.
    launching = 1,  ///< launch or attach request sent; waiting for first CreateProcess event.
    running   = 2,  ///< Target is executing. Resume/step have been issued.
    stopped   = 3,  ///< Target is paused at a breakpoint, step completion, or exception.
    exited    = 4   ///< Target process has exited. Session is shutting down.
};

/** Cause of the current stop, held as one value in State::stopReason.
 *
 *  Whatdbg::processDeferredEvents reads stopReason and runs the drain for it.
 *  Writers, by value:
 *  - none:         The default. The drains set it back to none when they run.
 *                  drainInitialBreak does so only after configurationDone.
 *  - initialBreak: Callbacks.cpp onBreakpoint while launching; Session_mac
 *                  onProcessStateStopped while launching; Whatdbg::onAttach on macOS.
 *  - moduleLoad:   Callbacks.cpp LoadModule, when no step is pending.
 *  - breakpoint:   Callbacks.cpp Breakpoint; Session_mac onBreakpointStop.
 *  - step:         Session::pollEvents on Windows, when no other stop is set; Session_mac onStepStop.
 *  - pause:        Callbacks.cpp onBreakpoint while isPausePending; Session_mac onInterruptStop.
 *  - exception:    Callbacks.cpp onBreakpoint and onUnknownException (second chance);
 *                  Session_mac onSignalStop and onExceptionStop.
 */
enum class StopReason
{
    none,
    initialBreak,
    moduleLoad,
    breakpoint,
    step,
    pause,
    exception
};

/** Outcome of a Session::getOffsetStatus call.
 *
 *  - found      — symbol found; Session::getOffset returns the resolved address.
 *  - notFound   — symbol not resolvable (no code at this line, or module not loaded).
 *  - engineBusy — Windows only: dbgeng transient error (E_UNEXPECTED); caller should
 *                 retry later. liblldb resolves synchronously and never returns this.
 */
enum class OffsetStatus
{
    found,      ///< Symbol found; Session::getOffset returns the resolved address.
    notFound,   ///< Symbol not resolvable (no code at this line, or module not loaded).
    engineBusy  ///< Windows only: dbgeng transient error (E_UNEXPECTED); caller should
                ///< retry later. liblldb resolves synchronously and never returns this.
};

/** Single Source of Truth for all mutable debug session state.
 *
 *  State is the SSOT shared between Session, BreakpointManager, and Whatdbg.
 *  It is registered as a jam::Instance so that any subsystem can resolve it
 *  without an explicit pointer argument.
 *
 *  Fields are grouped by concern. The engine event paths write the stop cause into
 *  stopReason and its payload fields: Callbacks.cpp on Windows, Session::pollEvents on
 *  Windows for steps, and Session_mac.cpp on macOS. Whatdbg::processDeferredEvents
 *  reads stopReason and runs the drain for it. All access must occur on the main
 *  thread unless a field is explicitly noted otherwise.
 *
 *  @note Declared first in Whatdbg so that jam::Instance registration happens before
 *        any dependent object is constructed.
 */
class State : public jam::Instance<State>
{
public:
    State () = default;

    // ── Execution ──────────────────────────────────────────────────────

    /** Current execution lifecycle state of the debug target.
     *
     *  Set by the Whatdbg command handlers and drains, and by the engine event paths:
     *  the Callbacks.cpp and Session_mac.cpp stop and exit handlers, and Session::pollEvents
     *  on Windows for E_UNEXPECTED. Read by Whatdbg::run to choose between polling the
     *  engine and sleeping, by Whatdbg::onDisconnect, and by Whatdbg::getEndModeForExit.
     */
    ExecutionState executionState { ExecutionState::idle };

    /** Cause of the current stop. See StopReason for the writers of each value.
     *
     *  Read by Whatdbg::processDeferredEvents. A drain resets it to none.
     */
    StopReason stopReason { StopReason::none };

    /** Exit code of the debuggee process.
     *
     *  Set by EventCallbacks::ExitProcess (Windows) or onProcessStateExited (macOS).
     *  Read by Whatdbg::drainProcessExited for the DAP exited event body.
     */
    int processExitCode { 0 };

    /** OS process ID of the debug target.
     *
     *  Attach (both platforms): set by Whatdbg::onAttach after Session::attach
     *  succeeds — the sole writer for the attach path on both platforms.
     *  Launch: set by Session_mac::launch directly (macOS), or by
     *  EventCallbacks::CreateProcess (Windows, asynchronous). Used by
     *  Session::interrupt / Session::terminateDebuggee.
     */
    std::uint32_t targetProcessId { 0 };

    /** Program path from the launch request; empty for attach sessions.
     *
     *  Set by Whatdbg::onLaunch after Session::launch succeeds. Read by
     *  Whatdbg::resumeAfterInitialBreak, where non-empty selects the
     *  launch-path process event; attach sessions announce theirs from
     *  Whatdbg::onAttach instead.
     */
    juce::String targetProgram;

    /** juce::Time::getMillisecondCounter() deadline for the debuggee's exit event
     *  after Whatdbg::onDisconnect issues a terminate. 0 means no deadline is pending.
     *
     *  Set by Whatdbg::onDisconnect. Consumed by Whatdbg::drainTerminateTimeout, which
     *  forces run() to exit if the exit event never arrives. Cleared once the
     *  debuggee's exit is observed or the deadline is enforced.
     */
    std::uint32_t terminateDeadlineMs { 0 };

    // ── Control flags ─────────────────────────────────────────────────

    /** True while the main loop should continue running.
     *
     *  Set to false by Whatdbg::onDisconnect (also registered for the "terminate"
     *  command) to exit run(), by Whatdbg::drainProcessExited once the debuggee's
     *  exit has been observed, or by Whatdbg::drainTerminateTimeout when the deadline passes.
     */
    bool isRunning               { true };

    /** When true, the debuggee process will be terminated on disconnect.
     *
     *  Controlled by the terminateDebuggee field of the DAP disconnect request.
     */
    bool shouldTerminateOnExit   { false };

    /** Set to true once the client sends configurationDone.
     *
     *  The initial break is only released after configuration is complete.
     */
    bool isConfigurationDone { false };

    /** Set to true between a step request and the corresponding step-complete event.
     *
     *  Set by Whatdbg::onNext, onStepIn, and onStepOut, and cleared by Session::pollEvents
     *  on Windows when a wait returns S_OK. Read by Session::pollEvents on Windows and by
     *  Callbacks.cpp LoadModule, which does not stop for a module load while a step is pending.
     */
    bool isStepPending  { false };

    /** Set to true between a pause request and the corresponding pause-complete event.
     *
     *  Set by Whatdbg::onPause. Consumed by Callbacks.cpp onBreakpoint (Windows) and by
     *  Session_mac onStopReason (macOS) to classify the next stop as a pause, since a
     *  user pause on some liblldb builds reports eStopReasonBreakpoint rather than
     *  eStopReasonInterrupt. Cleared once the stop has been classified, and by Whatdbg::onContinue.
     */
    bool isPausePending { false };

    // ── Deferred events (written by the engine event paths, read by Whatdbg) ──

    /** Engine breakpoint ID that caused the most recent breakpoint stop.
     *
     *  Set with stopReason breakpoint by Callbacks.cpp Breakpoint and Session_mac
     *  onBreakpointStop. Read by Whatdbg::drainBreakpointHit and passed to
     *  BreakpointManager::getBreakpointStoppedEvent to build hitBreakpointIds.
     */
    std::int32_t breakpointEngineId { 0 };

    /** Image file name (full path) of the most recently loaded module.
     *
     *  Set by Callbacks.cpp LoadModule when no step is pending. Read by
     *  Whatdbg::drainModuleLoaded and passed to BreakpointManager::onModuleLoad, which
     *  reloads that module's symbols when a breakpoint is unresolved.
     */
    juce::String lastLoadedImageName;

    /** Set by Session_mac onBreakpointEvent when liblldb resolves a new
     *  breakpoint location (typically because a matching module loaded after
     *  the BP was created). Consumed by processDeferredEvents to emit a DAP
     *  `breakpoint` event with reason=`changed` so the client gutter marker
     *  flips from unverified to verified.
     */
    bool hasBreakpointLocationsResolved { false };

    /** Engine breakpoint ID associated with the most recent resolution event.
     *  Set alongside `hasBreakpointLocationsResolved`. Caller passes this to
     *  BreakpointManager::onBreakpointLocationFound, which scans the `breakpoints`
     *  registry for the matching entryInfo.engineId and returns its DAP breakpoint ID.
     */
    std::int32_t resolvedBreakpointEngineId { 0 };

    /** Resolved source line for the BP whose location was just added.
     *  Set alongside `hasBreakpointLocationsResolved`. 0 if unavailable
     *  (shouldn't happen in practice — LocationsResolved implies a line entry).
     */
    std::uint32_t resolvedBreakpointLine { 0 };

    /** Set when the target process exits: by Callbacks.cpp ExitProcess (Windows),
     *  by Session::pollEvents when WaitForEvent returns E_UNEXPECTED (Windows), or by
     *  Session_mac onProcessStateExited (macOS).
     *
     *  Consumed by Whatdbg::drainProcessExited, which emits DAP exited and terminated
     *  events and transitions executionState to exited. Cleared after consumption.
     */
    bool hasProcessExited { false };

    /** Platform-native exception/signal code.
     *
     *  Windows: NTSTATUS / SEH exception code (e.g., 0xC0000005 for ACCESS_VIOLATION).
     *           Written by Callbacks.cpp onUnknownException on a second-chance exception,
     *           and by onBreakpoint for an int3 that is neither the initial break nor a pause.
     *  macOS:   POSIX signal number from SBThread::GetStopReasonDataAtIndex(0) when
     *           stop reason is eStopReasonSignal; Mach exception type when eStopReasonException.
     *           Written by Session_mac onSignalStop and onExceptionStop.
     *
     *  Written with stopReason exception. Persists after consumption so that
     *  Whatdbg::onExceptionInfo can respond with the exception details.
     */
    std::uint32_t exceptionCode { 0 };

    /** Virtual address at which the exception occurred.
     *
     *  Written with exceptionCode, under stopReason exception. Persists after
     *  consumption for Whatdbg::onExceptionInfo's response.
     */
    std::uint64_t exceptionAddress { 0 };

    /** Discriminator for how `exceptionCode` should be interpreted on macOS.
     *
     *  `false` (default, and always-false on Windows): `exceptionCode` is a
     *  POSIX signal number (SIGSEGV, SIGBUS, …) resolved via `signalNames`.
     *
     *  `true` (macOS only): `exceptionCode` is a Mach exception type
     *  (EXC_BAD_ACCESS = 1, EXC_BAD_INSTRUCTION = 2, …) resolved via
     *  `machExceptionNames`. Set by `onExceptionStop` when LLDB reports
     *  `eStopReasonException` instead of `eStopReasonSignal`.
     */
    bool isMachException { false };

    /** Set by OutputCallbacks::Output2 (Windows OutputDebugString) or
     *  drainProcessStdio (macOS stdout/stderr).
     *
     *  Consumed by processDeferredEvents to emit a DAP output event using
     *  debuggeeOutputCategory. Cleared after consumption.
     */
    bool hasDebuggeeOutput { false };

    /** Text captured from the target via OutputDebugString (Windows) or
     *  SBProcess::GetSTDOUT / GetSTDERR (macOS).
     *
     *  Set alongside hasDebuggeeOutput. Forwarded verbatim in the DAP output event body.
     */
    juce::String debuggeeOutputText;

    /** DAP output event category for debuggeeOutputText — "console" on Windows
     *  (OutputDebugString has no stream distinction), "stdout" or "stderr" on
     *  macOS depending on which stream drainProcessStdio last read.
     *
     *  Set alongside hasDebuggeeOutput.
     */
    juce::String debuggeeOutputCategory { "console" };

    // ── Variable inspection ───────────────────────────────────────────

    /** Monotonically increasing counter used to generate unique variablesReference values.
     *
     *  Reset to 1 on each stop event via resetVariablesState().
     */
    int nextVariablesRef { 1 };

    /** Maps variablesReference -> (frameIndex, symbolIndex).
     *
     *  A symbolIndex of -1 means the entry represents the top-level locals scope for
     *  the given frame. A non-negative symbolIndex identifies an expanded child variable
     *  within the symbol group for that frame.
     */
    std::unordered_map<int, std::pair<int, int>> variablesRefMap;

    // ── Frame ID mapping (threadId + frameIndex) ──────────────────────

    /** Monotonically increasing counter used to generate unique frameId values.
     *
     *  Reset to 1 on each stop event via resetVariablesState().
     */
    int nextFrameId { 1 };

    /** Maps frameId -> (threadSystemId, frameIndex).
     *
     *  Populated by onStackTrace. Read by Whatdbg::selectFrameFromId, which onScopes
     *  and onEvaluate call to restore the thread and frame context before a query.
     */
    std::unordered_map<int, std::pair<std::uint32_t, int>> frameIdMap;

    /** System thread ID of the thread whose scopes were last requested.
     *
     *  Cached to avoid redundant setCurrentThreadBySystemId calls between
     *  consecutive scopes requests targeting the same thread.
     */
    std::uint32_t lastScopesThreadId { 0 };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (State)
};

/** Translate a platform-native exception/signal code to a human-readable name.
 *
 *  Windows: code is an NTSTATUS / SEH exception code (e.g., 0xC0000005 → "ACCESS_VIOLATION").
 *           isMachException is always false.
 *  macOS:   code is a POSIX signal number when isMachException is false, or a
 *           Mach exception type when isMachException is true (e.g., 11 → "EXC_BAD_ACCESS").
 *
 *  Implementations live in platform-specific files: Callbacks.cpp on Windows,
 *  Session_mac.cpp on macOS. Used by Whatdbg::drainExceptionStopped and
 *  Whatdbg::onExceptionInfo to build the DAP exceptionId field.
 *
 *  @param code             Platform-native exception/signal code from State::exceptionCode.
 *  @param isMachException  State::isMachException — selects the Mach exception table on macOS.
 *  @return Human-readable name, or "0x<hex>" fallback if the code is not known.
 */
juce::String getExceptionName (std::uint32_t code, bool isMachException) noexcept;

} // namespace debug
