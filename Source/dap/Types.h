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

/** Return the next monotonically increasing DAP sequence number.
 *
 *  Every DAP message (request, response, event) must carry a unique seq field.
 *  This function provides the global counter shared across all message types.
 *
 *  @return The next sequence number, starting at 1.
 *
 *  @note Not thread-safe. Must only be called on the main thread.
 */
inline int nextSeq () noexcept
{
    static int seq { 1 };
    return seq++;
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
inline juce::var makeResponse (int requestSeq,
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
 *  Convenience wrapper around makeResponse for the failure case. Sets
 *  success=false and attaches a human-readable message string.
 *
 *  @param requestSeq  The seq value of the originating request.
 *  @param command     The command name being responded to.
 *  @param message     Human-readable description of the error.
 *  @return A juce::var containing the error response DynamicObject.
 */
inline juce::var makeErrorResponse (int requestSeq,
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
inline juce::var makeEvent (const juce::String& event,
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
inline juce::var makeCapabilities () noexcept
{
    DynObj caps { new juce::DynamicObject () };
    caps->setProperty ("supportsConfigurationDoneRequest",   true);
    caps->setProperty ("supportsFunctionBreakpoints",        false);
    caps->setProperty ("supportsConditionalBreakpoints",     false);
    caps->setProperty ("supportsHitConditionalBreakpoints",  false);
    caps->setProperty ("supportsEvaluateForHovers",          true);
    caps->setProperty ("supportsSetVariable",                false);
    caps->setProperty ("supportsStepBack",                   false);
    caps->setProperty ("supportsRestartFrame",               false);
    caps->setProperty ("supportsGotoTargetsRequest",         false);
    caps->setProperty ("supportsStepInTargetsRequest",       false);
    caps->setProperty ("supportsCompletionsRequest",         false);
    caps->setProperty ("supportsModulesRequest",             false);
    caps->setProperty ("supportsExceptionOptions",           false);
    caps->setProperty ("supportsValueFormattingOptions",     false);
    caps->setProperty ("supportsExceptionInfoRequest",       false);
    caps->setProperty ("supportTerminateDebuggee",           true);
    caps->setProperty ("supportsDelayedStackTraceLoading",   false);
    caps->setProperty ("supportsLoadedSourcesRequest",       false);
    caps->setProperty ("supportsLogPoints",                  false);
    caps->setProperty ("supportsTerminateThreadsRequest",    false);
    caps->setProperty ("supportsSetExpression",              false);
    caps->setProperty ("supportsTerminateRequest",           true);
    caps->setProperty ("supportsDataBreakpoints",            false);
    caps->setProperty ("supportsReadMemoryRequest",          false);
    caps->setProperty ("supportsDisassembleRequest",         false);
    caps->setProperty ("supportsCancelRequest",              false);
    caps->setProperty ("supportsBreakpointLocationsRequest", false);
    caps->setProperty ("supportsClipboardContext",           false);
    caps->setProperty ("supportsSteppingGranularity",        false);
    caps->setProperty ("supportsInstructionBreakpoints",     false);
    caps->setProperty ("supportsExceptionFilterOptions",     false);
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
