/** @file Writer.cpp
 *  @brief Writes one DAP message to stdout, behind a Content-Length header.
 */

#include <JuceHeader.h>
#include "Writer.h"

#if JUCE_WINDOWS
#include <fcntl.h>
#include <io.h>
#endif

namespace dap
{

Writer::Writer ()
{
#if JUCE_WINDOWS
    _setmode (_fileno (stdout), _O_BINARY);
#endif
}

void Writer::writeMessage (std::string_view json) noexcept
{
    const std::string header { "Content-Length: " + std::to_string (json.size ()) + "\r\n\r\n" };

    std::cout.write (header.data (), static_cast<std::streamsize> (header.size ()));
    std::cout.write (json.data (), static_cast<std::streamsize> (json.size ()));
    std::cout.flush ();
}

} // namespace dap
