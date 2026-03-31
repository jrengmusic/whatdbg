#pragma once
#include <JuceHeader.h>

namespace dap
{

inline int nextSeq () noexcept
{
    static int seq { 1 };
    return seq++;
}

inline juce::var makeResponse (int requestSeq,
                               const juce::String& command,
                               bool isSuccess,
                               juce::var body = juce::var ()) noexcept
{
    auto* obj { new juce::DynamicObject () };
    obj->setProperty ("seq",         nextSeq ());
    obj->setProperty ("type",        "response");
    obj->setProperty ("request_seq", requestSeq);
    obj->setProperty ("command",     command);
    obj->setProperty ("success",     isSuccess);

    if (not body.isVoid ())
        obj->setProperty ("body", body);

    return juce::var (obj);
}

inline juce::var makeErrorResponse (int requestSeq,
                                    const juce::String& command,
                                    const juce::String& message) noexcept
{
    auto* obj { new juce::DynamicObject () };
    obj->setProperty ("seq",         nextSeq ());
    obj->setProperty ("type",        "response");
    obj->setProperty ("request_seq", requestSeq);
    obj->setProperty ("command",     command);
    obj->setProperty ("success",     false);
    obj->setProperty ("message",     message);
    return juce::var (obj);
}

inline juce::var makeEvent (const juce::String& event,
                            juce::var body = juce::var ()) noexcept
{
    auto* obj { new juce::DynamicObject () };
    obj->setProperty ("seq",   nextSeq ());
    obj->setProperty ("type",  "event");
    obj->setProperty ("event", event);

    if (not body.isVoid ())
        obj->setProperty ("body", body);

    return juce::var (obj);
}

inline juce::var makeCapabilities () noexcept
{
    auto* caps { new juce::DynamicObject () };
    caps->setProperty ("supportsConfigurationDoneRequest",   true);
    caps->setProperty ("supportsFunctionBreakpoints",        false);
    caps->setProperty ("supportsConditionalBreakpoints",     false);
    caps->setProperty ("supportsHitConditionalBreakpoints",  false);
    caps->setProperty ("supportsEvaluateForHovers",          false);
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

// Helper to get a string property from a var (DynamicObject)
inline juce::String getString (const juce::var& obj, const juce::Identifier& key) noexcept
{
    if (auto* dynObj { obj.getDynamicObject () })
        return dynObj->getProperty (key).toString ();

    return {};
}

// Helper to get an int property from a var (DynamicObject)
inline int getInt (const juce::var& obj, const juce::Identifier& key) noexcept
{
    if (auto* dynObj { obj.getDynamicObject () })
        return static_cast<int> (dynObj->getProperty (key));

    return 0;
}

} // namespace dap
