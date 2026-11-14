#pragma once
#include <JuceHeader.h>

namespace dap
{

/** Reader thread that parses DAP messages from stdin and passes each one to onMessage.
 *
 *  Runs one background JUCE thread. The thread reads Content-Length framed messages:
 *  it reads the header block, then the body, and parses the body as JSON. A body that
 *  does not parse is skipped. The thread ends when stdin fails or reaches its end, or
 *  when stop is signalled.
 *
 *  onMessage runs on the reader thread. Whatdbg sets it to call Processor::addRequest.
 *
 *  @note ~Reader() is the sole caller of stop(). It joins the background thread
 *        on destruction.
 */
class Reader : private juce::Thread
{
public:
    /** Creates the thread object. The thread does not run until start is called. */
    Reader ();

    /** Stops the thread and waits for it to end. */
    ~Reader () override;

    /** Start the background stdin-reading thread.
     *
     *  On Windows, switches stdin to binary mode first, so that CRLF is not translated.
     *
     *  @note Call it after onMessage is set. Whatdbg::initialise calls it on the message thread.
     */
    void start ();

    /** Receives each parsed message, on the reader thread. */
    std::function<void (jam::JsonDocument)> onMessage;

private:
    /** Signal the background thread to exit, close stdin, and wait up to 2000 ms for the thread to end.
     *
     *  Closing stdin unblocks a read that is waiting for input. Only ~Reader() calls it.
     */
    void stop ();

    /** Background thread entry point — reads and parses stdin until signalled to stop. */
    void run () override;

    /** Read and parse the DAP Content-Length header from stdin.
     *
     *  Blocks on std::getline until the blank line terminating the header block,
     *  or until the background thread is signalled to stop.
     *
     *  @return the parsed Content-Length value, or -1 if no header was found.
     */
    int readContentLength ();

    /** Read contentLength bytes of DAP message body from stdin and parse as JSON.
     *
     *  @param contentLength  Number of bytes to read, as reported by the Content-Length header.
     *  @return the parsed document from jam::JsonDocument::parse when stdin is still good, otherwise nullptr.
     */
    std::unique_ptr<jam::JsonDocument> readMessage (int contentLength);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Reader)
};

} // namespace dap
