/** @file Reader.cpp
 *  @brief DAP stdin reader — background thread parsing Content-Length-framed JSON.
 *
 *  Runs a dedicated thread that reads DAP messages from stdin following the
 *  Debug Adapter Protocol wire format (Content-Length header + JSON body).
 *  Parsed messages are queued into a thread-safe FIFO consumed by the main thread.
 */
#include <JuceHeader.h>
#include "Reader.h"

#if JUCE_WINDOWS
#include <fcntl.h>
#include <io.h>
#endif

namespace dap
{

Reader::Reader ()
    : juce::Thread { "dap::Reader" }
{
    storage.resize (fifoCapacity);
}

Reader::~Reader ()
{
    stop ();
}

void Reader::start ()
{
#if JUCE_WINDOWS
    // Windows stdin defaults to text mode (CRLF translation); DAP protocol is binary.
    _setmode (_fileno (stdin), _O_BINARY);
#endif
    startThread ();
}

void Reader::stop ()
{
    signalThreadShouldExit ();
    std::fclose (stdin);
    stopThread (2000);
}

std::optional<jam::JsonDocument> Reader::tryPop () noexcept
{
    std::optional<jam::JsonDocument> message;
    const auto scope { fifo.read (1) };

    if (scope.blockSize1 > 0)
        message.emplace (std::move (storage.at (static_cast<size_t> (scope.startIndex1))));

    return message;
}

int Reader::readContentLength ()
{
    int contentLength { -1 };
    bool headersComplete { false };

    while (not threadShouldExit () and not headersComplete and std::cin)
    {
        std::string line {};
        std::getline (std::cin, line);

        if (not line.empty () and line.back () == '\r')
            line.pop_back ();

        if (std::cin)
        {
            if (line.empty ())
            {
                headersComplete = true;
            }
            else
            {
                const std::string prefix { "Content-Length: " };

                if (line.rfind (prefix, 0) == 0)
                    contentLength = juce::String (line.substr (prefix.size ())).getIntValue ();
            }
        }
    }

    return contentLength;
}

std::unique_ptr<jam::JsonDocument> Reader::readMessage (int contentLength)
{
    std::string body (static_cast<size_t> (contentLength), '\0');
    std::cin.read (body.data (), contentLength);

    if (std::cin)
        return jam::JsonDocument::parse (std::string_view (body));

    return nullptr;
}

void Reader::addMessage (jam::JsonDocument&& message)
{
#if JUCE_DEBUG
    const auto& request { *message.getRoot ()->firstChild };
    const auto messageType { jam::Strings::fromView (message.getString (*message.find (request, jam::Strings::getView (Id::type.toString ())))) };
    const auto messageCommand { jam::Strings::fromView (message.getString (*message.find (request, jam::Strings::getView (Id::command.toString ())))) };
    jam::debug::Log::write ("[dap::Reader] parsed: type=" + messageType + " command=" + messageCommand);
#endif
    const auto scope { fifo.write (1) };

    if (scope.blockSize1 > 0)
    {
        storage.at (static_cast<size_t> (scope.startIndex1)) = std::move (message);
#if JUCE_DEBUG
        jam::debug::Log::write ("[dap::Reader] queued message");
#endif
    }
    else
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("[dap::Reader] DROPPED (FIFO full): command=" + messageCommand);
#endif
    }
}

void Reader::run ()
{
    bool isConnected { true };

    while (not threadShouldExit () and isConnected)
    {
        const int contentLength { readContentLength () };

        if (std::cin and contentLength >= 0)
        {
            auto message { readMessage (contentLength) };

            if (not std::cin)
            {
                isConnected = false;
            }
            else if (message != nullptr)
            {
                addMessage (std::move (*message));
            }
        }
        else
        {
            isConnected = false;
        }
    }
}

} // namespace dap
