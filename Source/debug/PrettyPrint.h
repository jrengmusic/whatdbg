#pragma once
#include <JuceHeader.h>
#include <dbgeng.h>

namespace debug { namespace detail {

// Strip dbgeng "0n" decimal prefix from a value string.
// "0n877" -> "877", "mid (0n2)" -> "mid (2)"
juce::String stripDecimalPrefix (const juce::String& input) noexcept;

// Format a raw symbol value text for display.
// Removes backtick padding, strips 0n prefix, trims trailing type info,
// and suppresses composite-type echoes ("class Foo", "struct Bar").
juce::String formatSymbolValue (const juce::String& rawValue) noexcept;

// Read a null-terminated string from target process memory at the given address.
juce::String readTargetString (IDebugDataSpaces4* dataSpaces, ULONG64 address) noexcept;

// Extract a hex address from a symbol value text like "0x0000000010db01b0".
ULONG64 parseHexAddress (const juce::String& valueText) noexcept;

// Find a child symbol by name within an expanded parent.
// Returns the child's index in the group, or -1 if not found.
int findChildByName (IDebugSymbolGroup2* group, ULONG parentIndex,
                     ULONG totalCount, const char* childName) noexcept;

// Get the value text of a symbol at the given index in the group.
juce::String getChildValueText (IDebugSymbolGroup2* group, int index) noexcept;

// Pretty-print a known type.
// Returns empty string if the type is not recognized.
// Operates on the provided cached group — ExpandSymbol calls persist in the group.
juce::String prettyPrint (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                           IDebugSymbols3* symbols, int symbolIndex,
                           const juce::String& typeName) noexcept;

}} // namespace debug::detail
