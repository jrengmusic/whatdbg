#pragma once
#include <JuceHeader.h>
#include "debug/State.h"
#include "debug/Session.h"
#if JUCE_WINDOWS
#include "debug/Callbacks.h"
#include "debug/Loader.h"
#endif
#include "debug/BreakpointManager.h"
#include "dap/Reader.h"
#include "dap/Types.h"

/** Top-level application object that owns the DAP server event loop.
 *
 *  Whatdbg connects a DAP client (e.g. nvim-dap) to a debug session — dbgeng on
 *  Windows, liblldb on macOS. It reads DAP messages from stdin via dap::Reader,
 *  dispatches them to the appropriate handler, drives the debug engine's event
 *  loop, and writes DAP responses and events to stdout.
 *
 *  Ownership hierarchy:
 *  - debug::State       — SSOT shared by Session and BreakpointManager via jam::Instance.
 *  - debug::Session     — cross-platform wrapper around the debug engine (dbgeng COM
 *                          interfaces on Windows, liblldb SB API on macOS).
 *  - debug::BreakpointManager — Tracks DAP breakpoints and resolves them to engine IDs.
 *  - dap::Reader        — Background thread that parses stdin and enqueues messages.
 *
 *  @note All public methods must be called on the main thread.
 */
class Whatdbg
{
public:
    /** Maximum time to wait for the debuggee's exit event after a terminate request
     *  before drainTerminateTimeout gives up and forces run() to exit.
     */
    static constexpr std::uint32_t terminateTimeoutMs { 5000 };

    Whatdbg ();

    /** Initialize the debug engine sidecar.
     *
     *  Must be called once on the main thread before run(). Delegates to
     *  debug::Session::initialize, which on Windows loads dbgeng.dll from the
     *  sidecar directory and acquires all required COM interfaces, and on macOS
     *  creates the liblldb SBDebugger.
     *
     *  @param sidecarDir  Directory containing the extracted debug engine sidecar.
     *  @return true if the debug engine initialized successfully.
     *
     *  @note Must be called on the main thread before any other method.
     */
    bool initialize (const juce::File& sidecarDir) noexcept;

    /** Run the main DAP event loop.
     *
     *  Blocks until the client sends a disconnect or terminate request, or the
     *  debuggee process exits. In each iteration the loop pops DAP messages from
     *  the Reader FIFO, dispatches them, polls the debug engine for events, and
     *  forwards deferred callback events as DAP events to stdout.
     *
     *  @note Blocks the calling thread (expected to be called from main()).
     */
    void run ();

private:
    // ── DAP command dispatch ───────────────────────────────────────────

    /** Signature of a DAP command handler bound to a specific `on*` method. */
    using Command = std::function<void (const juce::var&)>;

    /** Dispatch table mapping a DAP command name to its bound handler.
     *
     *  Populated once in the constructor with one entry per supported DAP
     *  request ("initialize", "launch", "setBreakpoints", ...); "terminate"
     *  shares its entry with "disconnect". Looked up by onCommand.
     */
    std::unordered_map<std::string, Command> commands;

    /** Dispatch a parsed DAP message to the registered handler.
     *
     *  @param message  Parsed JSON message as juce::var.
     */
    void onCommand (const juce::var& message);

    /** Handle DAP initialize request — send capabilities response.
     *
     *  @param request  The full DAP initialize request object.
     */
    void onInitialize (const juce::var& request);

    /** Handle DAP launch request — launch the debuggee process.
     *
     *  @param request  The full DAP launch request object containing the program path.
     */
    void onLaunch (const juce::var& request);

    /** Handle DAP attach request — attach to a running process by PID.
     *
     *  @param request  The full DAP attach request object containing the process ID.
     */
    void onAttach (const juce::var& request);

    /** Configure symbol and source search paths for the newly launched/attached target.
     *
     *  @param cwd  The debuggee's working directory from the DAP launch/attach
     *              arguments, or empty if the client did not supply one.
     */
    void addSearchPaths (const juce::String& cwd);

    /** Handle DAP configurationDone request — release the initial break and begin execution.
     *
     *  @param request  The full DAP configurationDone request object.
     */
    void onConfigurationDone (const juce::var& request);

    /** Handle DAP disconnect request — detach or terminate the debuggee.
     *
     *  @param request  The full DAP disconnect request object.
     */
    void onDisconnect (const juce::var& request);

    /** Handle DAP setBreakpoints request — sync client breakpoints with dbgeng.
     *
     *  @param request  The full DAP setBreakpoints request containing source and breakpoints array.
     */
    void onSetBreakpoints (const juce::var& request);

    /** Handle DAP threads request — enumerate all threads in the debuggee.
     *
     *  @param request  The full DAP threads request object.
     */
    void onThreads (const juce::var& request);

    /** Handle DAP stackTrace request — return call stack frames for a thread.
     *
     *  @param request  The full DAP stackTrace request containing threadId and optional startFrame/levels.
     */
    void onStackTrace (const juce::var& request);

    /** Decode a DAP frameId to (threadSystemId, frameIndex) and select that thread.
     *
     *  Shared by onScopes and onEvaluate — both must restore the correct
     *  thread/frame context from a client-supplied frameId before querying Session.
     *
     *  @param frameId  The DAP frameId, as previously assigned by onStackTrace.
     *  @return the resolved (threadSystemId, frameIndex) pair, or (0, frameId)
     *          unchanged if frameId is not in state.frameIdMap.
     */
    std::pair<std::uint32_t, int> selectFrameFromId (int frameId);

    /** Handle DAP scopes request — return variable scopes for a stack frame.
     *
     *  @param request  The full DAP scopes request containing frameId.
     */
    void onScopes (const juce::var& request);

    /** Resolve a variablesReference to its formatted DAP variable list.
     *
     *  @param ref  A variablesReference previously registered in state.variablesRefMap
     *              by onScopes (locals scope) or addDapVariable (expandable child).
     *  @return the formatted DAP variables, or an empty array if ref is not registered.
     */
    juce::Array<juce::var> getDapVariables (int ref);

    /** Handle DAP variables request — return variables for a variablesReference.
     *
     *  @param request  The full DAP variables request containing variablesReference.
     */
    void onVariables (const juce::var& request);

    /** Format one raw Session variable and add it to the DAP variables array.
     *
     *  @param dapVariables  The DAP variables array being built; appended to in place.
     *  @param rawVar        The raw variable object returned by Session::getLocals
     *                       or Session::getVariableChildren.
     *  @param frameIndex    Stack frame index the variable belongs to, used to key
     *                       any child variablesReference registered for expansion.
     */
    void addDapVariable (juce::Array<juce::var>& dapVariables, const juce::var& rawVar, int frameIndex);

    /** Handle DAP continue request — resume execution.
     *
     *  @param request  The full DAP continue request object.
     */
    void onContinue (const juce::var& request);

    /** Handle DAP next request — step over one source line.
     *
     *  @param request  The full DAP next request object.
     */
    void onNext (const juce::var& request);

    /** Handle DAP stepIn request — step into a function call.
     *
     *  @param request  The full DAP stepIn request object.
     */
    void onStepIn (const juce::var& request);

    /** Handle DAP stepOut request — step out of the current function.
     *
     *  @param request  The full DAP stepOut request object.
     */
    void onStepOut (const juce::var& request);

    /** Handle DAP pause request — interrupt a running debuggee.
     *
     *  @param request  The full DAP pause request object.
     */
    void onPause (const juce::var& request);

    /** Handle DAP evaluate request — evaluate a C++ expression in the stopped context.
     *
     *  @param request  The full DAP evaluate request containing expression, frameId, and context.
     */
    void onEvaluate (const juce::var& request);

    /** Handle DAP exceptionInfo request — return crash details for the last unhandled exception.
     *
     *  @param request  The full DAP exceptionInfo request object.
     */
    void onExceptionInfo (const juce::var& request);

    // ── Deferred event processing ──────────────────────────────────────

    /** Drain deferred events set by the debug engine and emit corresponding DAP events.
     *
     *  COM callbacks (Windows EventCallbacks) or Session_mac's event handlers (macOS)
     *  write flags into debug::State. This method reads those flags on the main thread
     *  and sends DAP stopped, output, exited, and terminated events as appropriate.
     *  Called once per main loop iteration.
     */
    void processDeferredEvents ();

    /** Resolve pending breakpoints and resume once the initial break is fully configured. */
    void drainInitialBreak ();

    /** Emit a DAP stopped(breakpoint) event for the pending breakpoint hit. */
    void emitBreakpointStoppedEvent ();

    /** Emit a DAP stopped(breakpoint) event if a user breakpoint hit is pending.
     *
     *  @return true if state.hasBreakpointHit was consumed as a real breakpoint stop.
     *          false if no breakpoint was pending, or the hit was an internal
     *          stepOut breakpoint converted into a step completion.
     */
    bool drainBreakpointHit ();

    /** Emit a DAP stopped(step) event if a step completion is pending.
     *
     *  @return true if a stopped event was emitted.
     */
    bool drainStepCompleted ();

    /** Emit a DAP stopped(pause) event if a pause completion is pending. */
    void drainPauseCompleted ();

    /** Resolve pending breakpoints against a newly loaded module and resume if needed. */
    void drainModuleLoaded ();

    /** Build a DAP breakpoint(changed) event body for an asynchronously resolved location.
     *
     *  @param dapId         The DAP breakpoint ID resolved by BreakpointManager.
     *  @param resolvedLine  The source line the breakpoint resolved to.
     */
    juce::var getBreakpointChangedEvent (int dapId, std::uint32_t resolvedLine);

    /** Emit a DAP breakpoint(changed) event if a breakpoint location was resolved asynchronously. */
    void drainBreakpointLocationResolved ();

    /** Emit a DAP output event if the debuggee has produced console output. */
    void drainDebuggeeOutput ();

    /** Build a DAP stopped(exception) event body from a formatted crash description.
     *
     *  @param exceptionName  Human-readable exception/signal name from debug::getExceptionName.
     *  @param description    Formatted "0x<code> at 0x<address>" crash description.
     *  @param threadId       System thread ID that crashed.
     */
    juce::var getExceptionStoppedEvent (const juce::String& exceptionName,
                                        const juce::String& description,
                                        int threadId);

    /** Build a DAP output(stderr) event body summarizing an unhandled exception.
     *
     *  @param exceptionName  Human-readable exception/signal name from debug::getExceptionName.
     *  @param description    Formatted "0x<code> at 0x<address>" crash description.
     */
    juce::var getExceptionOutputEvent (const juce::String& exceptionName,
                                       const juce::String& description);

    /** Emit DAP stopped(exception) and output events if an unhandled exception fired. */
    void drainExceptionStopped ();

    /** Emit DAP exited and terminated events if the debuggee process has exited. */
    void drainProcessExited ();

    /** Force run() to exit if the debuggee's exit event never arrives after terminate.
     *
     *  Reads state.terminateDeadlineMs, set by onDisconnect. If the deadline has
     *  passed and the debuggee has still not exited, logs a diagnostic and sets
     *  state.isRunning to false so run() exits deterministically instead of hanging.
     */
    void drainTerminateTimeout ();

    /** Tell the Session to continue and mark executionState running.
     *
     *  Shared by onContinue, resumeAfterInitialBreak, and drainModuleLoaded's
     *  re-resume after a paused module load.
     */
    void resumeExecution ();

    /** Forward any DAP events produced by resolving pending breakpoints.
     *
     *  Shared by resumeAfterInitialBreak and drainModuleLoaded — both call
     *  BreakpointManager::onModuleLoad after their own platform-specific symbol
     *  reload, then must forward the resulting breakpoint-resolution events
     *  identically.
     */
    void emitResolvedBreakpointEvents ();

    /** Resolve any pending breakpoints and resume the debuggee after the initial break.
     *
     *  At the initial break the exe module is fully loaded and the symbol engine
     *  is ready. This resolves standalone breakpoints that went pending because
     *  the exe loaded before setBreakpoints arrived. Plugin DLLs resolve later
     *  via the LoadModule path. Called from both onConfigurationDone and
     *  drainInitialBreak depending on message arrival order.
     */
    void resumeAfterInitialBreak ();

    /** Decide how Session::shutdown should end the debug session at run() exit.
     *
     *  @return EndMode::passive if the debuggee already exited, EndMode::terminate
     *          if the client requested termination, otherwise EndMode::detach.
     */
    debug::EndMode getEndModeForExit () const noexcept;

    // ── stdout writing ─────────────────────────────────────────────────

    /** Serialize a DAP message to JSON and write it to stdout with a Content-Length header.
     *
     *  @param message  The DAP message as a juce::var (must be a DynamicObject).
     *
     *  @note Not thread-safe — must be called on the main thread only.
     */
    void writeMessage (const juce::var& message) noexcept;

    // ── Owned objects ──────────────────────────────────────────────────

    /** Single Source of Truth for all mutable debug session state.
     *
     *  Declared first so that its jam::Instance registration happens before
     *  session, breakpointManager, or reader are constructed.
     */
    debug::State state;

    /** Cross-platform wrapper around the debug engine (dbgeng on Windows,
     *  liblldb on macOS).
     */
    debug::Session session;

    /** Tracks DAP breakpoints and resolves them against session. */
    debug::BreakpointManager breakpointManager;

    /** Background stdin thread that parses DAP messages into the main-thread FIFO. */
    dap::Reader reader;

    /** Reset all variable inspection state for a new stop event.
     *
     *  Clears variablesRefMap, frameIdMap, resets nextVariablesRef and nextFrameId
     *  to 1, and zeroes lastScopesThreadId. Called at the start of every stopped event.
     */
    void resetVariablesState () noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Whatdbg)
};
