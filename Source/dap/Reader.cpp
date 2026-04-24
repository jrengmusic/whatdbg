/** @file Reader.cpp
 *  @brief DAP stdin reader — background thread parsing Content-Length-framed JSON.
 *
 *  Runs a dedicated thread that reads DAP messages from stdin following the
 *  Debug Adapter Protocol wire format (Content-Length header + JSON body).
 *  Parsed messages are queued into a thread-safe FIFO consumed by the main thread.
 */
#include <JuceHeader.h>
#include "Reader.h"
#include "../Log.h"

#if JUCE_WINDOWS
#include <fcntl.h>
#include <io.h>
#endif
#include <iostream>
#include <string>

namespace dap
{

/** @brief Constructs the reader and preallocates the FIFO backing storage. */
Reader::Reader ()
    : juce::Thread { "dap::Reader" }
{
    storage.resize (fifoCapacity);
}

/** @brief Stops the background thread before destruction. */
Reader::~Reader ()
{
    stop ();
}

/** @brief Switches stdin to binary mode on Windows and starts the reader thread. */
void Reader::start ()
{
#if JUCE_WINDOWS
    // Windows stdin defaults to text mode (CRLF translation); DAP protocol is binary.
    _setmode (_fileno (stdin), _O_BINARY);
#endif
    startThread ();
}

/** @brief Signals the thread to exit, closes stdin to unblock any pending read, and joins. */
void Reader::stop ()
{
    signalThreadShouldExit ();

#if JUCE_WINDOWS
    if (_fileno (stdin) >= 0)
        std::fclose (stdin);
#else
    if (fileno (stdin) >= 0)
        std::fclose (stdin);
#endif

    stopThread (2000);
}

/** @brief Attempts a non-blocking dequeue of the next parsed DAP message.
 *  @return True if a message was dequeued into @p outMessage, false if the FIFO was empty.
 */
bool Reader::tryPop (juce::var& outMessage) noexcept
{
    bool hasMessage { false };
    const auto scope { fifo.read (1) };

    if (scope.blockSize1 > 0)
    {
        outMessage = storage.at (static_cast<size_t> (scope.startIndex1));
        hasMessage = true;
    }

    return hasMessage;
}

/** @brief Thread body — reads Content-Length-framed DAP messages from stdin and enqueues them.
 *
 *  Each iteration parses the header block (Content-Length line + blank separator),
 *  reads the exact body byte count, parses the JSON, and pushes into the FIFO.
 *  Drops messages silently when the FIFO is full and logs the drop.
 *  Exits when @c threadShouldExit() is set or stdin reaches EOF/error.
 */
void Reader::run ()
{
    bool isConnected { true };

    while (not threadShouldExit () and isConnected)
    {
        int contentLength { -1 };
        bool headersComplete { false };

        while (not threadShouldExit () and not headersComplete and isConnected)
        {
            std::string line {};
            std::getline (std::cin, line);

            if (not line.empty () and line.back () == '\r')
                line.pop_back ();

            if (not std::cin)
            {
                isConnected = false;
            }
            else if (line.empty ())
            {
                headersComplete = true;
            }
            else
            {
                const std::string prefix { "Content-Length: " };

                if (line.rfind (prefix, 0) == 0)
                    contentLength = std::stoi (line.substr (prefix.size ()));
            }
        }

        if (isConnected and std::cin and contentLength >= 0)
        {
            std::string body (static_cast<size_t> (contentLength), '\0');
            std::cin.read (body.data (), contentLength);

            if (not std::cin)
            {
                isConnected = false;
            }
            else
            {
                juce::var message { juce::JSON::parse (
                    juce::String (body.data (), static_cast<size_t> (contentLength))) };

                if (not message.isVoid ())
                {
                    logWrite ("[dap::Reader] parsed: type=%s command=%s length=%d\n",
                              message["type"].toString ().toRawUTF8 (),
                              message["command"].toString ().toRawUTF8 (),
                              contentLength);

                    const auto scope { fifo.write (1) };

                    if (scope.blockSize1 > 0)
                    {
                        storage.at (static_cast<size_t> (scope.startIndex1)) = message;
                        logWrite ("[dap::Reader] queued message\n");
                    }
                    else
                    {
                        logWrite ("[dap::Reader] DROPPED (FIFO full): command=%s\n",
                                  message["command"].toString ().toRawUTF8 ());
                    }
                }
                else
                {
                    logWrite ("[dap::Reader] JSON parse failed, contentLength=%d\n", contentLength);
                }
            }
        }
        else if (isConnected and (not std::cin or contentLength < 0))
        {
            isConnected = false;
        }
    }
}

} // namespace dap
