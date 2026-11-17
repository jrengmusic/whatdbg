#pragma once
#include <JuceHeader.h>
#include "debug/Session.h"
#include "debug/BreakpointManager.h"
#include "debug/VariableManager.h"
#include "debug/StopManager.h"
#include "dap/Types.h"

class Processor : private jam::AsyncUpdater
{
public:
    /** Registers the command, cancellation, engine event, launch channel, and output event handlers.
     *
     *  On Windows it adds setBreakpoints to the deferrals. It sets the stdin callback, which triggers
     *  the engine context and wakes the session. At the end of stdin, the callback stores Id::isInputClosed
     *  with release order, then notifies the engine context and the session.
     */
    Processor ();

    /** Calls releaseResources, which shuts the session down on the engine context. */
    ~Processor () override;

    /** Starts the adapter with the sidecar directory of the debug engine.
     *
     *  Writes the prepare request and waits until the engine context runs it. Opens stdio after that
     *  when isRunning is not 0. onPrepare clears isRunning when Session::initialize fails.
     *
     *  @param sidecarDirectory  Directory that Session::initialize receives. The request carries it as its path argument.
     */
    void prepare (const juce::File& sidecarDirectory);

private:
    /** Closes stdio, drops a queued update, and waits until the engine context runs the releaseResources request.
     *
     *  The destructor calls it. onReleaseResources shuts the session down.
     */
    void releaseResources ();

    /** Runs process on the engine context. jam::AsyncUpdater calls it on its serial context.
     *  Triggers that arrive before the call give one call.
     */
    void handleAsyncUpdate () override;

    /** Runs one pass of the adapter loop on the engine context.
     *
     *  A pass applies the pending input, calls drainInputClosed, then runs the lifecycle request in requests, if one waits.
     *  It polls the engine when isPolling is true. Otherwise it drains the launch channel.
     *  It applies the pending input again when a stop reason is set.
     *  It emits the stop messages, then checks the terminate deadline.
     *  It schedules the next pass when isRunning is set and isPolling is true.
     */
    void process ();

    /** Tells whether the engine polls for events.
     *
     *  @return true while the target runs or launches, or while a terminate deadline is pending.
     */
    bool isPolling () const;

    static int getRequestSeq (const jam::JsonDocument& request);

    template <typename NumberType>
    static NumberType getNumber (const jam::JsonDocument& request, const jam::Document::Element& object, const juce::Identifier& key)
    {
        const auto* value { request.find (object, jam::Strings::getView (key.toString ())) };

        return value != nullptr ? jam::Format::getNumber<NumberType> (request.getView (*value)) : NumberType {};
    }

    static juce::String getText (const jam::JsonDocument& request, const jam::Document::Element& object, const juce::Identifier& key);

    static void appendLocalsScope (std::string& json, int localsRef);

    void setAttachedState (std::uint32_t pid);

    static const jam::Document::Element& getArguments (const jam::JsonDocument& request);

    void onCommand (const jam::JsonDocument& document);

    /** Handles the client reply to the runInTerminal request.
     *
     *  A reply with success set to false logs the failure in debug builds and sends a failed launch response.
     *  A successful reply does nothing.
     *
     *  @param document  Reply document from the client.
     */
    void onRunInTerminal (const jam::JsonDocument& document);

    /** Routes one DAP request.
     *
     *  On Windows, a request whose command is in deferrals does not run while the target runs or launches.
     *  When the target runs and no break-in is pending, the call sets the break-in flag.
     *  It then interrupts the target.
     *  The caller, applyPendingRequests, leaves the frame of a deferred request in the input ring.
     *  Every other request runs at once through onCommand.
     *
     *  @param request  Request document.
     */
    void onRequest (const jam::JsonDocument& request);

    /** Tells whether a request must wait.
     *
     *  @param request  Request document.
     *  @return true when its command is in deferrals and the execution state is running or launching.
     */
    bool isDeferred (const jam::JsonDocument& request) const;

    /** Applies the requests that wait in the input ring, in arrival order.
     *
     *  A deferred request makes the protocol callback return false. The frame of that request and the
     *  later frames stay in the input ring. A request with a deferred command stays deferred while the
     *  execution state is running or launching. process calls this on each pass. process also calls it
     *  when a stop reason is set. drainProcessExited calls it when the process exits. drainInputClosed
     *  calls it at the end of stdin.
     */
    void applyPendingRequests ();

    void onPrepare (const jam::JsonDocument& document);

    void onReleaseResources (const jam::JsonDocument& document);

    void onInitialize (const jam::JsonDocument& document);

    void onLaunch (const jam::JsonDocument& document);

    /** Starts the target with Session::launch, for the internalConsole setting.
     *
     *  On success it adds the search paths, sets the launching state, and records the target
     *  program. It sends a successful launch response. On failure it sends a failed launch response.
     *
     *  @param seq      Sequence number of the launch request.
     *  @param program  Program path from the launch arguments.
     *  @param cwd      Working directory. Adds the search paths when not empty.
     */
    void launchInConsole (int seq, const juce::String& program, const juce::String& cwd);

    /** Starts the target in a terminal that the client opens, with a runInTerminal reverse request.
     *
     *  Stores the sequence number, so the launch response is sent after the Launcher attaches.
     *  When the client does not support runInTerminal, it sends a failed launch response.
     *
     *  @param seq      Sequence number of the launch request.
     *  @param program  Program path from the launch arguments.
     *  @param console  Console setting. externalTerminal selects an external terminal. Any other
     *                  value selects the integrated terminal.
     *  @param cwd      Working directory. Adds the search paths and sends it to the client when not empty.
     */
    void launchInTerminal (int seq, const juce::String& program, const juce::String& console, const juce::String& cwd);

    void onAttach (const jam::JsonDocument& document);

    void addSearchPaths (const juce::String& cwd);

    void onConfigurationDone (const jam::JsonDocument& document);

    void onDisconnect (const jam::JsonDocument& document, bool isTerminate);

    void onSetBreakpoints (const jam::JsonDocument& document);

    void onThreads (const jam::JsonDocument& document);

    void onStackTrace (const jam::JsonDocument& document);

    void onScopes (const jam::JsonDocument& document);

    void onVariables (const jam::JsonDocument& document);

    void onContinue (const jam::JsonDocument& document);

    void onNext (const jam::JsonDocument& document);

    void onStepIn (const jam::JsonDocument& document);

    void onStepOut (const jam::JsonDocument& document);

    void onPause (const jam::JsonDocument& document);

    void onEvaluate (const jam::JsonDocument& document);

    void onExceptionInfo (const jam::JsonDocument& document);

    /** Adds the handler of each DAP command to commands. The constructor calls it. */
    void registerCommands ();

    /** Adds the handlers of the engine events: targetProcessId, exited, terminated, running, and breakpointLocationFound.
     *
     *  The exited handler calls session.drainOutput before it stores the exit code.
     */
    void registerEvents ();

    /** Registers the handlers for the launch channel: runInTerminal, didAttach, and error.
     *
     *  runInTerminal triggers the engine context, which reads the channel when the engine does
     *  not poll. didAttach sets the attached state and sends the successful launch response.
     *  error sends a failed launch response that carries its text.
     */
    void registerLauncherEvents ();

    /** Registers the handlers that send the console, standardOutput, and standardError events as DAP output events. */
    void registerOutputEvents ();

    /** Emits the exited and terminated events once. It marks the execution state exited.
     *
     *  It also clears isRunning and the terminate deadline. applyPendingRequests runs first,
     *  before the exit events are added. Nothing happens once the state is exited.
     */
    void drainProcessExited ();

    /** Forces the exited state when the terminate deadline passes before the exit event.
     *
     *  process calls it on each pass. onDisconnect sets the deadline when it terminates the debuggee.
     */
    void drainTerminateTimeout ();

    /** Clears isRunning when stdin has closed, unless a terminate or a break-in is pending.
     *
     *  process calls it on each pass. When isInputClosed is not 0, it applies the pending input first.
     *  The acquire load pairs with the release store in the stdin callback.
     *  Frames the readiness context wrote before the end of stdin are in the input ring when the pass applies them.
     *  A pending terminate keeps the passes running, so drainProcessExited or drainTerminateTimeout can run.
     *  The frames behind a deferred request stay in the input ring until the target stops.
     *  A pending break-in keeps the passes running, so a later pass applies them.
     *  It runs before the lifecycle request, so a releaseResources pass applies the pending input before session.shutdown.
     */
    void drainInputClosed ();

    /** Writes one DAP message to the stdio output.
     *
     *  jam::StdioProtocol::writeMessage writes the Content-Length header and the body, then flushes the output.
     *
     *  @param json  One serialized DAP message.
     */
    void addMessage (std::string_view json);

    /** Adds a failed launch response that carries the sequence number stored by launchInTerminal.
     *
     *  @param text  Failure text for the message member.
     */
    void addLaunchErrorResponse (const juce::String& text);

    /** Writes each message in order, through addMessage.
     *
     *  @param messages  Serialized DAP messages.
     */
    void addMessages (const jam::Strings& messages);

    /** Returns the end mode that the session shutdown uses.
     *
     *  It returns passive after the exit. It returns terminate when the client asked to terminate the debuggee.
     *  Otherwise it returns detach. onReleaseResources passes the result to Session::shutdown.
     *
     *  @return The end mode.
     */
    debug::EndMode getEndModeForExit () const noexcept;

    WHATDBGModel& model { *WHATDBGModel::getInstance() };

    debug::Session session;

    debug::BreakpointManager breakpointManager { session };

    debug::VariableManager variableManager { session };

    debug::StopManager stopManager { session, breakpointManager, variableManager };

    /** Command names that onRequest defers while the target runs or launches. Windows adds
     *  setBreakpoints. macOS adds none.
     */
    juce::Array<juce::Identifier> deferrals;

    /** Single-slot mailbox of the prepare and releaseResources requests.
     *
     *  A write replaces an unread request, and the replaced request is deleted. process reads one request on each pass.
     *  The stdio requests do not use this mailbox.
     */
    jam::Mailbox<jam::JsonDocument> requests;

    /** Byte capacity of each stdio ring, for input and for output. Its value is one mebibyte. */
    static constexpr int bufferSize { 1 << 20 };

    /** Maps each DAP command name to its handler. The handler receives the name and the request document.
     *
     *  registerCommands fills it. onCommand looks up each request here. An unknown command gets an error response.
     */
    jam::Function::Map<juce::String, jam::Function::pointerCapacity, void (const jam::JsonDocument&)> commands;

    /** Maps the disconnect and terminate command names to Session::cancel.
     *
     *  applyPendingRequests calls the entry for a command before it routes the request.
     */
    jam::Function::Map<juce::String, jam::Function::pointerCapacity, void ()> cancellations;

    /** Time, in milliseconds, that the adapter waits for the target to exit after a terminate request.
     *
     *  onDisconnect sets the deadline from this value. drainTerminateTimeout forces the exit state at the deadline.
     */
    static constexpr std::uint32_t terminateTimeoutMs { 5000 };

    /** Timeout, in milliseconds, of each engine poll in process.
     *
     *  On Windows, the timeout applies only while a terminate deadline is pending.
     *  On macOS, the wait rounds the timeout up to a whole second.
     */
    static constexpr std::uint32_t pollTimeoutMs { 50 };

    /** Program path of the launch request. launchInConsole and launchInTerminal store it.
     *
     *  process passes it to stopManager.onStop on each pass.
     */
    juce::String targetProgram;

    /** Adapter channel: stdin for the input, stdout for the output.
     *
     *  prepare opens it, and releaseResources closes it. applyPendingRequests reads its input ring.
     *  addMessage writes its output. Its stdin callback triggers the engine context and wakes the session.
     *  The end of stdin ends the run through drainInputClosed, unless a terminate or a break-in is pending.
     *  It is the last member, so it is destroyed first.
     */
    jam::StdioServer stdio;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
