#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
#include <dbgeng.h>
#endif // JUCE_WINDOWS

#if JUCE_MAC
#include <lldb/API/LLDB.h>
#endif // JUCE_MAC

namespace debug
{

/** Return true if a symbol/variable name should be hidden from the DAP variable panel.
 *
 *  Filters out compiler- and framework-injected symbols that carry no user-visible
 *  meaning: RTTI/vtable artifacts, the JUCE leak detector member, and the per-TU
 *  compile-unit-mismatch sentinel.
 *
 *  @param name  Symbol or variable name as reported by the debug engine.
 *  @return true if the symbol should be skipped.
 */
inline bool shouldSkipSymbol (const juce::String& name) noexcept
{
    return name.startsWithChar ('<')
        or name.startsWith ("leakDetector")
        or name.startsWith ("__vfptr")
        or name == "juce::compileUnitMismatchSentinel";
}

/** Extract a hexadecimal address from a debug engine value text string.
 *
 *  Scans the input for a "0x" prefixed hex token and parses it as a 64-bit
 *  unsigned integer. Used to extract pointer values from formatted symbol
 *  text such as "0x0000000010db01b0 \"hello\"". Backtick digit-group padding
 *  (dbgeng's 64-bit address formatting) is stripped before parsing.
 *
 *  @param valueText  Symbol value text that may contain a hex address.
 *  @return Parsed address, or 0 if no hex address is found.
 */
inline std::uint64_t parseHexAddress (const juce::String& valueText) noexcept
{
    const juce::String cleaned { valueText.replace ("`", "").trim () };
    std::uint64_t address { 0 };

    if (cleaned.startsWith ("0x"))
    {
        const juce::String hexPart { cleaned.substring (2).upToFirstOccurrenceOf (" ", false, false) };
        address = static_cast<std::uint64_t> (hexPart.getHexValue64 ());
    }

    return address;
}

#if JUCE_WINDOWS

/** Strip the dbgeng "0n" decimal prefix from a numeric value string.
 *
 *  dbgeng formats decimal integers with a "0n" prefix (e.g. "0n877"). This
 *  function removes those prefixes to produce clean decimal output suitable
 *  for display in a DAP variable panel.
 *
 *  Examples:
 *  - "0n877"      -> "877"
 *  - "mid (0n2)"  -> "mid (2)"
 *  - "0x1A"       -> "0x1A"  (hex unchanged)
 *
 *  @param input  Raw value string from dbgeng.
 *  @return String with all "0n" decimal prefixes removed.
 */
juce::String stripDecimalPrefix (const juce::String& input) noexcept;

/** Format a raw symbol value string for display in the DAP variable panel.
 *
 *  Applies a sequence of transformations to produce a clean, readable value:
 *  - Removes backtick padding inserted by dbgeng address formatting.
 *  - Strips "0n" decimal prefixes via stripDecimalPrefix.
 *  - Trims trailing type information that dbgeng appends (e.g. " [type: int]").
 *  - Suppresses composite-type echoes where dbgeng repeats the type name as
 *    the value (e.g. "class Foo", "struct Bar") — replaced with an empty string
 *    so the type column carries the type information instead.
 *
 *  @param rawValue  Raw value text as returned by IDebugSymbolGroup2::GetSymbolValueText.
 *  @return Cleaned value string suitable for the DAP "value" field.
 */
juce::String formatSymbolValue (const juce::String& rawValue) noexcept;

/** Read a null-terminated narrow string from the target process's virtual memory.
 *
 *  Reads bytes from the target address space at address via IDebugDataSpaces4::ReadVirtual,
 *  stopping at the first null byte or after a fixed maximum read length.
 *
 *  @param dataSpaces  dbgeng data spaces interface for reading target memory.
 *  @param address     Virtual address of the first character in the target process.
 *  @return String containing the target's null-terminated string, or empty on failure.
 *
 *  @note Reads only narrow (char) strings. Wide string support is not implemented.
 *  @note Returns an empty string if address is null or the read fails.
 */
juce::String readTargetString (IDebugDataSpaces4* dataSpaces, ULONG64 address) noexcept;

/** Find a child symbol by name within an expanded symbol group.
 *
 *  Searches the symbol group between parentIndex+1 and totalCount for a symbol
 *  whose name matches childName. Intended for navigating into known struct fields
 *  (e.g. finding "m_data" inside a std::string expansion).
 *
 *  @param group        The symbol group containing the expanded parent.
 *  @param parentIndex  Index of the parent symbol in the group.
 *  @param totalCount   Total number of symbols currently in the group.
 *  @param childName    Null-terminated name of the child symbol to find.
 *  @return Zero-based index of the matching child symbol, or -1 if not found.
 */
int findChildByName (IDebugSymbolGroup2* group, ULONG parentIndex,
                     ULONG totalCount, const char* childName) noexcept;

/** Get the formatted value text of a symbol at the given group index.
 *
 *  Calls IDebugSymbolGroup2::GetSymbolValueText for the symbol at index and
 *  returns the result as a juce::String.
 *
 *  @param group  The symbol group containing the symbol.
 *  @param index  Zero-based index of the symbol in the group.
 *  @return Value text string, or empty on failure.
 */
juce::String getChildValueText (IDebugSymbolGroup2* group, int index) noexcept;

/** Attempt to pretty-print a symbol using its known type name.
 *
 *  Dispatches to type-specific formatters for types such as std::string,
 *  std::wstring, juce::String, and pointer types. Expands the symbol group
 *  as needed (via IDebugSymbolGroup2::ExpandSymbol) to access struct internals.
 *  All ExpandSymbol calls modify the provided group object persistently.
 *
 *  @param group        Symbol group containing the symbol to pretty-print.
 *  @param dataSpaces   dbgeng data spaces interface for reading target memory.
 *  @param symbols      dbgeng symbols interface for type lookups.
 *  @param symbolIndex  Zero-based index of the symbol in the group.
 *  @param typeName     Type name string from dbgeng (e.g. "std::basic_string<char,...>").
 *  @return Formatted display string, or empty if the type is not recognized.
 *
 *  @note Returns empty for unrecognized types — caller should fall back to
 *        formatSymbolValue on the raw value text.
 */
juce::String prettyPrint (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                           IDebugSymbols3* symbols, int symbolIndex,
                           const juce::String& typeName) noexcept;
#endif // JUCE_WINDOWS

#if JUCE_MAC

/** Attempt to pretty-print an SBValue using its known type name.
 *
 *  Dispatches to type-specific formatters for types such as juce::String and
 *  std::unique_ptr. Returns empty if the type is not recognized — callers
 *  should fall back to SBValue::GetValue() / SBValue::GetSummary().
 *
 *  @param value     The SBValue to format. May be mutated by child walks.
 *  @param typeName  Type name string from LLDB (e.g. "juce::String").
 *  @return Formatted display string, or empty if the type is not recognized.
 */
juce::String prettyPrint (lldb::SBValue& value, const juce::String& typeName) noexcept;
#endif // JUCE_MAC

} // namespace debug
