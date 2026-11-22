#include "Session.h"

Session::Session (jam::Model& model, const juce::String& handle)
{
    row.setProperty (Id::id, handle, nullptr);
    row.setProperty (Id::nextSeq, 1, nullptr);
    row.setProperty (Id::events, juce::String {}, nullptr);
    row.setProperty (Id::numMessages, 0, nullptr);
    row.setProperty (Id::isOpen, 1, nullptr);
    row.appendChild (eventsTree, nullptr);
    row.appendChild (pendingTree, nullptr);
    model.state.appendChild (row, nullptr);
}

Session::~Session()
{
    stopTimer();

    auto parent { row.getParent() };

    if (parent.isValid())
        parent.removeChild (row, nullptr);
}

bool Session::open (const juce::File& whatdbgFile, const juce::File& workingDirectory)
{
    jassert (onResponse != nullptr and onToolError != nullptr and onFailed != nullptr);

    client.onBytesReady = [this] (int)
    {
        numMessages.setValue (numMessages.getValue() + 1);
    };

    client.onExit = [this] (int)
    {
        isOpen.setValue (0);
    };

    return client.open (jam::Strings { whatdbgFile.getFullPathName() },
                        workingDirectory,
                        jam::StdioClient::wantStdOut,
                        nullptr,
                        bufferSize,
                        maximumPeriodMilliseconds);
}

void Session::startHandshake (const juce::String& requestId,
                              std::string_view followUpCommand,
                              const jam::JsonDocument& document,
                              const jam::Document::Element& arguments)
{
    static constexpr std::string_view adapterIdKey { "adapterID" };
    static constexpr std::string_view adapterIdValue { "whatdbg-mcp" };

    std::string followUpArguments;

    jam::JsonWriter::appendObject (followUpArguments, [followUpCommand, &document, &arguments] (std::string& members)
    {
        appendObjectMembers (members, document, arguments, {});

        if (followUpCommand.compare (jam::Strings::getView (Id::launch.toString())) == 0)
        {
            jam::JsonWriter::appendKey (members, Id::console);
            jam::JsonWriter::appendString (members, jam::Strings::getView (Id::internalConsole.toString()));
        }
    });

    std::string initializeArguments;

    jam::JsonWriter::appendObject (initializeArguments, [] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, adapterIdKey);
        jam::JsonWriter::appendString (members, adapterIdValue);
        jam::JsonWriter::appendKey (members, Id::supportsRunInTerminalRequest);
        jam::JsonWriter::appendBoolean (members, false);
    });

    sendRequest (requestId,
                 jam::Strings::getView (Id::initialize.toString()),
                 initializeArguments,
                 followUpCommand,
                 followUpArguments,
                 {});
}

void Session::send (const juce::String& requestId,
                    std::string_view command,
                    const jam::JsonDocument& document,
                    const jam::Document::Element& arguments)
{
    std::string argumentsJson;

    jam::JsonWriter::appendObject (argumentsJson, [&document, &arguments] (std::string& members)
    {
        appendObjectMembers (members, document, arguments, jam::Strings::getView (Id::session.toString()));
    });

    sendRequest (requestId, command, argumentsJson, {}, {}, {});
}

void Session::requestEvents (const juce::String& requestId, int timeoutMs)
{
    static constexpr std::string_view eventsAlreadyWaitingMessage { "events already waiting" };

    if (eventsTree.getNumChildren() > 0)
    {
        const auto json { getQueuedEventsJson() };
        eventsTree.removeAllChildren (nullptr);
        onResponse (requestId, json);
    }
    else if (row[Id::events].toString().isNotEmpty())
    {
        onToolError (requestId, eventsAlreadyWaitingMessage);
    }
    else if (timeoutMs == 0)
    {
        onResponse (requestId, emptyArray);
    }
    else
    {
        row.setProperty (Id::events, requestId, nullptr);
        startTimer (timeoutMs);
    }
}

void Session::cancelRequest (const juce::String& requestId)
{
    if (row[Id::events].toString().compare (requestId) == 0)
    {
        stopTimer();
        row.setProperty (Id::events, juce::String {}, nullptr);
    }
}

void Session::setPendingMessages()
{
    bool hasFailed { false };

    client.applyPendingData ([this, &hasFailed] (const char* data, int size)
    {
        return jam::StdioProtocol::applyMessages (data, size, [this, &hasFailed] (std::string_view body)
        {
            if (not setMessage (body))
            {
                hasFailed = true;
                return false;
            }

            return true;
        });
    });

    if (hasFailed)
        onFailed (row[Id::id].toString());
}

void Session::setClosed()
{
    static constexpr std::string_view sessionEndedMessage { "session ended" };

    stopTimer();

    client.applyPendingData ([this] (const char* data, int size)
    {
        return jam::StdioProtocol::applyMessages (data, size, [this] (std::string_view body)
        {
            return setMessage (body);
        });
    });

    while (pendingTree.getNumChildren() > 0)
    {
        const auto pendingRow { pendingTree.getChild (0) };
        const auto requestId { pendingRow[Id::id].toString() };

        pendingTree.removeChild (0, nullptr);
        onToolError (requestId, sessionEndedMessage);
    }

    const auto waitingId { row[Id::events].toString() };

    if (waitingId.isNotEmpty())
    {
        row.setProperty (Id::events, juce::String {}, nullptr);
        const auto json { getQueuedEventsJson() };
        eventsTree.removeAllChildren (nullptr);
        onResponse (waitingId, json);
    }
}

void Session::timerCallback()
{
    stopTimer();

    const auto waitingId { row[Id::events].toString() };

    if (waitingId.isNotEmpty())
    {
        row.setProperty (Id::events, juce::String {}, nullptr);
        onResponse (waitingId, emptyArray);
    }
}

void Session::sendRequest (const juce::String& requestId,
                           std::string_view command,
                           std::string_view argumentsJson,
                           std::string_view prepareCommand,
                           std::string_view prepareArguments,
                           const juce::String& sessionHandle)
{
    const int seq { static_cast<int> (row[Id::nextSeq]) };
    row.setProperty (Id::nextSeq, seq + 1, nullptr);

    std::string json;

    jam::JsonWriter::appendObject (json, [seq, command, argumentsJson] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::seq);
        jam::Format::appendDecimal (members, seq);
        jam::JsonWriter::appendKey (members, Id::type);
        jam::JsonWriter::appendString (members, jam::Strings::getView (Id::request.toString()));
        jam::JsonWriter::appendKey (members, Id::command);
        jam::JsonWriter::appendString (members, command);
        jam::JsonWriter::appendKey (members, Id::arguments);
        jam::Format::appendBytes (members, argumentsJson);
    });

    jam::StdioProtocol::writeMessage (client, json);

    juce::ValueTree pendingRow { jam::Format::toType (Id::row) };

    pendingRow.setProperty (Id::requestSeq, seq, nullptr);
    pendingRow.setProperty (Id::id, requestId, nullptr);

    if (not prepareCommand.empty())
    {
        pendingRow.setProperty (Id::prepare, jam::Strings::fromView (prepareCommand), nullptr);
        pendingRow.setProperty (Id::arguments, jam::Strings::fromView (prepareArguments), nullptr);
    }

    if (sessionHandle.isNotEmpty())
        pendingRow.setProperty (Id::session, sessionHandle, nullptr);

    pendingTree.appendChild (pendingRow, nullptr);
}

bool Session::setMessage (std::string_view body)
{
    const auto document { jam::JsonDocument::read (body) };

    if (document != nullptr)
    {
        const auto* root { document->getChildByToken (*document->getRoot(), document->getValue()) };

        if (root != nullptr and root->id == Id::object and document->contains (*root, jam::Strings::getView (Id::type.toString())))
        {
            const auto type { document->getString (document->get (*root, jam::Strings::getView (Id::type.toString()))) };

            if (type.compare (jam::Strings::getView (Id::response.toString())) == 0)
                return setResponse (*document, *root);

            if (type.compare (jam::Strings::getView (Id::event.toString())) == 0)
            {
                juce::ValueTree entry { jam::Format::toType (Id::row) };

                entry.setProperty (Id::output, juce::var { juce::MemoryBlock { body.data(), body.size() } }, nullptr);
                eventsTree.appendChild (entry, nullptr);

                const auto waitingId { row[Id::events].toString() };

                if (waitingId.isNotEmpty())
                {
                    stopTimer();
                    row.setProperty (Id::events, juce::String {}, nullptr);
                    const auto json { getQueuedEventsJson() };
                    eventsTree.removeAllChildren (nullptr);
                    onResponse (waitingId, json);
                }
            }
        }
    }

    return true;
}

bool Session::setResponse (const jam::JsonDocument& document, const jam::Document::Element& root)
{
    static constexpr std::string_view emptyObject { "{}" };
    static constexpr std::string_view requestFailedMessage { "request failed" };

    if (document.contains (root, jam::Strings::getView (Id::requestSeq.toString())))
    {
        const auto requestSeq { jam::Format::getNumber<int> (document.getView (document.get (root, jam::Strings::getView (Id::requestSeq.toString())))) };
        const auto pendingRow { pendingTree.getChildWithProperty (Id::requestSeq, requestSeq) };

        if (pendingRow.isValid())
        {
            const auto requestId { pendingRow[Id::id].toString() };
            const auto prepareCommand { pendingRow[Id::prepare].toString() };
            const auto prepareArguments { pendingRow[Id::arguments].toString() };
            const auto sessionHandle { pendingRow[Id::session].toString() };

            pendingTree.removeChild (pendingRow, nullptr);

            const bool isSuccess { document.contains (root, jam::Strings::getView (Id::success.toString()))
                                   and document.getBoolean (document.get (root, jam::Strings::getView (Id::success.toString()))) };

            if (isSuccess)
            {
                if (prepareCommand.isNotEmpty())
                {
                    sendRequest (requestId,
                                 jam::Strings::getView (prepareCommand),
                                 jam::Strings::getView (prepareArguments),
                                 {},
                                 {},
                                 row[Id::id].toString());
                }
                else if (sessionHandle.isNotEmpty())
                {
                    std::string json;

                    jam::JsonWriter::appendObject (json, [&sessionHandle] (std::string& members)
                    {
                        jam::JsonWriter::appendKey (members, Id::session);
                        jam::JsonWriter::appendString (members, jam::Strings::getView (sessionHandle));
                    });

                    onResponse (requestId, json);
                }
                else if (document.contains (root, jam::Strings::getView (Id::body.toString())))
                {
                    const auto bodyView { getValueView (document, root, document.get (root, jam::Strings::getView (Id::body.toString()))) };

                    onResponse (requestId, bodyView);
                }
                else
                {
                    onResponse (requestId, emptyObject);
                }
            }
            else
            {
                const auto errorMessage { document.contains (root, jam::Strings::getView (Id::message.toString()))
                                              ? document.getString (document.get (root, jam::Strings::getView (Id::message.toString())))
                                              : std::string { requestFailedMessage } };

                onToolError (requestId, errorMessage);

                if (prepareCommand.isNotEmpty() or sessionHandle.isNotEmpty())
                    return false;
            }
        }
    }

    return true;
}

std::string Session::getQueuedEventsJson() const
{
    std::string json;

    jam::JsonWriter::appendArray (json, [this] (std::string& entries)
    {
        for (int index { 0 }; index < eventsTree.getNumChildren(); ++index)
        {
            const auto entry { eventsTree.getChild (index) };

            if (const auto* block { entry[Id::output].getBinaryData() }; block != nullptr)
            {
                jam::JsonWriter::appendSeparator (entries);
                jam::Format::appendBytes (entries, std::string_view { static_cast<const char*> (block->getData()), block->getSize() });
            }
        }
    });

    return json;
}

void Session::appendObjectMembers (std::string& members,
                                   const jam::JsonDocument& document,
                                   const jam::Document::Element& object,
                                   std::string_view excludedKey)
{
    const auto& tokens { *object.get<jam::Document::Tokens> (Id::tokens) };

    for (int index { 0 }; index < tokens.size(); index += jam::JsonDocument::numMemberTokens)
    {
        const auto key { document.getString (tokens.at (index)) };

        if (excludedKey.empty() or key.compare (excludedKey) != 0)
        {
            jam::JsonWriter::appendKey (members, key);
            jam::Format::appendBytes (members, getValueView (document, object, tokens.at (index + 1)));
        }
    }
}

std::string_view Session::getValueView (const jam::JsonDocument& document,
                                        const jam::Document::Element& container,
                                        const jam::Document::Token& valueToken)
{
    const auto* valueElement { document.getChildByToken (container, valueToken) };

    return valueElement != nullptr ? document.getView (valueElement->get<jam::Document::Tokens> (Id::range)->at (0))
                                   : document.getView (valueToken);
}
