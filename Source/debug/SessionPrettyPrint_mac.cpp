/** @file SessionPrettyPrint_mac.cpp
 *  @brief macOS pretty-print formatters for common C++ types via liblldb SB API.
 *
 *  Platform counterpart to SessionPrettyPrint.cpp (Windows dbgeng). Provides
 *  human-readable display strings for juce::String, std::unique_ptr, and other
 *  types that liblldb's default formatters don't handle well in DAP context.
 */

#include <JuceHeader.h>
#include "Session.h"
#include "PrettyPrint.h"

#if JUCE_MAC

namespace debug
{

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

static juce::String prettyPrintUniquePtr (lldb::SBValue& value) noexcept
{
    juce::String result;

    auto ptrChild { value.GetChildMemberWithName ("pointer") };

    if (not ptrChild.IsValid ())
    {
        auto ptrMember { value.GetChildMemberWithName ("__ptr_") };

        if (ptrMember.IsValid ())
        {
            ptrChild = ptrMember.GetChildMemberWithName ("__value_");
        }
    }

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

} // namespace debug

#endif // JUCE_MAC
