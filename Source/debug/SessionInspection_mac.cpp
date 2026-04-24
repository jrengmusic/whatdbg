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

// ---------------------------------------------------------------------------
// shouldSkipSymbol
// ---------------------------------------------------------------------------

/** Returns true for internal or compiler-generated symbols that should be hidden from DAP responses.
 *  Filters JUCE leak detectors, vtable pointers, anonymous compiler temporaries, and sentinel symbols.
 */
static bool shouldSkipSymbol (const juce::String& name) noexcept
{
    return name.startsWithChar ('<')
        or name.startsWith ("leakDetector")
        or name.startsWith ("__vfptr")
        or name == "juce::compileUnitMismatchSentinel";
}

// ---------------------------------------------------------------------------
// Session::ensureFrameVariablesCache
// ---------------------------------------------------------------------------

/** Populates cachedFrameVariables from the selected thread's frame at frameIndex if not already cached.
 *  GetVariables(true,true,true,true) fetches locals, args, statics, and in-scope variables.
 */
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

/** Converts a single SBValue into a DAP-compatible DynObj with name, value, type, hasChildren, and symbolIndex.
 *  Prefers prettyPrint output over raw GetValue/GetSummary; falls back to "<unavailable>" on empty.
 */
juce::var Session::makeVariableDynObj (lldb::SBValue& value, int symbolIndex) noexcept
{
    const char* rawType { value.GetTypeName () };
    const juce::String typeName { rawType != nullptr ? rawType : "" };

    const char* rawVal { value.GetValue () };
    juce::String displayValue { rawVal != nullptr
        ? juce::String (rawVal)
        : (value.GetSummary () != nullptr
            ? juce::String (value.GetSummary ())
            : juce::String ()) };

    const juce::String prettyValue { detail::prettyPrint (value, typeName) };

    if (prettyValue.isNotEmpty ())
    {
        displayValue = prettyValue;
    }

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

// ---------------------------------------------------------------------------
// Session::getStackTrace
// ---------------------------------------------------------------------------

/** Returns up to maxFrames DAP stackFrame objects from the selected thread.
 *  Each entry includes id, name, and a source sub-object with file name and path from DWARF line entries.
 */
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

/** Returns DAP variable objects for all non-skipped locals and args in the given frame.
 *  Delegates to ensureFrameVariablesCache then filters via shouldSkipSymbol.
 */
juce::Array<juce::var> Session::getLocals (int frameIndex) noexcept
{
    juce::Array<juce::var> result;

    ensureFrameVariablesCache (frameIndex);

    for (std::uint32_t i { 0 }; i < cachedFrameVariables.GetSize (); ++i)
    {
        auto value { cachedFrameVariables.GetValueAtIndex (i) };
        const char* rawName { value.GetName () };
        const juce::String name { rawName != nullptr ? rawName : "" };

        if (not shouldSkipSymbol (name))
        {
            result.add (makeVariableDynObj (value, static_cast<int> (i)));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::getVariableChildren
// ---------------------------------------------------------------------------

/** Expands the SBValue at symbolIndex and returns DAP objects for its non-skipped children.
 *  Used by the DAP variables request when the client expands a structured value.
 */
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
            const char* rawName { child.GetName () };
            const juce::String name { rawName != nullptr ? rawName : "" };

            if (not shouldSkipSymbol (name))
            {
                result.add (makeVariableDynObj (child, static_cast<int> (i)));
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::evaluateExpression
// ---------------------------------------------------------------------------

/** Evaluates an arbitrary expression in the given frame via SBFrame::EvaluateExpression.
 *  Returns prettyPrint output, raw value, summary, or an error string on failure.
 */
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
        const juce::String prettyValue { detail::prettyPrint (value, typeName) };

        if (prettyValue.isNotEmpty ())
        {
            result = prettyValue;
        }
        else if (value.GetValue () != nullptr)
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
