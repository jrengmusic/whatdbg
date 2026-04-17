#pragma once
#include <JuceHeader.h>
#include <cstdint>

namespace debug
{

/** Execution lifecycle states for the debug target.
 *
 *  Represents the coarse state machine of a debug session as seen by the DAP layer.
 *  Transitions are driven by Session and surfaced to Whatdbg via processDeferredEvents.
 */
enum class ExecutionState : int
{
    idle      = 0,  ///< No target attached. Initial state.
    launching = 1,  ///< launch or attach request sent; waiting for first CreateProcess event.
    running   = 2,  ///< Target is executing. Resume/step have been issued.
    stopped   = 3,  ///< Target is paused at a breakpoint, step completion, or exception.
    exited    = 4   ///< Target process has exited. Session is shutting down.
};

/** Lifecycle phase of the dbgeng loader/initial breakpoint.
 *
 *  Three-state machine, transitioned by EventCallbacks::Breakpoint and consumed
 *  by processDeferredEvents/resolveAndResumeAfterInitialBreak.
 *
 *  - notHit    — dbgeng has not yet raised the initial loader breakpoint.
 *  - pending   — loader breakpoint fired; resolve+resume not yet run.
 *  - resolved  — initial break consumed; subsequent breakpoints are user BPs.
 */
enum class InitialBreakPhase
{
    notHit,
    pending,
    resolved
};

/** Outcome of a Session::getOffsetByLine call.
 *
 *  Replaces the previous juce::Result + hex-string encoding pattern so that
 *  callers can branch on a typed enum rather than inspecting error message text.
 *
 *  - resolved   — symbol found; *outOffset has been populated.
 *  - notFound   — symbol not resolvable (no code at this line, or module not loaded).
 *  - engineBusy — engine transient error (E_UNEXPECTED); caller should retry later.
 */
enum class ResolveStatus
{
    resolved,
    notFound,
    engineBusy
};

/** Single Source of Truth for all mutable debug session state.
 *
 *  State is the SSOT shared between Session, BreakpointManager, and Whatdbg.
 *  It is registered as a jreng::Context so that any subsystem can resolve it
 *  without an explicit pointer argument.
 *
 *  Fields are grouped by concern. COM callbacks (EventCallbacks) write deferred-event
 *  flags; the Whatdbg main loop reads and clears them in processDeferredEvents().
 *  All access must occur on the main thread unless a field is explicitly noted otherwise.
 *
 *  @note Declared first in Whatdbg so that jreng::Context registration happens before
 *        any dependent object is constructed.
 */
class State : public jreng::Context<State>
{
public:
    State () = default;

    // ── Execution ──────────────────────────────────────────────────────

    /** Current execution lifecycle state of the debug target.
     *
     *  Set by Whatdbg command handlers and by processDeferredEvents in response
     *  to deferred callback flags. Read by handleContinue, handleNext, and others
     *  to guard against invalid operations in the wrong state.
     */
    ExecutionState executionState { ExecutionState::idle };

    /** Lifecycle phase of the dbgeng initial (loader) breakpoint.
     *
     *  Transitions: notHit → pending (loader BP fires) → resolved (processDeferredEvents
     *  runs resolveAndResumeAfterInitialBreak). Replaces former paired booleans
     *  `isInitialBreakSeen` + `isInitialBreakHandled`.
     */
    InitialBreakPhase initialBreakPhase { InitialBreakPhase::notHit };

    /** Exit code of the debuggee process.
     *
     *  Set by EventCallbacks::ExitProcess. Read by processDeferredEvents to include
     *  in the DAP exited event body.
     */
    int processExitCode { 0 };

    /** OS process ID of the debug target.
     *
     *  Set by Whatdbg::handleLaunch / handleAttach after Session::launch or
     *  Session::attach succeeds. Used by Session::interrupt to send DebugBreakProcess.
     */
    std::uint32_t targetProcessId { 0 };

    // ── Deferred events (set by COM callbacks, consumed by main loop) ──

    /** Set by EventCallbacks::Breakpoint when a user-registered breakpoint is hit.
     *
     *  Consumed by processDeferredEvents to emit a DAP stopped event with
     *  reason "breakpoint". Cleared after consumption.
     */
    bool  hasBreakpointHit   { false };

    /** Engine breakpoint ID that caused the most recent breakpoint stop.
     *
     *  Set alongside hasBreakpointHit. Passed to BreakpointManager::onBreakpointHit
     *  to produce the correct hitBreakpointIds array in the DAP stopped body.
     */
    std::uint32_t breakpointEngineId { 0 };

    /** Set by EventCallbacks when a step operation has completed.
     *
     *  Consumed by processDeferredEvents to emit a DAP stopped event with
     *  reason "step". Cleared after consumption.
     */
    bool hasStepCompleted { false };

    /** Set by EventCallbacks::LoadModule when a new module is loaded into the target.
     *
     *  Consumed by processDeferredEvents to trigger BreakpointManager::onModuleLoad
     *  so that pending breakpoints can be resolved against the new module's symbols.
     *  Cleared after consumption.
     */
    bool hasNewModuleLoaded { false };

    /** Display name of the most recently loaded module.
     *
     *  Set alongside hasNewModuleLoaded. Used for logging and for deciding whether
     *  per-module symbol reload is needed.
     */
    juce::String lastLoadedModuleName;

    /** Image file name (full path) of the most recently loaded module.
     *
     *  Set alongside hasNewModuleLoaded. Passed to Session::loadModuleSymbols so
     *  dbgeng reloads PDB symbols for that specific DLL.
     */
    juce::String lastLoadedImageName;

    /** Set by Session_mac handleBreakpointEvent when liblldb resolves a new
     *  breakpoint location (typically because a matching module loaded after
     *  the BP was created). Consumed by processDeferredEvents to emit a DAP
     *  `breakpoint` event with reason=`changed` so the client gutter marker
     *  flips from unverified to verified.
     */
    bool hasBreakpointLocationsResolved { false };

    /** Engine breakpoint ID associated with the most recent resolution event.
     *  Set alongside `hasBreakpointLocationsResolved`. Caller resolves the
     *  DAP breakpoint ID via `engineToDap` in BreakpointManager.
     */
    std::uint32_t resolvedBreakpointEngineId { 0 };

    /** Resolved source line for the BP whose location was just added.
     *  Set alongside `hasBreakpointLocationsResolved`. 0 if unavailable
     *  (shouldn't happen in practice — LocationsResolved implies a line entry).
     */
    std::uint32_t resolvedBreakpointLine { 0 };

    /** True when at least one breakpoint is waiting to be resolved.
     *
     *  Set by BreakpointManager::handleSetBreakpoints when a breakpoint cannot be
     *  resolved immediately (symbols not yet loaded). Read by EventCallbacks::LoadModule
     *  to decide whether to set hasNewModuleLoaded and trigger resolution.
     */
    bool hasPendingBreakpoints { false };

    /** Set by EventCallbacks::ExitProcess when the target process exits.
     *
     *  Consumed by processDeferredEvents to emit DAP exited and terminated events
     *  and transition executionState to exited. Cleared after consumption.
     */
    bool hasProcessExited { false };

    /** Set by handleUnknownException when a second-chance unhandled exception fires.
     *
     *  Consumed by processDeferredEvents to emit a DAP stopped event with
     *  reason "exception" and an output event with the crash summary.
     *  Cleared after consumption.
     */
    bool hasExceptionStopped { false };

    /** Platform-native exception/signal code.
     *
     *  Windows: NTSTATUS / SEH exception code (e.g., 0xC0000005 for ACCESS_VIOLATION),
     *           captured by handleUnknownException on 2nd-chance.
     *  macOS:   POSIX signal number from SBThread::GetStopReasonDataAtIndex(0) when
     *           stop reason is eStopReasonSignal; Mach exception type when eStopReasonException.
     *
     *  Set alongside hasExceptionStopped. Persists after consumption so that
     *  handleExceptionInfo can respond with the exception details.
     */
    std::uint32_t exceptionCode { 0 };

    /** Virtual address at which the exception occurred.
     *
     *  Set alongside hasExceptionStopped. Persists after consumption for
     *  handleExceptionInfo response.
     */
    std::uint64_t exceptionAddress { 0 };

    /** Discriminator for how `exceptionCode` should be interpreted on macOS.
     *
     *  `false` (default, and always-false on Windows): `exceptionCode` is a
     *  POSIX signal number (SIGSEGV, SIGBUS, …) resolved via `signalNames`.
     *
     *  `true` (macOS only): `exceptionCode` is a Mach exception type
     *  (EXC_BAD_ACCESS = 1, EXC_BAD_INSTRUCTION = 2, …) resolved via
     *  `machExceptionNames`. Set by `handleExceptionStop` when LLDB reports
     *  `eStopReasonException` instead of `eStopReasonSignal`.
     */
    bool isMachException { false };

    /** Set by OutputCallbacks::Output2 when the target writes to OutputDebugString.
     *
     *  Consumed by processDeferredEvents to emit a DAP output event with
     *  category "stdout". Cleared after consumption.
     */
    bool hasDebuggeeOutput { false };

    /** Text captured from the target via OutputDebugString.
     *
     *  Set alongside hasDebuggeeOutput. Forwarded verbatim in the DAP output event body.
     */
    juce::String debuggeeOutputText;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (State)
};

/** Translate a platform-native exception/signal code to a human-readable name.
 *
 *  Windows: code is an NTSTATUS / SEH exception code (e.g., 0xC0000005 → "ACCESS_VIOLATION").
 *  macOS:   code is a POSIX signal number / Mach exception mapping (e.g., 11 → "EXC_BAD_ACCESS").
 *
 *  Implementations live in platform-specific files: Callbacks.cpp on Windows,
 *  Session_mac.cpp on macOS. Used by Whatdbg::processDeferredEvents and
 *  handleExceptionInfo to build the DAP exceptionId field.
 *
 *  @param code  Platform-native exception/signal code from State::exceptionCode.
 *  @return Human-readable name, or "0x<hex>" fallback if the code is not known.
 */
juce::String getExceptionName (std::uint32_t code) noexcept;

} // namespace debug
