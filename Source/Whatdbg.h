#pragma once
#include <JuceHeader.h>
#include <cstdint>
#include <functional>
#include <unordered_map>
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
 *  Whatdbg connects a DAP client (e.g. nvim-dap) to a Windows dbgeng debug
 *  session. It reads DAP messages from stdin via dap::Reader, dispatches them
 *  to the appropriate handler, drives the dbgeng event loop, and writes DAP
 *  responses and events to stdout.
 *
 *  Ownership hierarchy:
 *  - debug::State       — SSOT shared by Session and BreakpointManager via jam::Context.
 *  - debug::Session     — COM wrapper around dbgeng interfaces.
 *  - debug::BreakpointManager — Tracks DAP breakpoints and resolves them to engine IDs.
 *  - dap::Reader        — Background thread that parses stdin and enqueues messages.
 *
 *  @note All public methods must be called on the main thread.
 */
class Whatdbg
{
public:
    Whatdbg ();

    /** Initialize COM and dbgeng sidecar DLLs.
     *
     *  Must be called once on the main thread before run(). Delegates to
     *  debug::Session::initialize which loads dbgeng.dll from the sidecar
     *  directory and acquires all required COM interfaces.
     *
     *  @param sidecarDir  Directory containing the extracted dbgeng sidecar DLLs.
     *  @return true if all COM interfaces initialized successfully.
     *
     *  @note Must be called on the main thread before any other method.
     */
    bool initialize (const juce::File& sidecarDir) noexcept;

    /** Run the main DAP event loop.
     *
     *  Blocks until the client sends a disconnect or terminate request, or the
     *  debuggee process exits. In each iteration the loop pops DAP messages from
     *  the Reader FIFO, dispatches them, polls dbgeng for debug events, and
     *  forwards deferred callback events as DAP events to stdout.
     *
     *  @note Blocks the calling thread (expected to be called from main()).
     */
    void run ();

private:
    // ── DAP command dispatch ───────────────────────────────────────────
    using CommandHandler = std::function<void (const juce::var&)>;
    std::unordered_map<std::string, CommandHandler> commandHandlers;

    /** Dispatch a parsed DAP message to the registered handler.
     *
     *  @param message  Parsed JSON message as juce::var.
     */
    void handleCommand (const juce::var& message);

    /** Handle DAP initialize request — send capabilities response.
     *
     *  @param request  The full DAP initialize request object.
     */
    void handleInitialize (const juce::var& request);

    /** Handle DAP launch request — launch the debuggee process.
     *
     *  @param request  The full DAP launch request object containing the program path.
     */
    void handleLaunch (const juce::var& request);

    /** Handle DAP attach request — attach to a running process by PID.
     *
     *  @param request  The full DAP attach request object containing the process ID.
     */
    void handleAttach (const juce::var& request);

    /** Handle DAP configurationDone request — release the initial break and begin execution.
     *
     *  @param request  The full DAP configurationDone request object.
     */
    void handleConfigurationDone (const juce::var& request);

    /** Handle DAP disconnect request — detach or terminate the debuggee.
     *
     *  @param request  The full DAP disconnect request object.
     */
    void handleDisconnect (const juce::var& request);

    /** Handle DAP setBreakpoints request — sync client breakpoints with dbgeng.
     *
     *  @param request  The full DAP setBreakpoints request containing source and breakpoints array.
     */
    void handleSetBreakpoints (const juce::var& request);

    /** Handle DAP threads request — enumerate all threads in the debuggee.
     *
     *  @param request  The full DAP threads request object.
     */
    void handleThreads (const juce::var& request);

    /** Handle DAP stackTrace request — return call stack frames for a thread.
     *
     *  @param request  The full DAP stackTrace request containing threadId and optional startFrame/levels.
     */
    void handleStackTrace (const juce::var& request);

    /** Handle DAP scopes request — return variable scopes for a stack frame.
     *
     *  @param request  The full DAP scopes request containing frameId.
     */
    void handleScopes (const juce::var& request);

    /** Handle DAP variables request — return variables for a variablesReference.
     *
     *  @param request  The full DAP variables request containing variablesReference.
     */
    void handleVariables (const juce::var& request);

    /** Handle DAP continue request — resume execution.
     *
     *  @param request  The full DAP continue request object.
     */
    void handleContinue (const juce::var& request);

    /** Handle DAP next request — step over one source line.
     *
     *  @param request  The full DAP next request object.
     */
    void handleNext (const juce::var& request);

    /** Handle DAP stepIn request — step into a function call.
     *
     *  @param request  The full DAP stepIn request object.
     */
    void handleStepIn (const juce::var& request);

    /** Handle DAP stepOut request — step out of the current function.
     *
     *  @param request  The full DAP stepOut request object.
     */
    void handleStepOut (const juce::var& request);

    /** Handle DAP pause request — interrupt a running debuggee.
     *
     *  @param request  The full DAP pause request object.
     */
    void handlePause (const juce::var& request);

    /** Handle DAP evaluate request — evaluate a C++ expression in the stopped context.
     *
     *  @param request  The full DAP evaluate request containing expression, frameId, and context.
     */
    void handleEvaluate (const juce::var& request);

    /** Handle DAP exceptionInfo request — return crash details for the last unhandled exception.
     *
     *  @param request  The full DAP exceptionInfo request object.
     */
    void handleExceptionInfo (const juce::var& request);

    // ── Deferred event processing ──────────────────────────────────────

    /** Drain deferred events set by COM callbacks and emit corresponding DAP events.
     *
     *  COM callbacks (EventCallbacks) write flags into debug::State. This method
     *  reads those flags on the main thread and sends DAP stopped, output, exited,
     *  and terminated events as appropriate. Called once per main loop iteration.
     */
    void processDeferredEvents ();

    /** Resolve any pending breakpoints and resume the debuggee after the initial break.
     *
     *  At the initial break the exe module is fully loaded and the symbol engine
     *  is ready. This resolves standalone breakpoints that went pending because
     *  the exe loaded before setBreakpoints arrived. Plugin DLLs resolve later
     *  via the LoadModule path. Called from both handleConfigurationDone and
     *  processDeferredEvents depending on message arrival order.
     */
    void resolveAndResumeAfterInitialBreak ();

    // ── stdout writing ─────────────────────────────────────────────────

    /** Serialize a DAP message to JSON and write it to stdout with a Content-Length header.
     *
     *  @param message  The DAP message as a juce::var (must be a DynamicObject).
     *
     *  @note Not thread-safe — must be called on the main thread only.
     */
    void writeMessage (const juce::var& message) noexcept;

    /** Serialize and send a DAP response to stdout.
     *
     *  @param response  A response object produced by dap::makeResponse or dap::makeErrorResponse.
     */
    void sendResponse (const juce::var& response) noexcept;

    /** Serialize and send a DAP event to stdout.
     *
     *  @param event  An event object produced by dap::makeEvent.
     */
    void sendEvent (const juce::var& event) noexcept;

    // ── Owned objects ──────────────────────────────────────────────────
    debug::State             state;              // SSOT — must be first (Context registration)
    debug::Session           session;            // COM wrapper
    debug::BreakpointManager breakpointManager;
    dap::Reader              reader;             // stdin thread + FIFO

    /** True while the main loop should continue running.
     *
     *  Set to false by handleDisconnect or handleTerminate to exit run().
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
     *  Prevents the main loop from issuing redundant resume calls while stepping.
     */
    bool isStepPending  { false };

    /** Set to true between a pause request and the target entering a stopped state.
     *
     *  Prevents duplicate interrupt calls if the client sends pause more than once.
     */
    bool isPausePending { false };

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
     *  Populated by handleStackTrace. Consumed by handleScopes and handleVariables
     *  to restore the correct thread/frame context before querying locals.
     */
    std::unordered_map<int, std::pair<std::uint32_t, int>> frameIdMap; // frameId → (threadSystemId, frameIndex)

    /** System thread ID of the thread whose scopes were last requested.
     *
     *  Cached to avoid redundant setCurrentThreadBySystemId calls between
     *  consecutive scopes requests targeting the same thread.
     */
    std::uint32_t lastScopesThreadId { 0 };

    /** Reset all variable inspection state for a new stop event.
     *
     *  Clears variablesRefMap, frameIdMap, resets nextVariablesRef and nextFrameId
     *  to 1, and zeroes lastScopesThreadId. Called at the start of every stopped event.
     */
    void resetVariablesState () noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Whatdbg)
};
