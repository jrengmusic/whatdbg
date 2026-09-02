/** @file SessionPrettyPrint.cpp
 *  @brief Windows pretty-print formatters for common C++ types via dbgeng.
 *
 *  Platform counterpart to SessionPrettyPrint_mac.cpp (macOS liblldb). Provides
 *  human-readable display strings for juce::String, std::unique_ptr, std::string,
 *  and other types whose raw memory layout needs interpretation for DAP display.
 */

#include <JuceHeader.h>
#include <cstring>
#include "Session.h"
#include "PrettyPrint.h"

#if JUCE_WINDOWS
#include <dbghelp.h>

namespace debug
{

static constexpr const char* dbgengDecimalPrefix { "0n" };

juce::String stripDecimalPrefix (const juce::String& input) noexcept
{
    juce::String result { input };
    int searchPos { 0 };

    while (searchPos < result.length () - 2)
    {
        const int found { result.indexOf (searchPos, dbgengDecimalPrefix) };

        if (found < 0)
        {
            searchPos = result.length ();
        }
        else
        {
            const int charAfter { found + 2 };

            if (charAfter < result.length () and juce::CharacterFunctions::isDigit (result[charAfter]))
            {
                result = result.substring (0, found) + result.substring (charAfter);
            }
            else
            {
                searchPos = found + 2;
            }
        }
    }

    return result;
}

juce::String formatSymbolValue (const juce::String& rawValue) noexcept
{
    juce::String result { rawValue };

    result = result.replace ("`", "");
    result = stripDecimalPrefix (result);

    if (result.startsWith ("0x"))
    {
        const int spacePos { result.indexOf (" ") };

        if (spacePos > 0)
        {
            result = result.substring (0, spacePos);
        }
    }

    if (result.startsWith ("class ") or result.startsWith ("struct "))
    {
        result = "";
    }

    return result;
}

juce::String readTargetString (IDebugDataSpaces4* dataSpaces, ULONG64 address) noexcept
{
    juce::String result;

    if (dataSpaces != nullptr and address != 0)
    {
        static constexpr ULONG maxStringReadSize { 256 };
        char buffer[maxStringReadSize] {};
        ULONG bytesRead { 0 };

        const HRESULT hr { dataSpaces->ReadMultiByteStringVirtual (
            address, maxStringReadSize, buffer, maxStringReadSize, &bytesRead) };

        if (SUCCEEDED (hr) and bytesRead > 0)
        {
            result = juce::String (buffer);
        }
    }

    return result;
}

int findChildByName (IDebugSymbolGroup2* group, ULONG parentIndex,
                     ULONG totalCount, const char* childName) noexcept
{
    for (ULONG i { parentIndex + 1 }; i < totalCount; ++i)
    {
        DEBUG_SYMBOL_PARAMETERS childParams {};
        const HRESULT hr { group->GetSymbolParameters (i, 1, &childParams) };

        if (SUCCEEDED (hr) and childParams.ParentSymbol == parentIndex)
        {
            static constexpr int childNameSize { 256 };
            char nameBuffer[childNameSize] {};
            group->GetSymbolName (i, nameBuffer, childNameSize, nullptr);

            if (std::strcmp (nameBuffer, childName) == 0)
                return static_cast<int> (i);
        }
    }

    return -1;
}

juce::String getChildValueText (IDebugSymbolGroup2* group, int index) noexcept
{
    juce::String result;

    if (index >= 0)
    {
        static constexpr int valueSize { 512 };
        char buffer[valueSize] {};
        const HRESULT hr { group->GetSymbolValueText (
            static_cast<ULONG> (index), buffer, valueSize, nullptr) };

        if (SUCCEEDED (hr))
        {
            result = juce::String (buffer);
        }
    }

    return result;
}

static int findExpandedChild (IDebugSymbolGroup2* group, ULONG parentIndex, const char* childName) noexcept
{
    group->ExpandSymbol (parentIndex, TRUE);
    ULONG count { 0 };
    group->GetNumberSymbols (&count);

    return findChildByName (group, parentIndex, count, childName);
}

static int findMyval2 (IDebugSymbolGroup2* group, ULONG symbolIndex) noexcept
{
    const int mypairIdx { findExpandedChild (group, symbolIndex, "_Mypair") };

    return mypairIdx >= 0 ? findExpandedChild (group, static_cast<ULONG> (mypairIdx), "_Myval2") : -1;
}

static juce::String prettyPrintJuceString (IDebugSymbolGroup2* group,
                                           IDebugDataSpaces4* dataSpaces,
                                           int symbolIndex) noexcept
{
    juce::String result;

    const int textIdx { findExpandedChild (group, static_cast<ULONG> (symbolIndex), "text") };

    if (textIdx >= 0)
    {
        const int dataIdx { findExpandedChild (group, static_cast<ULONG> (textIdx), "data") };

        if (dataIdx >= 0)
        {
            const juce::String dataValue { getChildValueText (group, dataIdx) };
            const std::uint64_t address { parseHexAddress (dataValue) };

            if (address != 0)
            {
                const juce::String content { readTargetString (dataSpaces, address) };
                result = "\"" + content + "\"";
            }
        }
    }

    return result;
}

static juce::String getStdStringText (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                                      int bxIdx, int stringSize) noexcept
{
    juce::String result;
    static constexpr int ssoThreshold { 16 };

    if (stringSize < ssoThreshold)
    {
        const int bufIdx { findExpandedChild (group, static_cast<ULONG> (bxIdx), "_Buf") };
        const juce::String bufValue { getChildValueText (group, bufIdx) };
        const int quoteStart { bufValue.indexOf ("\"") };
        const int quoteEnd { bufValue.lastIndexOf ("\"") };

        if (quoteStart >= 0 and quoteEnd > quoteStart)
            result = bufValue.substring (quoteStart, quoteEnd + 1);
    }
    else
    {
        const int ptrIdx { findExpandedChild (group, static_cast<ULONG> (bxIdx), "_Ptr") };
        const juce::String ptrValue { getChildValueText (group, ptrIdx) };
        const std::uint64_t address { parseHexAddress (ptrValue) };

        if (address != 0)
        {
            const juce::String content { readTargetString (dataSpaces, address) };
            result = "\"" + content + "\"";
        }
    }

    return result;
}

static juce::String prettyPrintStdString (IDebugSymbolGroup2* group,
                                          IDebugDataSpaces4* dataSpaces,
                                          int symbolIndex) noexcept
{
    juce::String result;

    const int myval2Idx { findMyval2 (group, static_cast<ULONG> (symbolIndex)) };

    if (myval2Idx >= 0)
    {
        const int sizeIdx { findExpandedChild (group, static_cast<ULONG> (myval2Idx), "_Mysize") };
        const juce::String sizeText { getChildValueText (group, sizeIdx) };
        const int stringSize { sizeText.replace (dbgengDecimalPrefix, "").getIntValue () };

        const int bxIdx { findExpandedChild (group, static_cast<ULONG> (myval2Idx), "_Bx") };

        if (bxIdx >= 0)
            result = getStdStringText (group, dataSpaces, bxIdx, stringSize);
    }

    return result;
}

static juce::String prettyPrintUniquePtr (IDebugSymbolGroup2* group, int symbolIndex) noexcept
{
    juce::String result;

    const int myval2Idx { findMyval2 (group, static_cast<ULONG> (symbolIndex)) };

    if (myval2Idx >= 0)
    {
        const juce::String ptrValue { getChildValueText (group, myval2Idx) };
        const std::uint64_t address { parseHexAddress (ptrValue) };

        result = address == 0 ? juce::String ("null")
                               : "0x" + juce::String::toHexString (static_cast<juce::int64> (address));
    }

    return result;
}

static juce::String getVectorSize (IDebugSymbolGroup2* group, IDebugSymbols3* symbols,
                                   int firstIdx, std::uint64_t firstAddr, std::uint64_t lastAddr) noexcept
{
    juce::String result;

    static constexpr int elemTypeSize { 256 };
    char elemTypeBuffer[elemTypeSize] {};
    group->GetSymbolTypeName (static_cast<ULONG> (firstIdx), elemTypeBuffer, elemTypeSize, nullptr);

    juce::String elemType { elemTypeBuffer };

    if (elemType.endsWith (" *"))
    {
        elemType = elemType.dropLastCharacters (2);
    }

    ULONG64 moduleBase { 0 };
    ULONG typeId { 0 };

    if (symbols != nullptr)
    {
        const HRESULT typeResult { symbols->GetSymbolTypeId (
            elemType.toRawUTF8 (), &typeId, &moduleBase) };

        if (SUCCEEDED (typeResult))
        {
            ULONG elemSize { 0 };
            const HRESULT sizeResult { symbols->GetTypeSize (
                moduleBase, typeId, &elemSize) };

            if (SUCCEEDED (sizeResult) and elemSize > 0)
            {
                const std::uint64_t byteCount { lastAddr - firstAddr };
                result = "size=" + juce::String (static_cast<juce::int64> (byteCount / elemSize));
            }
        }
    }

    if (result.isEmpty () and lastAddr > firstAddr)
    {
        const std::uint64_t byteCount { lastAddr - firstAddr };
        result = "size=" + juce::String (static_cast<juce::int64> (byteCount)) + " bytes";
    }

    return result;
}

static juce::String prettyPrintVector (IDebugSymbolGroup2* group,
                                       IDebugSymbols3* symbols,
                                       int symbolIndex) noexcept
{
    juce::String result;

    const int myval2Idx { findMyval2 (group, static_cast<ULONG> (symbolIndex)) };

    if (myval2Idx >= 0)
    {
        const int firstIdx { findExpandedChild (group, static_cast<ULONG> (myval2Idx), "_Myfirst") };
        const int lastIdx  { findExpandedChild (group, static_cast<ULONG> (myval2Idx), "_Mylast") };

        if (firstIdx >= 0 and lastIdx >= 0)
        {
            const juce::String firstValue { getChildValueText (group, firstIdx) };
            const juce::String lastValue  { getChildValueText (group, lastIdx) };

            const std::uint64_t firstAddr { parseHexAddress (firstValue) };
            const std::uint64_t lastAddr  { parseHexAddress (lastValue) };

            if (firstAddr == 0 and lastAddr == 0)
            {
                result = "size=0";
            }
            else if (lastAddr >= firstAddr)
            {
                result = getVectorSize (group, symbols, firstIdx, firstAddr, lastAddr);
            }
        }
    }

    return result;
}

juce::String prettyPrint (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                           IDebugSymbols3* symbols, int symbolIndex,
                           const juce::String& typeName) noexcept
{
    juce::String result;

    if (group != nullptr and dataSpaces != nullptr)
    {
        const std::vector<std::pair<juce::String, std::function<juce::String ()>>> formatters
        {
            { "juce::String",           [&] { return prettyPrintJuceString (group, dataSpaces, symbolIndex); } },
            { "std::basic_string<char", [&] { return prettyPrintStdString (group, dataSpaces, symbolIndex); } },
            { "std::unique_ptr<",       [&] { return prettyPrintUniquePtr (group, symbolIndex); } },
            { "std::vector<",           [&] { return prettyPrintVector (group, symbols, symbolIndex); } }
        };

        const auto matchedFormatter { std::find_if (formatters.begin (), formatters.end (),
            [&typeName] (const auto& entry)
            {
                const auto& [typeSubstring, formatter] { entry };
                return typeName.contains (typeSubstring);
            }) };

        if (matchedFormatter != formatters.end ())
        {
            const auto& [typeSubstring, formatter] { *matchedFormatter };
            result = formatter ();
        }
    }

    return result;
}

} // namespace debug

#endif // JUCE_WINDOWS
