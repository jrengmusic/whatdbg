/** @file SessionInspection.cpp
 *  @brief Windows variable inspection and stack frame queries via dbgeng COM API.
 *
 *  Platform counterpart to SessionInspection_mac.cpp (macOS liblldb). Implements
 *  Session methods for DAP variables, scopes, stackTrace, and evaluate requests
 *  using IDebugSymbols3 and IDebugControl4.
 */

#include <JuceHeader.h>
#include "Session.h"
#include "PrettyPrint.h"
#include "../dap/Types.h"

#if JUCE_WINDOWS
#include <dbghelp.h>

namespace debug
{

using dap::DynObj;

/** Accumulates dbgeng Execute output into a juce::String for in-process consumption. */
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

static juce::var getVariableObject (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                                      IDebugSymbols3* symbols, ULONG index,
                                      const juce::String& symbolName, bool hasChildren) noexcept
{
    static constexpr int symbolTypeSize  { 256 };
    static constexpr int symbolValueSize { 512 };

    char typeBuffer[symbolTypeSize] {};
    group->GetSymbolTypeName (index, typeBuffer, symbolTypeSize, nullptr);
    const juce::String typeName { typeBuffer };

    char valueBuffer[symbolValueSize] {};
    const HRESULT valueResult { group->GetSymbolValueText (index, valueBuffer, symbolValueSize, nullptr) };

    juce::String displayValue { SUCCEEDED (valueResult)
        ? formatSymbolValue (juce::String (valueBuffer))
        : juce::String ("<unavailable>") };

    const juce::String prettyValue { prettyPrint (group, dataSpaces, symbols, static_cast<int> (index), typeName) };

    if (prettyValue.isNotEmpty ())
        displayValue = prettyValue;

    DynObj variableObj { new juce::DynamicObject () };
    variableObj->setProperty ("name",        symbolName);
    variableObj->setProperty ("value",       displayValue);
    variableObj->setProperty ("type",        typeName);
    variableObj->setProperty ("symbolIndex", static_cast<int> (index));
    variableObj->setProperty ("hasChildren", hasChildren);

    return juce::var (variableObj);
}

static juce::Array<juce::var> getSymbols (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                                          IDebugSymbols3* symbols, ULONG parentFilter) noexcept
{
    static constexpr int symbolNameSize { 256 };

    juce::Array<juce::var> outVariables;
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

            if (not shouldSkipSymbol (symbolName))
            {
                outVariables.add (getVariableObject (group, dataSpaces, symbols, i,
                                                       symbolName, params.SubElements > 0));
            }
        }
    }

    return outVariables;
}

static juce::var getStackFrame (IDebugSymbols3* symbols, const DEBUG_STACK_FRAME& stackFrame,
                                  ULONG index) noexcept
{
    static constexpr int nameBufferSize { 512 };
    static constexpr int fileBufferSize { 1024 };

    DynObj frame { new juce::DynamicObject () };
    frame->setProperty ("id", static_cast<int> (index));
    frame->setProperty ("name", "frame");

    char nameBuffer[nameBufferSize] {};
    ULONG nameSize { 0 };
    ULONG64 displacement { 0 };

    const HRESULT nameResult { symbols->GetNameByOffset (
        stackFrame.InstructionOffset, nameBuffer, nameBufferSize, &nameSize, &displacement) };

    if (SUCCEEDED (nameResult))
        frame->setProperty ("name", juce::String (nameBuffer));

    char fileBuffer[fileBufferSize] {};
    ULONG fileSize { 0 };
    ULONG line { 0 };

    const HRESULT lineResult { symbols->GetLineByOffset (
        stackFrame.InstructionOffset, &line, fileBuffer, fileBufferSize, &fileSize, nullptr) };

    if (SUCCEEDED (lineResult))
    {
        DynObj source { new juce::DynamicObject () };
        source->setProperty ("name", juce::File (juce::String (fileBuffer)).getFileName ());
        source->setProperty ("path", juce::String (fileBuffer).replace ("\\", "/"));
        frame->setProperty ("source", juce::var (source));
        frame->setProperty ("line", static_cast<int> (line));
        frame->setProperty ("column", 1);
    }

    return juce::var (frame);
}

juce::Array<juce::var> Session::getStackTrace (int maxFrames) noexcept
{
    juce::Array<juce::var> frames;

    if (control != nullptr and symbols != nullptr)
    {
        static constexpr int maxStackFrames { 128 };
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
                frames.add (getStackFrame (symbols.Get (), stackFrames.at (static_cast<size_t> (i)), i));
        }
    }

    return frames;
}

juce::Array<juce::var> Session::getLocals (int frameIndex) noexcept
{
    juce::Array<juce::var> variables;

    IDebugSymbolGroup2* group { getOrCreateSymbolGroup (frameIndex) };

    if (group != nullptr)
    {
        variables = getSymbols (group, dataSpaces.Get (), symbols.Get (),
                                 DEBUG_ANY_ID);
    }

    return variables;
}

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
            variables = getSymbols (group, dataSpaces.Get (), symbols.Get (),
                                     parentIndex);
        }
    }

    return variables;
}

static juce::String evaluateStringValue (IDebugControl4*      secondaryControl,
                                          IDebugDataSpaces4*   dataSpaces,
                                          const juce::String&  expression) noexcept
{
    secondaryControl->SetExpressionSyntax (DEBUG_EXPR_CPLUSPLUS);

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

    juce::String resolved;

    if (SUCCEEDED (evalResult))
    {
        const ULONG64 address { dataValue.I64 };
        const juce::String content { readTargetString (dataSpaces, address) };
        resolved = "\"" + content + "\"";
    }

    return resolved;
}

static std::pair<Microsoft::WRL::ComPtr<IDebugClient>, Microsoft::WRL::ComPtr<IDebugControl4>>
    getOrCreateCaptureClient (IDebugClient5* client, CaptureOutputCallback& captureCallback) noexcept
{
    Microsoft::WRL::ComPtr<IDebugClient>   secondaryClient;
    Microsoft::WRL::ComPtr<IDebugControl4> secondaryControl;

    IDebugClient* rawSecondary { nullptr };
    const HRESULT createResult { client->CreateClient (&rawSecondary) };

    if (SUCCEEDED (createResult) and rawSecondary != nullptr)
    {
        secondaryClient.Attach (rawSecondary);
        secondaryClient->SetOutputMask (DEBUG_OUTPUT_NORMAL | DEBUG_OUTPUT_ERROR);
        secondaryClient->SetOutputCallbacks (&captureCallback);

        const HRESULT qiResult { secondaryClient->QueryInterface (
            __uuidof (IDebugControl4),
            reinterpret_cast<PVOID*> (secondaryControl.GetAddressOf ())) };

        juce::ignoreUnused (qiResult);
    }

    return { secondaryClient, secondaryControl };
}

static juce::String getExecutedOutput (IDebugControl4* secondaryControl,
                                       CaptureOutputCallback& captureCallback,
                                       const juce::String& expression) noexcept
{
    secondaryControl->Execute (
        DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_NOT_LOGGED,
        ".symopt- 100",
        DEBUG_EXECUTE_NOT_LOGGED | DEBUG_EXECUTE_NO_REPEAT);

    captureCallback.captured.clear ();

    const juce::String command { "?? " + expression };
    const HRESULT execResult { secondaryControl->Execute (
        DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_NOT_LOGGED,
        command.toRawUTF8 (),
        DEBUG_EXECUTE_NOT_LOGGED | DEBUG_EXECUTE_NO_REPEAT) };

    juce::ignoreUnused (execResult);

    const juce::String trimmed { captureCallback.captured.trim ().replace ("`", "") };
    return stripDecimalPrefix (trimmed);
}

juce::String Session::evaluateExpression (const juce::String& expression, int frameIndex) noexcept
{
    juce::String result;

    if (client != nullptr and symbols != nullptr)
    {
        symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));

        CaptureOutputCallback captureCallback;
        const auto [secondaryClient, secondaryControl] { getOrCreateCaptureClient (client.Get (), captureCallback) };

        if (secondaryClient != nullptr and secondaryControl != nullptr)
        {
            result = getExecutedOutput (secondaryControl.Get (), captureCallback, expression);

            if (result.contains ("juce::String") and control != nullptr and dataSpaces != nullptr)
            {
                const juce::String resolved { evaluateStringValue (
                    secondaryControl.Get (), dataSpaces.Get (), expression) };

                if (resolved.isNotEmpty ())
                    result = resolved;
            }

            secondaryClient->SetOutputCallbacks (nullptr);
        }

        symbols->ResetScope ();
    }

    return result;
}

} // namespace debug

#endif // JUCE_WINDOWS
