#pragma once

#include <iostream>
#include <mutex>
#include <optional>
#include <string>

#include "../third_party/nlohmann/json.hpp"

class DapProtocol
{
public:
    static constexpr const char* kContentLengthHeader { "Content-Length: " };
    static constexpr const char* kHeaderTerminator    { "\r\n\r\n" };
    static constexpr const char* kLineEnding          { "\r\n" };

    // Read one DAP message from the stream.
    // Returns parsed JSON body, or std::nullopt on EOF or parse error.
    std::optional<nlohmann::json> readMessage (std::istream& in);

    // Write one DAP message to the stream.
    // Thread-safe: guarded by writeMutex.
    void writeMessage (std::ostream& out, const nlohmann::json& message);

private:
    std::mutex writeMutex;
};
