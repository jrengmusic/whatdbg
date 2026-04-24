/** @file SessionPrettyPrint.cpp
 *  @brief Windows pretty-print formatters for common C++ types via dbgeng.
 *
 *  Platform counterpart to SessionPrettyPrint_mac.cpp (macOS liblldb). Provides
 *  human-readable display strings for juce::String, std::unique_ptr, std::string,
 *  and other types whose raw memory layout needs interpretation for DAP display.
 */

#include <JuceHeader.h>
#include "Session.h"
#include "PrettyPrint.h"
#include "../Log.h"

#if JUCE_WINDOWS
#include <dbghelp.h>

namespace debug { namespace detail {

// ---------------------------------------------------------------------------
// stripDecimalPrefix
// ---------------------------------------------------------------------------

/** Removes dbgeng's "0n" decimal prefix from integer literals wherever it
 *  appears in the string (e.g. "0n42" → "42"). */
juce::String stripDecimalPrefix (const juce::String& input) noexcept
{
    juce::String result { input };
    int searchPos { 0 };

    while (searchPos < result.length () - 2)
    {
        const int found { result.indexOf (searchPos, "0n") };

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

// ---------------------------------------------------------------------------
// formatSymbolValue
// ---------------------------------------------------------------------------

/** Normalizes a raw dbgeng symbol value string: removes backtick address separators,
 *  strips the "0n" prefix, truncates pointer values after the first space, and
 *  clears composite type echoes ("class …", "struct …"). */
juce::String formatSymbolValue (const juce::String& rawValue) noexcept
{
    juce::String result { rawValue };

    // Clean 64-bit pointer backtick: "0x00000000`10db01b0" -> "0x0000000010db01b0"
    result = result.replace ("`", "");

    // Strip dbgeng decimal prefix "0n" when followed by a digit (anywhere in string).
    result = stripDecimalPrefix (result);

    // Pointer with trailing type: "0x0000000010550c30 class Foo *" -> "0x0000000010550c30"
    if (result.startsWith ("0x"))
    {
        const int spacePos { result.indexOf (" ") };

        if (spacePos > 0)
        {
            result = result.substring (0, spacePos);
        }
    }

    // Composite type echoed as value: "class juce::String" or "struct Foo" -> empty
    // The type column already identifies it; expand triangle indicates expandability.
    if (result.startsWith ("class ") or result.startsWith ("struct "))
    {
        result = "";
    }

    return result;
}

// ---------------------------------------------------------------------------
// readTargetString
// ---------------------------------------------------------------------------

/** Reads a null-terminated UTF-8 string from the target process address space
 *  via IDebugDataSpaces4::ReadMultiByteStringVirtual (up to 256 bytes). */
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

// ---------------------------------------------------------------------------
// parseHexAddress
// ---------------------------------------------------------------------------

/** Strips backtick separators from a dbgeng hex address string and converts
 *  the "0x…" portion (up to the first space) to a ULONG64. */
ULONG64 parseHexAddress (const juce::String& valueText) noexcept
{
    ULONG64 address { 0 };

    const juce::String cleaned { valueText.replace ("`", "").trim () };

    if (cleaned.startsWith ("0x"))
    {
        const juce::String hexPart { cleaned.substring (2).upToFirstOccurrenceOf (" ", false, false) };
        address = static_cast<ULONG64> (hexPart.getHexValue64 ());
    }

    return address;
}

// ---------------------------------------------------------------------------
// findChildByName
// ---------------------------------------------------------------------------

/** Scans group symbols past parentIndex to find the first child whose
 *  ParentSymbol equals parentIndex and whose name matches childName. */
int findChildByName (IDebugSymbolGroup2* group, ULONG parentIndex,
                     ULONG totalCount, const char* childName) noexcept
{
    int foundIndex { -1 };

    for (ULONG i { parentIndex + 1 }; i < totalCount; ++i)
    {
        DEBUG_SYMBOL_PARAMETERS childParams {};
        const HRESULT hr { group->GetSymbolParameters (i, 1, &childParams) };

        if (SUCCEEDED (hr))
        {
            if (childParams.ParentSymbol == parentIndex)
            {
                static constexpr int childNameSize { 256 };
                char nameBuffer[childNameSize] {};
                group->GetSymbolName (i, nameBuffer, childNameSize, nullptr);

                if (strcmp (nameBuffer, childName) == 0)
                {
                    foundIndex = static_cast<int> (i);
                }
            }
        }

        if (foundIndex >= 0)
        {
            i = totalCount;
        }
    }

    return foundIndex;
}

// ---------------------------------------------------------------------------
// getChildValueText
// ---------------------------------------------------------------------------

/** Returns the raw value text of the symbol at index via GetSymbolValueText,
 *  or an empty string if index is negative or the call fails. */
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

// ---------------------------------------------------------------------------
// prettyPrint — per-type formatters
// ---------------------------------------------------------------------------

/** Walks juce::String's text.data pointer child and reads target memory to
 *  produce a quoted string display value. */
static juce::String prettyPrintJuceString (IDebugSymbolGroup2* group,
                                           IDebugDataSpaces4* dataSpaces,
                                           int symbolIndex) noexcept
{
    juce::String result;

    group->ExpandSymbol (static_cast<ULONG> (symbolIndex), TRUE);
    ULONG count { 0 };
    group->GetNumberSymbols (&count);

    const int textIdx { findChildByName (group, static_cast<ULONG> (symbolIndex), count, "text") };

    if (textIdx >= 0)
    {
        group->ExpandSymbol (static_cast<ULONG> (textIdx), TRUE);
        group->GetNumberSymbols (&count);

        const int dataIdx { findChildByName (group, static_cast<ULONG> (textIdx), count, "data") };

        if (dataIdx >= 0)
        {
            const juce::String dataValue { getChildValueText (group, dataIdx) };
            const ULONG64 address { parseHexAddress (dataValue) };

            if (address != 0)
            {
                const juce::String content { readTargetString (dataSpaces, address) };
                result = "\"" + content + "\"";
            }
        }
    }

    return result;
}

/** Reads MSVC std::string internals (_Mypair → _Myval2 → _Mysize/_Bx) and
 *  returns a quoted string, using SSO buffer for size < 16 or heap pointer otherwise. */
static juce::String prettyPrintStdString (IDebugSymbolGroup2* group,
                                          IDebugDataSpaces4* dataSpaces,
                                          int symbolIndex) noexcept
{
    juce::String result;

    group->ExpandSymbol (static_cast<ULONG> (symbolIndex), TRUE);
    ULONG count { 0 };
    group->GetNumberSymbols (&count);

    const int mypairIdx { findChildByName (group, static_cast<ULONG> (symbolIndex), count, "_Mypair") };

    if (mypairIdx >= 0)
    {
        group->ExpandSymbol (static_cast<ULONG> (mypairIdx), TRUE);
        group->GetNumberSymbols (&count);

        const int myval2Idx { findChildByName (group, static_cast<ULONG> (mypairIdx), count, "_Myval2") };

        if (myval2Idx >= 0)
        {
            group->ExpandSymbol (static_cast<ULONG> (myval2Idx), TRUE);
            group->GetNumberSymbols (&count);

            const int sizeIdx { findChildByName (group, static_cast<ULONG> (myval2Idx), count, "_Mysize") };
            const juce::String sizeText { getChildValueText (group, sizeIdx) };
            const int stringSize { sizeText.replace ("0n", "").getIntValue () };

            const int bxIdx { findChildByName (group, static_cast<ULONG> (myval2Idx), count, "_Bx") };

            if (bxIdx >= 0)
            {
                static constexpr int ssoThreshold { 16 };

                group->ExpandSymbol (static_cast<ULONG> (bxIdx), TRUE);
                group->GetNumberSymbols (&count);

                if (stringSize < ssoThreshold)
                {
                    const int bufIdx { findChildByName (group, static_cast<ULONG> (bxIdx), count, "_Buf") };
                    const juce::String bufValue { getChildValueText (group, bufIdx) };
                    const int quoteStart { bufValue.indexOf ("\"") };
                    const int quoteEnd { bufValue.lastIndexOf ("\"") };

                    if (quoteStart >= 0 and quoteEnd > quoteStart)
                    {
                        result = bufValue.substring (quoteStart, quoteEnd + 1);
                    }
                }
                else
                {
                    const int ptrIdx { findChildByName (group, static_cast<ULONG> (bxIdx), count, "_Ptr") };
                    const juce::String ptrValue { getChildValueText (group, ptrIdx) };
                    const ULONG64 address { parseHexAddress (ptrValue) };

                    if (address != 0)
                    {
                        const juce::String content { readTargetString (dataSpaces, address) };
                        result = "\"" + content + "\"";
                    }
                }
            }
        }
    }

    return result;
}

/** Reads MSVC std::unique_ptr internals (_Mypair → _Myval2) to produce "null"
 *  or a hex address display string. */
static juce::String prettyPrintUniquePtr (IDebugSymbolGroup2* group, int symbolIndex) noexcept
{
    juce::String result;

    group->ExpandSymbol (static_cast<ULONG> (symbolIndex), TRUE);
    ULONG count { 0 };
    group->GetNumberSymbols (&count);

    const int mypairIdx { findChildByName (group, static_cast<ULONG> (symbolIndex), count, "_Mypair") };

    if (mypairIdx >= 0)
    {
        group->ExpandSymbol (static_cast<ULONG> (mypairIdx), TRUE);
        group->GetNumberSymbols (&count);

        const int myval2Idx { findChildByName (group, static_cast<ULONG> (mypairIdx), count, "_Myval2") };
        const juce::String ptrValue { getChildValueText (group, myval2Idx) };
        const ULONG64 address { parseHexAddress (ptrValue) };

        if (address == 0)
        {
            result = "null";
        }
        else
        {
            result = "0x" + juce::String::toHexString (static_cast<juce::int64> (address));
        }
    }

    return result;
}

/** Reads MSVC std::vector internals (_Myfirst/_Mylast pointers) and computes
 *  element count via GetTypeSize; falls back to byte count if type lookup fails. */
static juce::String prettyPrintVector (IDebugSymbolGroup2* group,
                                       IDebugSymbols3* symbols,
                                       int symbolIndex) noexcept
{
    juce::String result;

    group->ExpandSymbol (static_cast<ULONG> (symbolIndex), TRUE);
    ULONG count { 0 };
    group->GetNumberSymbols (&count);

    const int mypairIdx { findChildByName (group, static_cast<ULONG> (symbolIndex), count, "_Mypair") };

    if (mypairIdx >= 0)
    {
        group->ExpandSymbol (static_cast<ULONG> (mypairIdx), TRUE);
        group->GetNumberSymbols (&count);

        const int myval2Idx { findChildByName (group, static_cast<ULONG> (mypairIdx), count, "_Myval2") };

        if (myval2Idx >= 0)
        {
            group->ExpandSymbol (static_cast<ULONG> (myval2Idx), TRUE);
            group->GetNumberSymbols (&count);

            const int firstIdx { findChildByName (group, static_cast<ULONG> (myval2Idx), count, "_Myfirst") };
            const int lastIdx  { findChildByName (group, static_cast<ULONG> (myval2Idx), count, "_Mylast") };

            if (firstIdx >= 0 and lastIdx >= 0)
            {
                const juce::String firstValue { getChildValueText (group, firstIdx) };
                const juce::String lastValue  { getChildValueText (group, lastIdx) };

                const ULONG64 firstAddr { parseHexAddress (firstValue) };
                const ULONG64 lastAddr  { parseHexAddress (lastValue) };

                if (firstAddr == 0 and lastAddr == 0)
                {
                    result = "size=0";
                }
                else if (lastAddr >= firstAddr)
                {
                    static constexpr int elemTypeSize { 256 };
                    char elemTypeBuffer[elemTypeSize] {};
                    group->GetSymbolTypeName (static_cast<ULONG> (firstIdx),
                                             elemTypeBuffer, elemTypeSize, nullptr);

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
                                const ULONG64 byteCount { lastAddr - firstAddr };
                                const ULONG64 elementCount { byteCount / elemSize };
                                result = "size=" + juce::String (static_cast<juce::int64> (elementCount));
                            }
                        }
                    }

                    // Fallback: byte count if type size lookup failed
                    if (result.isEmpty () and lastAddr > firstAddr)
                    {
                        const ULONG64 byteCount { lastAddr - firstAddr };
                        result = "size=" + juce::String (static_cast<juce::int64> (byteCount)) + " bytes";
                    }
                }
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// prettyPrint
// ---------------------------------------------------------------------------

/** Dispatches to the appropriate per-type formatter based on typeName.
 *  Returns an empty string if no formatter matches or group/dataSpaces is null. */
juce::String prettyPrint (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                           IDebugSymbols3* symbols, int symbolIndex,
                           const juce::String& typeName) noexcept
{
    juce::String result;

    if (group != nullptr and dataSpaces != nullptr)
    {
        if (typeName.contains ("juce::String"))
        {
            result = prettyPrintJuceString (group, dataSpaces, symbolIndex);
        }
        else if (typeName.contains ("std::basic_string<char"))
        {
            result = prettyPrintStdString (group, dataSpaces, symbolIndex);
        }
        else if (typeName.contains ("std::unique_ptr<"))
        {
            result = prettyPrintUniquePtr (group, symbolIndex);
        }
        else if (typeName.contains ("std::vector<"))
        {
            result = prettyPrintVector (group, symbols, symbolIndex);
        }
    }

    return result;
}

}} // namespace debug::detail

#endif // JUCE_WINDOWS
