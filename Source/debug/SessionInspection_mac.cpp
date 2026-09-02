/** @file SessionInspection_mac.cpp
 *  @brief macOS variable inspection and stack frame queries via liblldb SB API.
 *
 *  Platform counterpart to SessionInspection.cpp (Windows dbgeng). Implements
 *  Session methods for DAP variables, scopes, stackTrace, and evaluate requests.
 *  Uses SBFrame::GetVariables for locals/args, SBValue tree for structured
 *  inspection, and SBFrame::EvaluateExpression for watch/hover evaluation.
 */

#include <JuceHeader.h>
#include "Session.h"
#include "PrettyPrint.h"
#include "../dap/Types.h"

#if JUCE_MAC

namespace debug
{

using dap::DynObj;

void Session::cacheFrameVariables (int frameIndex) noexcept
{
    if (cachedFrameIndex != frameIndex)
    {
        auto frame { process.GetSelectedThread ().GetFrameAtIndex (static_cast<std::uint32_t> (frameIndex)) };
        cachedFrameVariables = frame.GetVariables (true, true, true, true);
        cachedFrameIndex = frameIndex;
    }
}

static juce::String getDisplayValue (lldb::SBValue& value, const juce::String& typeName) noexcept
{
    const juce::String prettyValue { prettyPrint (value, typeName) };

    if (prettyValue.isNotEmpty ())
        return prettyValue;

    if (value.GetValue () != nullptr)
        return juce::String { value.GetValue () };

    if (value.GetSummary () != nullptr)
        return juce::String { value.GetSummary () };

    return juce::String ();
}

juce::var Session::getVariableObject (lldb::SBValue& value, int symbolIndex) noexcept
{
    const char* rawType { value.GetTypeName () };
    const juce::String typeName { rawType != nullptr ? rawType : "" };

    juce::String displayValue { getDisplayValue (value, typeName) };

    if (displayValue.isEmpty ())
    {
        displayValue = "<unavailable>";
    }

    DynObj obj { new juce::DynamicObject () };
    const char* rawName { value.GetName () };
    obj->setProperty ("name",        juce::String (rawName != nullptr ? rawName : ""));
    obj->setProperty ("value",       displayValue);
    obj->setProperty ("type",        typeName);
    obj->setProperty ("hasChildren", value.MightHaveChildren ());
    obj->setProperty ("symbolIndex", symbolIndex);

    return juce::var (obj);
}

static juce::var getStackFrame (lldb::SBFrame frame, int index) noexcept
{
    DynObj frameEntry { new juce::DynamicObject () };

    const char* rawName { frame.GetFunctionName () };
    frameEntry->setProperty ("id",   index);
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

    return juce::var (frameEntry);
}

juce::Array<juce::var> Session::getStackTrace (int maxFrames) noexcept
{
    juce::Array<juce::var> frames;
    auto thread { process.GetSelectedThread () };
    const int numFrames { static_cast<int> (thread.GetNumFrames ()) };
    const int clampedMax { juce::jmin (maxFrames, numFrames) };

    for (int i { 0 }; i < clampedMax; ++i)
        frames.add (getStackFrame (thread.GetFrameAtIndex (static_cast<std::uint32_t> (i)), i));

    return frames;
}

juce::Array<juce::var> Session::getLocals (int frameIndex) noexcept
{
    juce::Array<juce::var> result;

    cacheFrameVariables (frameIndex);

    for (std::uint32_t i { 0 }; i < cachedFrameVariables.GetSize (); ++i)
    {
        auto value { cachedFrameVariables.GetValueAtIndex (i) };
        const char* rawName { value.GetName () };
        const juce::String name { rawName != nullptr ? rawName : "" };

        if (not shouldSkipSymbol (name))
        {
            result.add (getVariableObject (value, static_cast<int> (i)));
        }
    }

    return result;
}

juce::Array<juce::var> Session::getVariableChildren (int frameIndex, int symbolIndex) noexcept
{
    juce::Array<juce::var> result;

    cacheFrameVariables (frameIndex);

    if (symbolIndex >= 0
        and symbolIndex < static_cast<int> (cachedFrameVariables.GetSize ()))
    {
        auto parent { cachedFrameVariables.GetValueAtIndex (static_cast<std::uint32_t> (symbolIndex)) };
        const std::uint32_t numChildren { parent.GetNumChildren () };

        for (std::uint32_t i { 0 }; i < numChildren; ++i)
        {
            auto child { parent.GetChildAtIndex (i) };
            const char* rawName { child.GetName () };
            const juce::String name { rawName != nullptr ? rawName : "" };

            if (not shouldSkipSymbol (name))
            {
                result.add (getVariableObject (child, static_cast<int> (i)));
            }
        }
    }

    return result;
}

juce::String Session::evaluateExpression (const juce::String& expression, int frameIndex) noexcept
{
    juce::String result;
    auto thread { process.GetSelectedThread () };
    auto frame { thread.GetFrameAtIndex (static_cast<std::uint32_t> (frameIndex)) };
    auto value { frame.EvaluateExpression (expression.toRawUTF8 ()) };

    if (value.IsValid () and value.GetError ().Success ())
    {
        const char* rawType { value.GetTypeName () };
        const juce::String typeName { rawType != nullptr ? rawType : "" };
        result = getDisplayValue (value, typeName);
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
