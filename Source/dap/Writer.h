#pragma once
#include <JuceHeader.h>

namespace dap
{

/** Writes DAP messages to stdout. Owned by Whatdbg and used only on the message thread. */
class Writer
{
public:
    /** On Windows, switches stdout to binary mode, so that CRLF is not translated. */
    Writer ();

    /** Writes a Content-Length header and the body to stdout, then flushes stdout.
     *
     *  Does not check the state of the stream.
     *
     *  @param json  DAP message body, a JSON object, without the header.
     */
    void writeMessage (std::string_view json) noexcept;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Writer)
};

} // namespace dap
