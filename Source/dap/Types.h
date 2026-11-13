#pragma once
#include <JuceHeader.h>
#include "../generated/Generated.h"

namespace dap
{

/** Process-wide DAP sequence counter.
 *
 *  Every DAP message (request, response, event) must carry a unique seq field.
 *  This is the single shared counter consulted by getResponse, getErrorResponse,
 *  and getEvent. Declared as a C++17 inline variable so every translation unit
 *  that includes this header shares the same instance.
 *
 *  @note Not thread-safe. Must only be touched on the main thread.
 *  @note This is DAP protocol session state and belongs, in the fully migrated
 *        architecture, to debug::State (the Model) rather than to this header —
 *        tracked as outstanding work pending a State.h change outside this file.
 */
inline int nextSequenceNumber { 1 };

/** Return the next monotonically increasing DAP sequence number.
 *
 *  @return The next sequence number, starting at 1.
 */
inline int nextSeq () noexcept
{
    return nextSequenceNumber++;
}

inline void appendResponseMembers (std::string& json, int requestSeq, std::string_view command, bool isSuccess)
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

inline void appendEventMembers (std::string& json, std::string_view event)
{
    jam::JsonWriter::appendKey (json, Id::seq);
    jam::Format::appendDecimal (json, nextSeq ());
    jam::JsonWriter::appendKey (json, Id::type);
    jam::JsonWriter::appendString (json, jam::Strings::getView (Id::event.toString ()));
    jam::JsonWriter::appendKey (json, Id::event);
    jam::JsonWriter::appendString (json, event);
}

template <typename Function>
inline void appendResponse (std::string& json, int requestSeq, std::string_view command, bool isSuccess, const Function& appendBody)
{
    jam::JsonWriter::appendObject (json, [requestSeq, command, isSuccess, &appendBody] (std::string& members)
    {
        appendResponseMembers (members, requestSeq, command, isSuccess);
        jam::JsonWriter::appendKey (members, Id::body);
        jam::JsonWriter::appendObject (members, appendBody);
    });
}

inline void appendResponse (std::string& json, int requestSeq, std::string_view command, bool isSuccess)
{
    jam::JsonWriter::appendObject (json, [requestSeq, command, isSuccess] (std::string& members)
    {
        appendResponseMembers (members, requestSeq, command, isSuccess);
    });
}

inline void appendErrorResponse (std::string& json, int requestSeq, std::string_view command, std::string_view message)
{
    jam::JsonWriter::appendObject (json, [requestSeq, command, message] (std::string& members)
    {
        appendResponseMembers (members, requestSeq, command, false);
        jam::JsonWriter::appendKey (members, Id::message);
        jam::JsonWriter::appendString (members, message);
    });
}

template <typename Function>
inline void appendEvent (std::string& json, std::string_view event, const Function& appendBody)
{
    jam::JsonWriter::appendObject (json, [event, &appendBody] (std::string& members)
    {
        appendEventMembers (members, event);
        jam::JsonWriter::appendKey (members, Id::body);
        jam::JsonWriter::appendObject (members, appendBody);
    });
}

inline void appendEvent (std::string& json, std::string_view event)
{
    jam::JsonWriter::appendObject (json, [event] (std::string& members)
    {
        appendEventMembers (members, event);
    });
}

inline void appendBreakpointChangedEvent (std::string& json, int dapId, int line)
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

inline void appendSource (std::string& json, std::string_view name, std::string_view path)
{
    jam::JsonWriter::appendObject (json, [name, path] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::name);
        jam::JsonWriter::appendString (members, name);
        jam::JsonWriter::appendKey (members, Id::path);
        jam::JsonWriter::appendString (members, path);
    });
}

inline void appendCapabilities (std::string& json)
{
    static const std::vector<std::pair<juce::Identifier, bool>> capabilityFlags
    {
        { "supportsConfigurationDoneRequest",   true },
        { "supportsFunctionBreakpoints",        false },
        { "supportsConditionalBreakpoints",     false },
        { "supportsHitConditionalBreakpoints",  false },
        { "supportsEvaluateForHovers",          true },
        { "supportsSetVariable",                false },
        { "supportsStepBack",                   false },
        { "supportsRestartFrame",               false },
        { "supportsGotoTargetsRequest",         false },
        { "supportsStepInTargetsRequest",       false },
        { "supportsCompletionsRequest",         false },
        { "supportsModulesRequest",             false },
        { "supportsExceptionOptions",           false },
        { "supportsValueFormattingOptions",     false },
        { "supportsExceptionInfoRequest",       true },
        { "supportTerminateDebuggee",           true },
        { "supportsDelayedStackTraceLoading",   false },
        { "supportsLoadedSourcesRequest",       false },
        { "supportsLogPoints",                  false },
        { "supportsTerminateThreadsRequest",    false },
        { "supportsSetExpression",              false },
        { "supportsTerminateRequest",           true },
        { "supportsDataBreakpoints",            false },
        { "supportsReadMemoryRequest",          false },
        { "supportsDisassembleRequest",         false },
        { "supportsCancelRequest",              false },
        { "supportsBreakpointLocationsRequest", false },
        { "supportsClipboardContext",           false },
        { "supportsSteppingGranularity",        false },
        { "supportsInstructionBreakpoints",     false },
        { "supportsExceptionFilterOptions",     false }
    };

    for (const auto& [capabilityName, isSupported] : capabilityFlags)
    {
        jam::JsonWriter::appendKey (json, capabilityName);
        jam::JsonWriter::appendBoolean (json, isSupported);
    }
}

} // namespace dap
