#include <JuceHeader.h>
#include "Reader.h"
#include "../Log.h"

#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <string>

namespace dap
{

Reader::Reader ()
    : juce::Thread { "dap::Reader" }
{
    storage.resize (kFifoCapacity);
}

Reader::~Reader ()
{
    stop ();
}

void Reader::start ()
{
    _setmode (_fileno (stdin), _O_BINARY);
    startThread ();
}

void Reader::stop ()
{
    signalThreadShouldExit ();
    stopThread (2000);
}

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
