/** @file Types.cpp
 *  @brief DAP message builders: sequence numbers, the response and event envelopes, the capability list, and the stopped, process, output, and exception events.
 */

#include "Types.h"
#include "../WHATDBGModel.h"

namespace dap
{

int nextSeq () noexcept
{
    auto& model { *WHATDBGModel::getInstance () };
    auto* sequenceNumber { model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::nextSequenceNumber) };
    const int value { sequenceNumber->getValue () };

    model.storeValue (model.sessionTag, Id::nextSequenceNumber, value + 1);

    return value;
}

void appendResponseMembers (std::string& json, int requestSeq, std::string_view command, bool isSuccess)
{
    jam::JsonWriter::appendKey (json, Id::seq);
    jam::Format::appendDecimal (json, nextSeq ());
    jam::JsonWriter::appendKey (json, Id::type);
    jam::JsonWriter::appendString (json, jam::Strings::getView (Id::response.toString ()));
    jam::JsonWriter::appendKey (json, Id::requestSeq);
    jam::Format::appendDecimal (json, requestSeq);
    jam::JsonWriter::appendKey (json, Id::command);
    jam::JsonWriter::appendString (json, command);
    jam::JsonWriter::appendKey (json, Id::success);
    jam::JsonWriter::appendBoolean (json, isSuccess);
}

void appendEventMembers (std::string& json, std::string_view event)
{
    jam::JsonWriter::appendKey (json, Id::seq);
    jam::Format::appendDecimal (json, nextSeq ());
    jam::JsonWriter::appendKey (json, Id::type);
    jam::JsonWriter::appendString (json, jam::Strings::getView (Id::event.toString ()));
    jam::JsonWriter::appendKey (json, Id::event);
    jam::JsonWriter::appendString (json, event);
}

void appendResponse (std::string& json, int requestSeq, std::string_view command, bool isSuccess)
{
    jam::JsonWriter::appendObject (json, [requestSeq, command, isSuccess] (std::string& members)
    {
        appendResponseMembers (members, requestSeq, command, isSuccess);
    });
}

void appendErrorResponse (std::string& json, int requestSeq, std::string_view command, std::string_view message)
{
    jam::JsonWriter::appendObject (json, [requestSeq, command, message] (std::string& members)
    {
        appendResponseMembers (members, requestSeq, command, false);
        jam::JsonWriter::appendKey (members, Id::message);
        jam::JsonWriter::appendString (members, message);
    });
}

void appendEvent (std::string& json, std::string_view event)
{
    jam::JsonWriter::appendObject (json, [event] (std::string& members)
    {
        appendEventMembers (members, event);
    });
}

jam::JsonDocument getRequest (std::string_view command)
{
    std::string json;

    jam::JsonWriter::appendObject (json, [command] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::seq);
        jam::Format::appendDecimal (members, nextSeq ());
        jam::JsonWriter::appendKey (members, Id::type);
        jam::JsonWriter::appendString (members, jam::Strings::getView (Id::request.toString ()));
        jam::JsonWriter::appendKey (members, Id::command);
        jam::JsonWriter::appendString (members, command);
    });

    return std::move (*jam::JsonDocument::parse (std::move (json)));
}

void appendBreakpointChangedEvent (std::string& json, int dapId, int line)
{
    appendEvent (json, jam::Strings::getView (Id::breakpoint.toString ()), [dapId, line] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::reason);
        jam::JsonWriter::appendString (body, jam::Strings::getView (Id::changed.toString ()));
        jam::JsonWriter::appendKey (body, Id::breakpoint);
        jam::JsonWriter::appendObject (body, [dapId, line] (std::string& breakpoint)
        {
            jam::JsonWriter::appendKey (breakpoint, Id::id);
            jam::Format::appendDecimal (breakpoint, dapId);
            jam::JsonWriter::appendKey (breakpoint, Id::verified);
            jam::JsonWriter::appendBoolean (breakpoint, true);
            jam::JsonWriter::appendKey (breakpoint, Id::line);
            jam::Format::appendDecimal (breakpoint, line);
        });
    });
}

void appendSource (std::string& json, std::string_view name, std::string_view path)
{
    jam::JsonWriter::appendObject (json, [name, path] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::name);
        jam::JsonWriter::appendString (members, name);
        jam::JsonWriter::appendKey (members, Id::path);
        jam::JsonWriter::appendString (members, path);
    });
}

void appendCapabilities (std::string& json)
{
    static const std::vector<std::pair<juce::Identifier, bool>> capabilityFlags
    {
        { Id::supportsConfigurationDoneRequest,   true },
        { Id::supportsFunctionBreakpoints,        false },
        { Id::supportsConditionalBreakpoints,     false },
        { Id::supportsHitConditionalBreakpoints,  false },
        { Id::supportsEvaluateForHovers,          true },
        { Id::supportsSetVariable,                false },
        { Id::supportsStepBack,                   false },
        { Id::supportsRestartFrame,               false },
        { Id::supportsGotoTargetsRequest,         false },
        { Id::supportsStepInTargetsRequest,       false },
        { Id::supportsCompletionsRequest,         false },
        { Id::supportsModulesRequest,             false },
        { Id::supportsExceptionOptions,           false },
        { Id::supportsValueFormattingOptions,     false },
        { Id::supportsExceptionInfoRequest,       true },
        { Id::supportTerminateDebuggee,           true },
        { Id::supportsDelayedStackTraceLoading,   false },
        { Id::supportsLoadedSourcesRequest,       false },
        { Id::supportsLogPoints,                  false },
        { Id::supportsTerminateThreadsRequest,    false },
        { Id::supportsSetExpression,              false },
        { Id::supportsTerminateRequest,           true },
        { Id::supportsDataBreakpoints,            false },
        { Id::supportsReadMemoryRequest,          false },
        { Id::supportsDisassembleRequest,         false },
        { Id::supportsCancelRequest,              false },
        { Id::supportsBreakpointLocationsRequest, false },
        { Id::supportsClipboardContext,           false },
        { Id::supportsSteppingGranularity,        false },
        { Id::supportsInstructionBreakpoints,     false },
        { Id::supportsExceptionFilterOptions,     false }
    };

    for (const auto& [capabilityName, isSupported] : capabilityFlags)
    {
        jam::JsonWriter::appendKey (json, capabilityName);
        jam::JsonWriter::appendBoolean (json, isSupported);
    }
}

std::string getStoppedEvent (std::string_view reason, int threadId)
{
    std::string json;

    appendEvent (json, jam::Strings::getView (Id::stopped.toString ()), [reason, threadId] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::reason);
        jam::JsonWriter::appendString (body, reason);
        jam::JsonWriter::appendKey (body, Id::threadId);
        jam::Format::appendDecimal (body, threadId);
        jam::JsonWriter::appendKey (body, Id::allThreadsStopped);
        jam::JsonWriter::appendBoolean (body, true);
    });

    return json;
}

juce::String getExceptionDescription (juce::int64 exceptionCode, juce::int64 exceptionAddress)
{
    const juce::String codeHex    { juce::String::toHexString (exceptionCode) };
    const juce::String addressHex { juce::String::toHexString (exceptionAddress) };

    return juce::String ("0x") + codeHex + " at 0x" + addressHex;
}

std::string getExceptionStoppedEvent (const juce::String& exceptionName,
                                      const juce::String& description,
                                      int threadId)
{
    std::string json;

    appendEvent (json, jam::Strings::getView (Id::stopped.toString ()), [&exceptionName, &description, threadId] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::reason);
        jam::JsonWriter::appendString (body, jam::Strings::getView (Id::exception.toString ()));
        jam::JsonWriter::appendKey (body, Id::text);
        jam::JsonWriter::appendString (body, jam::Strings::getView (exceptionName));
        jam::JsonWriter::appendKey (body, Id::description);
        jam::JsonWriter::appendString (body, jam::Strings::getView (description));
        jam::JsonWriter::appendKey (body, Id::threadId);
        jam::Format::appendDecimal (body, threadId);
        jam::JsonWriter::appendKey (body, Id::allThreadsStopped);
        jam::JsonWriter::appendBoolean (body, true);
    });

    return json;
}

std::string getExceptionOutputEvent (const juce::String& exceptionName,
                                     const juce::String& description)
{
    std::string json;

    appendEvent (json, jam::Strings::getView (Id::output.toString ()), [&exceptionName, &description] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::category);
        jam::JsonWriter::appendString (body, jam::Strings::getView (Id::standardError.toString ()));
        jam::JsonWriter::appendKey (body, Id::output);
        jam::JsonWriter::appendString (body, jam::Strings::getView ("Unhandled exception: " + exceptionName + " " + description + "\n"));
    });

    return json;
}

std::string getProcessEvent (const juce::String& name,
                             std::uint32_t processId,
                             std::string_view startMethod)
{
    std::string json;

    appendEvent (json, jam::Strings::getView (Id::process.toString ()), [&name, processId, startMethod] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::name);
        jam::JsonWriter::appendString (body, jam::Strings::getView (name));
        jam::JsonWriter::appendKey (body, Id::systemProcessId);
        jam::Format::appendDecimal (body, static_cast<int> (processId));
        jam::JsonWriter::appendKey (body, Id::isLocalProcess);
        jam::JsonWriter::appendBoolean (body, true);
        jam::JsonWriter::appendKey (body, Id::startMethod);
        jam::JsonWriter::appendString (body, startMethod);
    });

    return json;
}

std::string getOutputEvent (std::string_view category, std::string_view output)
{
    std::string json;

    appendEvent (json, jam::Strings::getView (Id::output.toString ()), [category, output] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::category);
        jam::JsonWriter::appendString (body, category);
        jam::JsonWriter::appendKey (body, Id::output);
        jam::JsonWriter::appendString (body, output);
    });

    return json;
}

} // namespace dap
