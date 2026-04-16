#include <JuceHeader.h>
#include "Session.h"
#include "PrettyPrint.h"

#if JUCE_MAC

namespace debug { namespace detail {

// ---------------------------------------------------------------------------
// parseHexAddress
// ---------------------------------------------------------------------------

/** Parse a hex address string (e.g. "0x7fff1234 <symbol>") into a numeric value.
 *
 *  Strips the leading "0x", trims any trailing annotation, and converts
 *  the remaining hex digits.  Returns 0 if the input is not a hex string.
 *
 *  @param text  Raw address text produced by SBValue::GetValue().
 *  @return Numeric address, or 0 if parsing fails.
 */
static std::uint64_t parseHexAddress (const juce::String& text) noexcept
{
    std::uint64_t address { 0 };

    if (text.startsWith ("0x"))
    {
        const juce::String hexPart { text.substring (2)
            .upToFirstOccurrenceOf (" ", false, false) };
        address = static_cast<std::uint64_t> (hexPart.getHexValue64 ());
    }

    return address;
}

// ---------------------------------------------------------------------------
// prettyPrintJuceString
// ---------------------------------------------------------------------------

/** Read a juce::String from the target process and return it as a quoted string.
 *
 *  Walks value.text.data to obtain the char pointer, reads the null-terminated
 *  UTF-8 content via SBProcess::ReadCStringFromMemory, and wraps it in quotes.
 *
 *  @param value  SBValue of type juce::String.
 *  @return Quoted content string (e.g. "hello"), or empty if the walk fails.
 */
static juce::String prettyPrintJuceString (lldb::SBValue& value) noexcept
{
    juce::String result;

    auto textChild { value.GetChildMemberWithName ("text") };
    auto dataChild { textChild.IsValid ()
        ? textChild.GetChildMemberWithName ("data") : lldb::SBValue {} };
    const char* rawAddr { dataChild.IsValid () ? dataChild.GetValue () : nullptr };
    const auto address { rawAddr != nullptr
        ? parseHexAddress (juce::String (rawAddr)) : std::uint64_t { 0 } };

    if (address != 0)
    {
        lldb::SBProcess proc { value.GetProcess () };

        static constexpr std::size_t maxStringReadSize { 256 };
        char buffer[maxStringReadSize] {};
        lldb::SBError readError;

        const std::size_t bytesRead { proc.ReadCStringFromMemory (
            static_cast<lldb::addr_t> (address),
            buffer, maxStringReadSize, readError) };

        if (not readError.Fail () and bytesRead > 0)
        {
            result = "\"" + juce::String (buffer) + "\"";
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// prettyPrintUniquePtr
// ---------------------------------------------------------------------------

/** Format a std::unique_ptr value as a pointer address or null.
 *
 *  Reads the libc++ internal `pointer` child to extract the stored address.
 *  Returns "null" if zero, or "0x<hex>" otherwise.
 *
 *  @param value  SBValue of type std::unique_ptr<T, ...>.
 *  @return "null" or hex address string, or empty if the walk fails.
 */
static juce::String prettyPrintUniquePtr (lldb::SBValue& value) noexcept
{
    juce::String result;

    auto ptrChild { value.GetChildMemberWithName ("pointer") };

    if (ptrChild.IsValid ())
    {
        const char* rawAddr { ptrChild.GetValue () };

        if (rawAddr != nullptr)
        {
            const auto address { parseHexAddress (juce::String (rawAddr)) };

            if (address == 0)
            {
                result = "null";
            }
            else
            {
                result = "0x" + juce::String::toHexString (
                    static_cast<juce::int64> (address));
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// prettyPrint
// ---------------------------------------------------------------------------

juce::String prettyPrint (lldb::SBValue& value, const juce::String& typeName) noexcept
{
    juce::String result;

    if (typeName.contains ("juce::String"))
    {
        result = prettyPrintJuceString (value);
    }
    else if (typeName.contains ("std::unique_ptr<"))
    {
        result = prettyPrintUniquePtr (value);
    }

    return result;
}

}} // namespace debug::detail

#endif // JUCE_MAC
