#include <JuceHeader.h>
#include "Session.h"
#include "../dap/Types.h"

#if JUCE_MAC

namespace debug
{

using dap::DynObj;

// ---------------------------------------------------------------------------
// Session::ensureFrameVariablesCache
// ---------------------------------------------------------------------------

void Session::ensureFrameVariablesCache (int frameIndex) noexcept
{
    if (cachedFrameIndex != frameIndex)
    {
        auto frame { process.GetSelectedThread ().GetFrameAtIndex (static_cast<std::uint32_t> (frameIndex)) };
        cachedFrameVariables = frame.GetVariables (true, true, true, true);
        cachedFrameIndex = frameIndex;
    }
}

// ---------------------------------------------------------------------------
// Session::makeVariableDynObj
// ---------------------------------------------------------------------------

juce::var Session::makeVariableDynObj (lldb::SBValue& value, int symbolIndex) noexcept
{
    const char* rawVal { value.GetValue () };
    const juce::String displayValue { rawVal != nullptr
        ? juce::String (rawVal)
        : (value.GetSummary () != nullptr
            ? juce::String (value.GetSummary ())
            : juce::String ()) };

    DynObj obj { new juce::DynamicObject () };
    const char* rawName { value.GetName () };
    const char* rawType { value.GetTypeName () };
    obj->setProperty ("name",        juce::String (rawName != nullptr ? rawName : ""));
    obj->setProperty ("value",       displayValue);
    obj->setProperty ("type",        juce::String (rawType != nullptr ? rawType : ""));
    obj->setProperty ("hasChildren", value.MightHaveChildren ());
    obj->setProperty ("symbolIndex", symbolIndex);

    return juce::var (obj);
}

// ---------------------------------------------------------------------------
// Session::getStackTrace
// ---------------------------------------------------------------------------

juce::Array<juce::var> Session::getStackTrace (int maxFrames) noexcept
{
    // BLESSED L: 38 lines. Null-safe const char* → juce::String + source sub-object
    // is inline cost; frame-shape DynObj has no second caller so extracting a helper
    // would be YAGNI. Smell-detector acknowledged, decomposition rejected.
    juce::Array<juce::var> frames;
    auto thread { process.GetSelectedThread () };
    const int numFrames { static_cast<int> (thread.GetNumFrames ()) };
    const int clampedMax { juce::jmin (maxFrames, numFrames) };

    for (int i { 0 }; i < clampedMax; ++i)
    {
        auto frame { thread.GetFrameAtIndex (static_cast<std::uint32_t> (i)) };
        DynObj frameEntry { new juce::DynamicObject () };

        const char* rawName { frame.GetFunctionName () };
        frameEntry->setProperty ("id",   static_cast<int> (i));
        frameEntry->setProperty ("name", juce::String (rawName != nullptr ? rawName : "??"));

        auto lineEntry { frame.GetLineEntry () };

        if (lineEntry.IsValid ())
        {
            DynObj source { new juce::DynamicObject () };
            const char* rawFileName { lineEntry.GetFileSpec ().GetFilename () };
            const char* rawFilePath { lineEntry.GetFileSpec ().GetDirectory () };
            const juce::String fileName { rawFileName != nullptr ? rawFileName : "" };
            const juce::String dirPath  { rawFilePath  != nullptr ? rawFilePath  : "" };
            source->setProperty ("name", fileName);
            source->setProperty ("path", dirPath.isNotEmpty ()
                                             ? dirPath + "/" + fileName
                                             : fileName);
            frameEntry->setProperty ("source", juce::var (source));
            frameEntry->setProperty ("line",   static_cast<int> (lineEntry.GetLine ()));
            frameEntry->setProperty ("column", 1);
        }

        frames.add (juce::var (frameEntry));
    }

    return frames;
}

// ---------------------------------------------------------------------------
// Session::getLocals
// ---------------------------------------------------------------------------

juce::Array<juce::var> Session::getLocals (int frameIndex) noexcept
{
    juce::Array<juce::var> result;

    ensureFrameVariablesCache (frameIndex);

    for (std::uint32_t i { 0 }; i < cachedFrameVariables.GetSize (); ++i)
    {
        auto value { cachedFrameVariables.GetValueAtIndex (i) };
        result.add (makeVariableDynObj (value, static_cast<int> (i)));
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::getVariableChildren
// ---------------------------------------------------------------------------

juce::Array<juce::var> Session::getVariableChildren (int frameIndex, int symbolIndex) noexcept
{
    juce::Array<juce::var> result;

    ensureFrameVariablesCache (frameIndex);

    if (symbolIndex >= 0
        and symbolIndex < static_cast<int> (cachedFrameVariables.GetSize ()))
    {
        auto parent { cachedFrameVariables.GetValueAtIndex (static_cast<std::uint32_t> (symbolIndex)) };
        const std::uint32_t numChildren { parent.GetNumChildren () };

        for (std::uint32_t i { 0 }; i < numChildren; ++i)
        {
            auto child { parent.GetChildAtIndex (i) };
            result.add (makeVariableDynObj (child, static_cast<int> (i)));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::evaluateExpression
// Phase 5 will wrap raw LLDB output through the pretty-print layer for STL-type parity.
// ---------------------------------------------------------------------------

juce::String Session::evaluateExpression (const juce::String& expression, int frameIndex) noexcept
{
    juce::String result;
    auto thread { process.GetSelectedThread () };
    auto frame { thread.GetFrameAtIndex (static_cast<std::uint32_t> (frameIndex)) };
    auto value { frame.EvaluateExpression (expression.toRawUTF8 ()) };

    if (value.IsValid () and value.GetError ().Success ())
    {
        if (value.GetValue () != nullptr)
        {
            result = juce::String { value.GetValue () };
        }
        else if (value.GetSummary () != nullptr)
        {
            result = juce::String { value.GetSummary () };
        }
    }
    else
    {
        if (value.GetError ().GetCString () != nullptr)
            result = juce::String { value.GetError ().GetCString () };
        else
            result = "evaluation failed";
    }

    return result;
}

} // namespace debug

#endif // JUCE_MAC
