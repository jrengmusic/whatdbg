#include "ToolServer.h"

ToolServer::ToolServer()
{
    for (const auto& command : { Id::launch, Id::attach })
    {
        server.addTool (jam::Strings::getView (command.toString()),
                        [this] (const juce::Identifier& name, const juce::String& requestId, const jam::JsonDocument& document, const jam::Document::Element& arguments)
                        {
                            startSession (requestId, jam::Strings::getView (name.toString()), document, arguments);
                        });
    }

    for (const auto& command : { Id::setBreakpoints, Id::configurationDone, Id::threads, Id::stackTrace,
                                 Id::scopes, Id::variables, Id::continueCommand, Id::next, Id::stepIn,
                                 Id::stepOut, Id::pause, Id::exceptionInfo, Id::evaluate, Id::disconnect,
                                 Id::terminate })
    {
        server.addTool (jam::Strings::getView (command.toString()),
                        [this] (const juce::Identifier& name, const juce::String& requestId, const jam::JsonDocument& document, const jam::Document::Element& arguments)
                        {
                            sendToSession (requestId, jam::Strings::getView (name.toString()), document, arguments);
                        });
    }

    server.addTool (jam::Strings::getView (Id::events.toString()),
                    [this] (const juce::Identifier&, const juce::String& requestId, const jam::JsonDocument& document, const jam::Document::Element& arguments)
                    {
                        requestSessionEvents (requestId, document, arguments);
                    });

    server.onCancelled = [this] (const juce::String& requestId)
    {
        for (auto& [handle, session] : sessions)
        {
            juce::ignoreUnused (handle);
            session->cancelRequest (requestId);
        }
    };

    server.onInputClosed = [this]
    {
        sessions.clear();
        quit();
    };

    model.state.addListener (this);
}

ToolServer::~ToolServer()
{
    model.state.removeListener (this);
}

const juce::String ToolServer::getApplicationName()
{
    return ProjectInfo::projectName;
}

const juce::String ToolServer::getApplicationVersion()
{
    return ProjectInfo::versionString;
}

bool ToolServer::moreThanOneInstanceAllowed()
{
    return true;
}

void ToolServer::initialise (const juce::String&)
{
    server.start();
}

void ToolServer::shutdown()
{
}

void ToolServer::anotherInstanceStarted (const juce::String&)
{
}

void ToolServer::systemRequestedQuit()
{
    quit();
}

void ToolServer::suspended()
{
}

void ToolServer::resumed()
{
}

void ToolServer::unhandledException (const std::exception*, const juce::String&, int)
{
}

void ToolServer::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType (jam::Format::toType (Id::session)))
    {
        const auto handle { tree[Id::id].toString() };

        if (sessions.contains (handle))
        {
            if (property == Id::numMessages)
            {
                sessions.at (handle)->setPendingMessages();
            }
            else if (property == Id::isOpen and static_cast<int> (tree[property]) == 0)
            {
                sessions.at (handle)->setClosed();
                sessions.erase (handle);
            }
        }
    }
}

void ToolServer::startSession (const juce::String& requestId,
                               std::string_view command,
                               const jam::JsonDocument& document,
                               const jam::Document::Element& arguments)
{
    static constexpr std::string_view openFailedMessage { "failed to open whatdbg session" };

    const auto workingDirectory { document.contains (arguments, jam::Strings::getView (Id::cwd.toString()))
                                      ? juce::File { jam::Strings::fromView (document.getString (document.get (arguments, jam::Strings::getView (Id::cwd.toString())))) }
                                      : juce::File::getCurrentWorkingDirectory() };
    const auto handle { juce::Uuid().toDashedString() };
    auto session { std::make_unique<Session> (model, handle) };

    session->onResponse = [this] (const juce::String& id, std::string_view content)
    {
        server.sendResponse (id, content);
    };

    session->onToolError = [this] (const juce::String& id, std::string_view text)
    {
        server.sendToolError (id, text);
    };

    session->onFailed = [this] (const juce::String& failedHandle)
    {
        sessions.erase (failedHandle);
    };

    const bool isOpened { session->open (juce::File::getSpecialLocation (juce::File::currentExecutableFile), workingDirectory) };

    if (isOpened)
    {
        sessions.emplace (handle, std::move (session));
        sessions.at (handle)->startHandshake (requestId, command, document, arguments);
    }
    else
    {
        server.sendToolError (requestId, openFailedMessage);
    }
}

void ToolServer::sendToSession (const juce::String& requestId,
                                std::string_view command,
                                const jam::JsonDocument& document,
                                const jam::Document::Element& arguments)
{
    const auto handle { jam::Strings::fromView (document.getString (document.get (arguments, jam::Strings::getView (Id::session.toString())))) };

    if (sessions.contains (handle))
        sessions.at (handle)->send (requestId, command, document, arguments);
    else
        sendUnknownSessionError (requestId, handle);
}

void ToolServer::requestSessionEvents (const juce::String& requestId,
                                       const jam::JsonDocument& document,
                                       const jam::Document::Element& arguments)
{
    const auto handle { jam::Strings::fromView (document.getString (document.get (arguments, jam::Strings::getView (Id::session.toString())))) };
    const auto timeoutMs { jam::Format::getNumber<int> (document.getView (document.get (arguments, jam::Strings::getView (Id::timeoutMs.toString())))) };

    if (sessions.contains (handle))
        sessions.at (handle)->requestEvents (requestId, timeoutMs);
    else
        sendUnknownSessionError (requestId, handle);
}

void ToolServer::sendUnknownSessionError (const juce::String& requestId, const juce::String& handle)
{
    static constexpr std::string_view unknownSessionPrefix { "unknown session: " };

    std::string errorText;

    jam::Format::appendBytes (errorText, unknownSessionPrefix);
    jam::Format::appendBytes (errorText, jam::Strings::getView (handle));
    server.sendToolError (requestId, errorText);
}
