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
     *  @tparam Callback  Callable that takes a const std::string&.
     *  @param callback   Receives each message. The lane clears the slot after the call.
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

    static int getRequestSeq (const jam::JsonDocument& request);

    template <typename NumberType>
    static NumberType getNumber (const jam::JsonDocument& request, const jam::Document::Element& object, const juce::Identifier& key)
    {
        return jam::Format::getNumber<NumberType> (request.getView (*request.find (object, jam::Strings::getView (key.toString ()))));
    }

    static juce::String getText (const jam::JsonDocument& request, const jam::Document::Element& object, const juce::Identifier& key);

    static void appendLocalsScope (std::string& json, int localsRef);

    void setAttachedState (std::uint32_t pid);

    static const jam::Document::Element& getArguments (const jam::JsonDocument& request);

    void onCommand (const jam::JsonDocument& document);

    void onPrepare (const jam::JsonDocument& document);

    void onReleaseResources (const jam::JsonDocument& document);

    void onInitialize (const jam::JsonDocument& document);

    void onLaunch (const jam::JsonDocument& document);

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

    void drainProcessExited ();

    void drainTerminateTimeout ();

    void addMessage (std::string json);

    void addMessages (const jam::Strings& messages);

    debug::EndMode getEndModeForExit () const noexcept;

    WHATDBGModel& model { *WHATDBGModel::getInstance() };

    debug::Session session;

    debug::BreakpointManager breakpointManager { session };

    debug::VariableManager variableManager { session };

    debug::StopManager stopManager { session, breakpointManager, variableManager };

    static constexpr int requestsCapacity { 64 };

    dap::FIFO<jam::JsonDocument> requests { requestsCapacity };

    static constexpr int messagesCapacity { 4096 };

    dap::FIFO<std::string> messages { messagesCapacity };

    jam::Function::Map<juce::String, jam::Function::pointerCapacity, void (const jam::JsonDocument&)> commands;

    jam::Function::Map<juce::String, jam::Function::pointerCapacity, void ()> cancellations;

    static constexpr std::uint32_t terminateTimeoutMs { 5000 };

    static constexpr std::uint32_t pollTimeoutMs { 50 };

    juce::String targetProgram;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
