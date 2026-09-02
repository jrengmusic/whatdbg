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

juce::var Reader::tryPop () noexcept
{
    juce::var message;
    const auto scope { fifo.read (1) };

    if (scope.blockSize1 > 0)
        message = storage.at (static_cast<size_t> (scope.startIndex1));

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

juce::var Reader::readMessage (int contentLength)
{
    std::string body (static_cast<size_t> (contentLength), '\0');
    std::cin.read (body.data (), contentLength);

    juce::var message;

    if (std::cin)
    {
        message = juce::JSON::parse (juce::String (body.data (), static_cast<size_t> (contentLength)));

        if (not message.isVoid ())
        {
#if JUCE_DEBUG
            jam::debug::Log::write ("[dap::Reader] parsed: type=" + message["type"].toString ()
                                     + " command=" + message["command"].toString ()
                                     + " length=" + juce::String (contentLength));
#endif
        }
        else
        {
#if JUCE_DEBUG
            jam::debug::Log::write ("[dap::Reader] JSON parse failed, contentLength="
                                     + juce::String (contentLength));
#endif
        }
    }

    return message;
}

void Reader::addMessage (const juce::var& message)
{
    const auto scope { fifo.write (1) };

    if (scope.blockSize1 > 0)
    {
        storage.at (static_cast<size_t> (scope.startIndex1)) = message;
#if JUCE_DEBUG
        jam::debug::Log::write ("[dap::Reader] queued message");
#endif
    }
    else
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("[dap::Reader] DROPPED (FIFO full): command="
                                 + message["command"].toString ());
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
            const juce::var message { readMessage (contentLength) };

            if (not std::cin)
            {
                isConnected = false;
            }
            else if (not message.isVoid ())
            {
                addMessage (message);
            }
        }
        else
        {
            isConnected = false;
        }
    }
}

} // namespace dap
