#include "DapProtocol.hpp"

#include <sstream>
#include <string>

std::optional<nlohmann::json> DapProtocol::readMessage (std::istream& in)
{
    int contentLength { -1 };

    // Read headers line by line until blank line
    std::string line;
    while (std::getline (in, line))
    {
        // Strip trailing \r if present
        if (line.empty () == false and line.back () == '\r')
        {
            line.pop_back ();
        }

        // Blank line signals end of headers
        if (line.empty () == true)
        {
            break;
        }

        // Parse Content-Length header
        const std::string prefix { kContentLengthHeader };
        if (line.rfind (prefix, 0) == 0)
        {
            contentLength = std::stoi (line.substr (prefix.size ()));
        }
    }

    if (not in or contentLength < 0)
    {
        return std::nullopt;
    }

    // Read exactly contentLength bytes of JSON body
    std::string body (static_cast<size_t> (contentLength), '\0');
    if (not in.read (body.data (), contentLength))
    {
        return std::nullopt;
    }

    nlohmann::json message { nlohmann::json::parse (body, nullptr, /*allow_exceptions=*/false) };
    if (message.is_discarded () == true)
    {
        return std::nullopt;
    }

    return message;
}

void DapProtocol::writeMessage (std::ostream& out, const nlohmann::json& message)
{
    std::string body { message.dump () };
    std::string header { std::string (kContentLengthHeader)
                       + std::to_string (body.size ())
                       + kHeaderTerminator };

    std::lock_guard<std::mutex> lock (writeMutex);
    out.write (header.data (), static_cast<std::streamsize> (header.size ()));
    out.write (body.data (),   static_cast<std::streamsize> (body.size ()));
    out.flush ();
}
