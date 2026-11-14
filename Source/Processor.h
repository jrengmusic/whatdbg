#pragma once
#include <JuceHeader.h>
#include "debug/Session.h"
#include "debug/BreakpointManager.h"
#include "debug/VariableManager.h"
#include "debug/StopManager.h"
#include "dap/FIFO.h"
#include "dap/Types.h"

/** Engine context of the adapter.
 *
 *  Runs every engine call on the serial context of jam::AsyncUpdater. Owns the debug
 *  session, the breakpoint, variable, and stop managers, the request lane, and the
 *  message lane. Writes no stdout. Stores the debug state through the model parameters.
 *
 *  Requests arrive through addRequest and leave the lane on the engine context. Messages
 *  go into the message lane and leave it through applyMessages on the message thread.
 */
class Processor : private jam::AsyncUpdater
{
public:
    /** Registers the command handlers, the cancellation handlers, and the engine event handlers. */
    Processor ();

    /** Calls releaseResources, which shuts the session down on the engine context. */
    ~Processor () override;

    /** Queues the prepare request with the sidecar folder and waits until the engine context runs it.
     *
     *  Called on the message thread from Whatdbg::initialise, before the reader starts.
     *
     *  @param sidecarDirectory  Folder that holds the extracted engine binaries. Sent as the path argument.
     */
    void prepare (const juce::File& sidecarDirectory);

    /** Queues one DAP request and triggers the engine context. Called from the reader thread.
     *
     *  A disconnect or terminate request first calls Session::cancel. The request then goes
     *  into the request lane, the engine context is triggered, and Session::wake is called
     *  so that a blocked poll returns.
     *
     *  @param request  Parsed DAP request. Moved into the request lane.
     */
    void addRequest (jam::JsonDocument request);

    /** Hands each queued DAP message to callback, in write order. Called on the message thread.
     *
     *  @tparam Callback  Callable that takes a std::string&&.
     *  @param callback   Receives each message by rvalue. The lane resets the slot after the call.
     */
    template <typename Callback>
    void applyMessages (Callback&& callback)
    {
        messages.applyPendingData (std::forward<Callback> (callback));
    }

private:
    void releaseResources ();

    void handleAsyncUpdate () override;

    void process ();

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
     *  A reply with success set to false logs the failure and sends a failed launch response.
     *  A successful reply does nothing.
     *
     *  @param document  Reply document from the client.
     */
    void onRunInTerminal (const jam::JsonDocument& document);

    /** Routes one DAP request from the request lane.
     *
     *  On Windows, a request whose command is in deferrals waits while the target runs or launches.
     *  When the target runs and no break-in is pending, the call sets the break-in flag and interrupts
     *  the target. Every other request runs at once.
     *
     *  @param request  Request document moved from the request lane.
     */
    void onRequest (jam::JsonDocument&& request);

    /** Runs the deferred requests in arrival order.
     *
     *  process calls it when a stop reason is set. drainProcessExited calls it when the process exits.
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

    void drainProcessExited ();

    void drainTerminateTimeout ();

    void addMessage (std::string json);

    /** Adds a failed launch response that carries the sequence number stored by launchInTerminal.
     *
     *  @param text  Failure text for the message member.
     */
    void addLaunchErrorResponse (const juce::String& text);

    void addMessages (const jam::Strings& messages);

    debug::EndMode getEndModeForExit () const noexcept;

    WHATDBGModel& model { *WHATDBGModel::getInstance() };

    debug::Session session;

    debug::BreakpointManager breakpointManager { session };

    debug::VariableManager variableManager { session };

    debug::StopManager stopManager { session, breakpointManager, variableManager };

    static constexpr int requestsCapacity { 64 };

    dap::FIFO<jam::JsonDocument> requests { requestsCapacity };

    /** Requests that onRequest deferred. applyPendingRequests runs them in arrival order. */
    dap::FIFO<jam::JsonDocument> pendingRequests { requestsCapacity };

    /** Command names that onRequest defers while the target runs or launches. Windows adds
     *  setBreakpoints. macOS adds none.
     */
    juce::Array<juce::Identifier> deferrals;

    static constexpr int messagesCapacity { 4096 };

    dap::FIFO<std::string> messages { messagesCapacity };

    jam::Function::Map<juce::String, jam::Function::pointerCapacity, void (const jam::JsonDocument&)> commands;

    jam::Function::Map<juce::String, jam::Function::pointerCapacity, void ()> cancellations;

    static constexpr std::uint32_t terminateTimeoutMs { 5000 };

    static constexpr std::uint32_t pollTimeoutMs { 50 };

    juce::String targetProgram;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
