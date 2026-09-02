#pragma once
#include <JuceHeader.h>

namespace dap
{

/** Convenience alias for a reference-counted DynamicObject pointer.
 *
 *  Used throughout the DAP layer to build JSON objects without manual
 *  reference counting. The underlying DynamicObject is heap-allocated and
 *  kept alive by the juce::var that wraps it.
 */
using DynObj = juce::ReferenceCountedObjectPtr<juce::DynamicObject>;

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

/** Build a DAP response object.
 *
 *  Constructs the mandatory DAP response envelope with seq, type, request_seq,
 *  command, and success fields. Optionally attaches a body object.
 *
 *  @param requestSeq  The seq value of the originating request.
 *  @param command     The command name being responded to (e.g. "initialize").
 *  @param isSuccess   Whether the command succeeded.
 *  @param body        Optional response body. Omitted when void (default).
 *  @return A juce::var containing the fully formed response DynamicObject.
 */
inline juce::var getResponse (int requestSeq,
                              const juce::String& command,
                              bool isSuccess,
                              juce::var body = juce::var ()) noexcept
{
    DynObj obj { new juce::DynamicObject () };
    obj->setProperty ("seq",         nextSeq ());
    obj->setProperty ("type",        "response");
    obj->setProperty ("request_seq", requestSeq);
    obj->setProperty ("command",     command);
    obj->setProperty ("success",     isSuccess);

    if (not body.isVoid ())
        obj->setProperty ("body", body);

    return juce::var (obj);
}

/** Build a DAP error response object.
 *
 *  Builds the same response envelope as getResponse for the failure case,
 *  with success=false and a human-readable message attached instead of a body.
 *
 *  @param requestSeq  The seq value of the originating request.
 *  @param command     The command name being responded to.
 *  @param message     Human-readable description of the error.
 *  @return A juce::var containing the error response DynamicObject.
 */
inline juce::var getErrorResponse (int requestSeq,
                                   const juce::String& command,
                                   const juce::String& message) noexcept
{
    DynObj obj { new juce::DynamicObject () };
    obj->setProperty ("seq",         nextSeq ());
    obj->setProperty ("type",        "response");
    obj->setProperty ("request_seq", requestSeq);
    obj->setProperty ("command",     command);
    obj->setProperty ("success",     false);
    obj->setProperty ("message",     message);
    return juce::var (obj);
}

/** Build a DAP event object.
 *
 *  Constructs the mandatory DAP event envelope with seq, type, and event fields.
 *  Optionally attaches a body object.
 *
 *  @param event  The event name (e.g. "stopped", "output", "terminated").
 *  @param body   Optional event body. Omitted when void (default).
 *  @return A juce::var containing the fully formed event DynamicObject.
 */
inline juce::var getEvent (const juce::String& event,
                           juce::var body = juce::var ()) noexcept
{
    DynObj obj { new juce::DynamicObject () };
    obj->setProperty ("seq",   nextSeq ());
    obj->setProperty ("type",  "event");
    obj->setProperty ("event", event);

    if (not body.isVoid ())
        obj->setProperty ("body", body);

    return juce::var (obj);
}

/** Build the DAP capabilities object advertised during the initialize handshake.
 *
 *  Lists every capability flag defined by the DAP specification with its current
 *  supported state. Unsupported capabilities are explicitly set to false so clients
 *  do not attempt to use them.
 *
 *  @return A juce::var containing the capabilities DynamicObject.
 *
 *  @note Update this function whenever a new capability is implemented or dropped.
 */
inline juce::var getCapabilities () noexcept
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

    DynObj caps { new juce::DynamicObject () };

    for (const auto& [capabilityName, isSupported] : capabilityFlags)
        caps->setProperty (capabilityName, isSupported);

    return juce::var (caps);
}

/** Extract a string property from a DAP message object.
 *
 *  Safe to call on any juce::var — returns an empty string if obj is not a
 *  DynamicObject or if the property does not exist.
 *
 *  @param obj  The juce::var expected to wrap a DynamicObject (parsed DAP message).
 *  @param key  The property name to retrieve.
 *  @return The property value as a string, or an empty string if not found.
 */
inline juce::String getString (const juce::var& obj, const juce::Identifier& key) noexcept
{
    if (auto* dynObj { obj.getDynamicObject () })
        return dynObj->getProperty (key).toString ();

    return {};
}

} // namespace dap
