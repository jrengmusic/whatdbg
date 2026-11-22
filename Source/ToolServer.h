/**
 * @file ToolServer.h
 * @brief MCP server application that routes whatdbg tool calls to child DAP sessions.
 */

#pragma once
#include <JuceHeader.h>
#include <BinaryData.h>
#include "Session.h"
#include "generated/Generated.h"

/**
 * @class ToolServer
 * @brief Runs the JUCE application loop for the whatdbg MCP stdio server.
 *
 * Registers MCP tool callbacks and dispatches child session events on the message thread.
 */
class ToolServer : public juce::JUCEApplicationBase,
                   private juce::ValueTree::Listener
{
public:
    /**
     * @brief Registers all whatdbg MCP tools, binds server callbacks, and listens to model state.
     */
    ToolServer();

    /**
     * @brief Detaches this listener from model.state before destroying sessions and server.
     */
    ~ToolServer() override;

    /**
     * @brief Returns the application name from ProjectInfo.
     *
     * @return The project name string.
     */
    const juce::String getApplicationName() override;

    /**
     * @brief Returns the application version from ProjectInfo.
     *
     * @return The project version string.
     */
    const juce::String getApplicationVersion() override;

    /**
     * @brief Permits concurrent MCP server processes for separate clients.
     *
     * @return Always returns true.
     */
    bool moreThanOneInstanceAllowed() override;

    /**
     * @brief Starts the stdio MCP server loop on application startup.
     *
     * @param commandLine  Unused startup command line text from JUCE.
     */
    void initialise (const juce::String& commandLine) override;

    /**
     * @brief Performs no extra shutdown work; input closure and destructors release state.
     */
    void shutdown() override;

    /**
     * @brief Ignores secondary launch notices because each stdio process serves one client.
     *
     * @param commandLine  Unused command line text from a secondary launch.
     */
    void anotherInstanceStarted (const juce::String& commandLine) override;

    /**
     * @brief Quits the JUCE message loop when the host OS requests termination.
     */
    void systemRequestedQuit() override;

    /**
     * @brief Ignores application suspend events in headless stdio mode.
     */
    void suspended() override;

    /**
     * @brief Ignores application resume events in headless stdio mode.
     */
    void resumed() override;

    /**
     * @brief Leaves unhandled exceptions unintercepted; required JUCEApplicationBase override.
     */
    void unhandledException (const std::exception*, const juce::String&, int) override;

private:
    /**
     * @brief Handles message-thread notifications when a session row updates numMessages or closes.
     *
     * @param tree      Modified ValueTree node inside model.state.
     * @param property  Identifier of the changed property on tree.
     */
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    /**
     * @brief Spawns a child whatdbg session and begins its DAP initialize handshake.
     *
     * @param requestId  MCP JSON-RPC request identifier for the start tool call.
     * @param command    Follow-up DAP command name, either launch or attach.
     * @param document   Parsed JSON document holding the tool call arguments.
     * @param arguments  JSON object element containing target options and optional cwd.
     */
    void startSession (const juce::String& requestId,
                       std::string_view command,
                       const jam::JsonDocument& document,
                       const jam::Document::Element& arguments);

    /**
     * @brief Looks up the target session by handle and forwards a DAP command.
     *
     * @param requestId  MCP JSON-RPC request identifier for the tool call.
     * @param command    DAP command name to send to the child session.
     * @param document   Parsed JSON document holding the tool call arguments.
     * @param arguments  JSON object element containing the session handle and command fields.
     */
    void sendToSession (const juce::String& requestId,
                        std::string_view command,
                        const jam::JsonDocument& document,
                        const jam::Document::Element& arguments);

    /**
     * @brief Looks up the target session by handle and forwards an events wait request.
     *
     * @param requestId  MCP JSON-RPC request identifier for the events call.
     * @param document   Parsed JSON document holding the tool call arguments.
     * @param arguments  JSON object element containing session and timeoutMs.
     */
    void requestSessionEvents (const juce::String& requestId,
                               const jam::JsonDocument& document,
                               const jam::Document::Element& arguments);

    /**
     * @brief Sends a tool error reporting that handle matches no active session.
     *
     * @param requestId  MCP JSON-RPC request identifier for the failing tool call.
     * @param handle     Unknown session handle string from the tool arguments.
     */
    void sendUnknownSessionError (const juce::String& requestId, const juce::String& handle);

    /** @brief Shared Model owning child session ValueTree rows for message-thread dispatch. */
    jam::Model model { Id::session };

    /** @brief JSON-RPC stdio server exposing tools from embedded whatdbgTools metadata. */
    jam::ModelContextServer server { BinaryData::getString (files::whatdbgTools).toStdString(),
                                     ProjectInfo::projectName,
                                     ProjectInfo::versionString };

    /** @brief Active child debug sessions keyed by UUID session handle. */
    jam::HashMap<juce::String, std::unique_ptr<Session>> sessions;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolServer)
};
