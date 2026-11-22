/**
 * @file Session.h
 * @brief Child DAP session bridge between MCP tool calls and a whatdbg adapter process.
 */

#pragma once
#include <JuceHeader.h>
#include "generated/Generated.h"

/**
 * @class Session
 * @brief Bridges one MCP debug session to a child whatdbg DAP process over stdio.
 *
 * Stores session, pending-request, and queued-event state in a Model ValueTree row so
 * reader-thread callbacks marshal safely to the message thread.
 */
class Session : private juce::Timer
{
public:
    /**
     * @brief Registers a new session row and child trees under the shared Model.
     *
     * @param model   Shared Model whose state tree owns this session row.
     * @param handle  Unique UUID string identifying this debug session.
     */
    Session (jam::Model& model, const juce::String& handle);

    /**
     * @brief Stops the event wait timer and detaches this session row from the Model.
     */
    ~Session() override;

    /**
     * @brief Binds stdio reader callbacks and launches the child whatdbg adapter process.
     *
     * Callers must assign onResponse, onToolError, and onFailed before this call.
     *
     * @param whatdbgFile       Executable file for the child whatdbg adapter process.
     * @param workingDirectory  Working directory for the child adapter process.
     * @return True when the child process opens.
     */
    bool open (const juce::File& whatdbgFile, const juce::File& workingDirectory);

    /**
     * @brief Sends DAP initialize and queues the follow-up launch or attach request.
     *
     * Forces launch requests to use internalConsole so the adapter skips runInTerminal.
     *
     * @param requestId        MCP JSON-RPC request identifier for the handshake response.
     * @param followUpCommand  DAP command name to send after initialize succeeds.
     * @param document         Parsed JSON document holding the tool call arguments.
     * @param arguments        JSON object element containing the follow-up command arguments.
     */
    void startHandshake (const juce::String& requestId,
                         std::string_view followUpCommand,
                         const jam::JsonDocument& document,
                         const jam::Document::Element& arguments);

    /**
     * @brief Forwards one MCP tool call as a DAP request, omitting the session handle.
     *
     * @param requestId  MCP JSON-RPC request identifier for the tool response.
     * @param command    DAP command name to dispatch to the adapter.
     * @param document   Parsed JSON document holding the tool call arguments.
     * @param arguments  JSON object element containing the tool call arguments.
     */
    void send (const juce::String& requestId,
               std::string_view command,
               const jam::JsonDocument& document,
               const jam::Document::Element& arguments);

    /**
     * @brief Delivers queued DAP events immediately or arms a wait timer up to timeoutMs.
     *
     * Returns an error when another events request already waits on this session.
     *
     * @param requestId  MCP JSON-RPC request identifier for the events response.
     * @param timeoutMs  Maximum milliseconds to wait for an event; zero returns immediately.
     */
    void requestEvents (const juce::String& requestId, int timeoutMs);

    /**
     * @brief Aborts an active events wait when the client cancels requestId.
     *
     * @param requestId  MCP JSON-RPC request identifier from a cancellation notification.
     */
    void cancelRequest (const juce::String& requestId);

    /**
     * @brief Drains complete Content-Length frames from stdio and fails the session on handshake error.
     */
    void setPendingMessages();

    /**
     * @brief Drains remaining stdio frames, fails pending requests, and flushes any waiting events call.
     */
    void setClosed();

    /** @brief Runs on the message thread with the request id and JSON response payload. */
    std::function<void (const juce::String&, std::string_view)> onResponse;

    /** @brief Runs on the message thread with the request id and error text on failure. */
    std::function<void (const juce::String&, std::string_view)> onToolError;

    /** @brief Runs on the message thread with the session handle when a handshake step fails. */
    std::function<void (const juce::String&)> onFailed;

private:
    /**
     * @brief Completes an expired events wait with an empty JSON array.
     */
    void timerCallback() override;

    /**
     * @brief Writes one framed DAP request to the child process and records it in pendingTree.
     *
     * @param requestId         MCP JSON-RPC request identifier awaiting this DAP response.
     * @param command           DAP command name for the outbound request.
     * @param argumentsJson     Serialized JSON object string for the DAP arguments field.
     * @param prepareCommand    Follow-up DAP command to chain after initialize, or empty.
     * @param prepareArguments  Serialized JSON arguments for prepareCommand, or empty.
     * @param sessionHandle     Session handle to return after launch or attach, or empty.
     */
    void sendRequest (const juce::String& requestId,
                      std::string_view command,
                      std::string_view argumentsJson,
                      std::string_view prepareCommand,
                      std::string_view prepareArguments,
                      const juce::String& sessionHandle);

    /**
     * @brief Routes one parsed DAP message as either a response or a queued event.
     *
     * @param body  Raw JSON body of one Content-Length frame from the child adapter.
     * @return True to continue draining messages; false when a handshake step fails.
     */
    bool setMessage (std::string_view body);

    /**
     * @brief Matches a DAP response to pendingTree and advances handshakes or completes the MCP call.
     *
     * @param document  Parsed JSON document containing the DAP response message.
     * @param root      Root JSON object element of the DAP response message.
     * @return True to continue draining messages; false when a handshake step fails.
     */
    bool setResponse (const jam::JsonDocument& document, const jam::Document::Element& root);

    /**
     * @brief Serializes all buffered event entries in eventsTree into one JSON array string.
     *
     * @return JSON array text holding the queued DAP event objects in arrival order.
     */
    std::string getQueuedEventsJson() const;

    /**
     * @brief Appends serialized key-value members from object into members, skipping excludedKey.
     *
     * @param members      Destination JSON member buffer.
     * @param document     Parsed JSON document backing object.
     * @param object       JSON object element whose members to copy.
     * @param excludedKey  Member key to omit, or empty to copy all members.
     */
    static void appendObjectMembers (std::string& members,
                                     const jam::JsonDocument& document,
                                     const jam::Document::Element& object,
                                     std::string_view excludedKey);

    /**
     * @brief Returns the raw JSON source slice for a scalar token or compound child element.
     *
     * @param document    Parsed JSON document owning container and valueToken.
     * @param container   Parent JSON element containing the value.
     * @param valueToken  Token representing the value within container.
     * @return View of the raw JSON text for the value.
     */
    static std::string_view getValueView (const jam::JsonDocument& document,
                                          const jam::Document::Element& container,
                                          const jam::Document::Token& valueToken);

    /** @brief FIFO buffer capacity in bytes for reading child adapter stdout without backpressure. */
    static constexpr int bufferSize { 1 << 20 };

    /** @brief Polling period limit in milliseconds; zero dispatches reads without throttling. */
    static constexpr double maximumPeriodMilliseconds { 0.0 };

    /** @brief Empty JSON array that an events request returns when no events wait. */
    static constexpr std::string_view emptyArray { "[]" };

    /** @brief ValueTree row storing session handle, sequence counter, waiting events id, and status. */
    juce::ValueTree row { jam::Format::toType (Id::session) };

    /** @brief Child ValueTree holding raw DAP event payloads in arrival order until drained. */
    juce::ValueTree eventsTree { jam::Format::toType (Id::events) };

    /** @brief Child ValueTree mapping in-flight DAP sequence numbers to MCP request ids and handshake state. */
    juce::ValueTree pendingTree { jam::Format::toType (Id::pending) };

    /** @brief Atomic read-batch counter; client.onBytesReady increments it off the message thread. */
    jam::Parameter<int> numMessages { Id::numMessages, 0, Id::numMessages };

    /** @brief Atomic open flag; client.onExit sets it to zero when the child process exits. */
    jam::Parameter<int> isOpen { Id::isOpen, 1, Id::isOpen };

    /** @brief Message-thread attachment syncing numMessages onto row to wake ToolServer draining. */
    jam::Model::ParameterAttachment numMessagesAttachment { numMessages,
                                                            [this] (const juce::var& value)
                                                            {
                                                                auto target { row };
                                                                target.setProperty (Id::numMessages, value, nullptr);
                                                            } };

    /** @brief Message-thread attachment syncing isOpen onto row to trigger ToolServer session cleanup. */
    jam::Model::ParameterAttachment isOpenAttachment { isOpen,
                                                       [this] (const juce::var& value)
                                                       {
                                                           auto target { row };
                                                           target.setProperty (Id::isOpen, value, nullptr);
                                                       } };

    /** @brief Stdio subprocess client owning the child whatdbg DAP process and pipes. */
    jam::StdioClient client;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};
