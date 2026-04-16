#include <JuceHeader.h>
#include "Session.h"
#include "State.h"
#include "PrettyPrint.h"
#include "../dap/Types.h"
#include "../Log.h"

#if JUCE_WINDOWS
#include <dbghelp.h>

namespace debug
{

using dap::DynObj;

// ---------------------------------------------------------------------------
// CaptureOutputCallback — minimal IDebugOutputCallbacks for Execute capture
// ---------------------------------------------------------------------------

class CaptureOutputCallback : public IDebugOutputCallbacks
{
public:
    juce::String captured;

    STDMETHOD_ (ULONG, AddRef) () override  { return 1; }
    STDMETHOD_ (ULONG, Release) () override { return 1; }

    STDMETHOD (QueryInterface) (REFIID interfaceId, PVOID* outInterface) override
    {
        HRESULT result { E_NOINTERFACE };
        *outInterface = nullptr;

        if (IsEqualIID (interfaceId, __uuidof (IUnknown))
            or IsEqualIID (interfaceId, __uuidof (IDebugOutputCallbacks)))
        {
            *outInterface = static_cast<IDebugOutputCallbacks*> (this);
            result = S_OK;
        }

        return result;
    }

    STDMETHOD (Output) (ULONG /*mask*/, PCSTR text) override
    {
        captured += text;
        return S_OK;
    }
};

// ---------------------------------------------------------------------------
// enumerateSymbols — shared symbol iteration for getLocals / getVariableChildren
// ---------------------------------------------------------------------------

static void enumerateSymbols (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                              IDebugSymbols3* symbols, ULONG parentFilter,
                              juce::Array<juce::var>& outVariables) noexcept
{
    static constexpr int symbolNameSize  { 256 };
    static constexpr int symbolTypeSize  { 256 };
    static constexpr int symbolValueSize { 512 };

    ULONG count { 0 };
    group->GetNumberSymbols (&count);

    for (ULONG i { 0 }; i < count; ++i)
    {
        DEBUG_SYMBOL_PARAMETERS params {};
        const HRESULT paramResult { group->GetSymbolParameters (i, 1, &params) };

        if (SUCCEEDED (paramResult) and params.ParentSymbol == parentFilter)
        {
            char nameBuffer[symbolNameSize] {};
            group->GetSymbolName (i, nameBuffer, symbolNameSize, nullptr);

            const juce::String symbolName { nameBuffer };

            if (not symbolName.startsWithChar ('<')
                and not symbolName.startsWith ("leakDetector")
                and not symbolName.startsWith ("__vfptr"))
            {
                char typeBuffer[symbolTypeSize] {};
                group->GetSymbolTypeName (i, typeBuffer, symbolTypeSize, nullptr);

                char valueBuffer[symbolValueSize] {};
                const HRESULT valueResult { group->GetSymbolValueText (
                    i, valueBuffer, symbolValueSize, nullptr) };

                juce::String displayValue { SUCCEEDED (valueResult)
                    ? detail::formatSymbolValue (juce::String (valueBuffer))
                    : juce::String ("<unavailable>") };

                const juce::String typeName { typeBuffer };
                const juce::String prettyValue { detail::prettyPrint (
                    group, dataSpaces, symbols, static_cast<int> (i), typeName) };

                if (prettyValue.isNotEmpty ())
                {
                    displayValue = prettyValue;
                }

                DynObj var { new juce::DynamicObject () };
                var->setProperty ("name",        symbolName);
                var->setProperty ("value",       displayValue);
                var->setProperty ("type",        typeName);
                var->setProperty ("symbolIndex", static_cast<int> (i));
                var->setProperty ("hasChildren", params.SubElements > 0);

                outVariables.add (juce::var (var));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Session::getStackTrace
// ---------------------------------------------------------------------------

juce::Array<juce::var> Session::getStackTrace (int maxFrames) noexcept
{
    juce::Array<juce::var> frames;

    if (control != nullptr and symbols != nullptr)
    {
        static constexpr int maxStackFrames { 128 };
        static constexpr int nameBufferSize { 512 };
        static constexpr int fileBufferSize { 1024 };
        const int frameCount { juce::jmin (maxFrames, maxStackFrames) };

        std::vector<DEBUG_STACK_FRAME> stackFrames (static_cast<size_t> (frameCount));
        ULONG framesFilled { 0 };

        const HRESULT hr { control->GetStackTrace (
            0, 0, 0,
            stackFrames.data (),
            static_cast<ULONG> (frameCount),
            &framesFilled) };

        if (SUCCEEDED (hr))
        {
            for (ULONG i { 0 }; i < framesFilled; ++i)
            {
                DynObj frame { new juce::DynamicObject () };
                frame->setProperty ("id", static_cast<int> (i));
                frame->setProperty ("name", "frame");

                // Resolve function name
                char nameBuffer[nameBufferSize] {};
                ULONG nameSize { 0 };
                ULONG64 displacement { 0 };

                const HRESULT nameResult { symbols->GetNameByOffset (
                    stackFrames.at (static_cast<size_t> (i)).InstructionOffset,
                    nameBuffer,
                    nameBufferSize,
                    &nameSize,
                    &displacement) };

                if (SUCCEEDED (nameResult))
                {
                    frame->setProperty ("name", juce::String (nameBuffer));
                }

                // Resolve source location
                char fileBuffer[fileBufferSize] {};
                ULONG fileSize { 0 };
                ULONG line { 0 };

                const HRESULT lineResult { symbols->GetLineByOffset (
                    stackFrames.at (static_cast<size_t> (i)).InstructionOffset,
                    &line,
                    fileBuffer,
                    fileBufferSize,
                    &fileSize,
                    nullptr) };

                if (SUCCEEDED (lineResult))
                {
                    DynObj source { new juce::DynamicObject () };
                    source->setProperty ("name", juce::File (juce::String (fileBuffer)).getFileName ());
                    source->setProperty ("path", juce::String (fileBuffer).replace ("\\", "/"));
                    frame->setProperty ("source", juce::var (source));
                    frame->setProperty ("line", static_cast<int> (line));
                    frame->setProperty ("column", 1);
                }

                frames.add (juce::var (frame));
            }
        }
    }

    return frames;
}

// ---------------------------------------------------------------------------
// Session::getLocals
// ---------------------------------------------------------------------------

juce::Array<juce::var> Session::getLocals (int frameIndex) noexcept
{
    juce::Array<juce::var> variables;

    IDebugSymbolGroup2* group { getOrCreateSymbolGroup (frameIndex) };

    if (group != nullptr)
    {
        enumerateSymbols (group, dataSpaces.Get (), symbols.Get (),
                          DEBUG_ANY_ID, variables);
    }

    return variables;
}

// ---------------------------------------------------------------------------
// Session::getVariableChildren
// ---------------------------------------------------------------------------

juce::Array<juce::var> Session::getVariableChildren (int frameIndex, int symbolIndex) noexcept
{
    juce::Array<juce::var> variables;

    IDebugSymbolGroup2* group { getOrCreateSymbolGroup (frameIndex) };

    if (group != nullptr)
    {
        const ULONG parentIndex { static_cast<ULONG> (symbolIndex) };
        const HRESULT expandResult { group->ExpandSymbol (parentIndex, TRUE) };

        if (SUCCEEDED (expandResult))
        {
            enumerateSymbols (group, dataSpaces.Get (), symbols.Get (),
                              parentIndex, variables);
        }
    }

    return variables;
}

// ---------------------------------------------------------------------------
// Session::evaluateExpression
// ---------------------------------------------------------------------------

juce::String Session::evaluateExpression (const juce::String& expression, int frameIndex) noexcept
{
    juce::String result;

    if (client != nullptr and symbols != nullptr)
    {
        symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));

        Microsoft::WRL::ComPtr<IDebugClient> secondaryClient;
        IDebugClient* rawSecondary { nullptr };
        const HRESULT createResult { client->CreateClient (&rawSecondary) };

        if (SUCCEEDED (createResult) and rawSecondary != nullptr)
        {
            secondaryClient.Attach (rawSecondary);

            CaptureOutputCallback captureCallback;
            secondaryClient->SetOutputMask (DEBUG_OUTPUT_NORMAL | DEBUG_OUTPUT_ERROR);
            secondaryClient->SetOutputCallbacks (&captureCallback);

            Microsoft::WRL::ComPtr<IDebugControl4> secondaryControl;
            const HRESULT qiResult { secondaryClient->QueryInterface (
                __uuidof (IDebugControl4),
                reinterpret_cast<PVOID*> (secondaryControl.GetAddressOf ())) };

            if (SUCCEEDED (qiResult) and secondaryControl != nullptr)
            {
                // Enable unqualified symbol resolution for local variable names
                secondaryControl->Execute (
                    DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_NOT_LOGGED,
                    ".symopt- 100",
                    DEBUG_EXECUTE_NOT_LOGGED | DEBUG_EXECUTE_NO_REPEAT);

                // Clear any output from .symopt
                captureCallback.captured.clear ();

                const juce::String command { "?? " + expression };
                const HRESULT execResult { secondaryControl->Execute (
                    DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_NOT_LOGGED,
                    command.toRawUTF8 (),
                    DEBUG_EXECUTE_NOT_LOGGED | DEBUG_EXECUTE_NO_REPEAT) };

                juce::ignoreUnused (execResult);

                // ?? output format differs from GetSymbolValueText — type comes first.
                // Only strip backticks and 0n prefix, not pointer/composite rules.
                result = captureCallback.captured.trim ().replace ("`", "");
                result = detail::stripDecimalPrefix (result);

                // juce::String pretty-print: resolve actual text content
                if (result.contains ("juce::String") and control != nullptr and dataSpaces != nullptr)
                {
                    secondaryControl->SetExpressionSyntax (DEBUG_EXPR_CPLUSPLUS);

                    // Try dot access first (value type), then arrow (pointer)
                    const juce::String dotExpr   { "(" + expression + ").text.data" };
                    const juce::String arrowExpr { "(" + expression + ")->text.data" };

                    DEBUG_VALUE dataValue {};
                    HRESULT evalResult { secondaryControl->Evaluate (
                        dotExpr.toRawUTF8 (), DEBUG_VALUE_INT64, &dataValue, nullptr) };

                    if (not SUCCEEDED (evalResult))
                    {
                        evalResult = secondaryControl->Evaluate (
                            arrowExpr.toRawUTF8 (), DEBUG_VALUE_INT64, &dataValue, nullptr);
                    }

                    if (SUCCEEDED (evalResult))
                    {
                        const ULONG64 address { dataValue.I64 };
                        const juce::String content { detail::readTargetString (dataSpaces.Get (), address) };
                        result = "\"" + content + "\"";
                    }
                }
            }
            secondaryClient->SetOutputCallbacks (nullptr);
        }

        symbols->ResetScope ();
    }

    return result;
}

} // namespace debug

#endif // JUCE_WINDOWS
