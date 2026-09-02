#pragma once
#include <JuceHeader.h>

namespace dap
{

/** Asynchronous stdin reader that parses DAP messages and queues them for the main thread.
 *
 *  Runs a background JUCE thread that reads Content-Length framed JSON from stdin,
 *  parses each message into a juce::var, and enqueues it in a lock-free FIFO.
 *  The main thread drains the FIFO via tryPop() on each event-loop iteration.
 *
 *  Capacity is fixed at fifoCapacity slots. If the FIFO is full the background
 *  thread drops the message; in a debug build the drop is also logged.
 *
 *  @note start() must be called before the first tryPop(). ~Reader() is the sole
 *        owner of stop() — it joins the background thread cleanly on destruction.
 */
class Reader : private juce::Thread
{
public:
    /** Maximum number of parsed DAP messages that can be queued simultaneously. */
    static constexpr int fifoCapacity { 64 };

    Reader ();
    ~Reader () override;

    /** Start the background stdin-reading thread.
     *
     *  @note Must be called on the main thread before any tryPop() calls.
     */
    void start ();

    /** Pop one parsed DAP message from the FIFO.
     *
     *  Non-blocking. Returns immediately with a void juce::var if no message is available.
     *
     *  @return the dequeued message, or a void juce::var if the FIFO was empty.
     *
     *  @note Must be called on the main thread only.
     */
    juce::var tryPop () noexcept;

private:
    /** Signal the background thread to stop and wait for it to exit.
     *
     *  @note Blocks until the background thread has finished. Called exactly once,
     *        by ~Reader() — sole owner of the reader's teardown.
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
     *  @return the parsed message, or a void juce::var if the body was not valid JSON.
     */
    juce::var readMessage (int contentLength);

    /** Enqueue a parsed DAP message into the FIFO for the main thread to consume.
     *
     *  @param message  The parsed message to enqueue. Dropped (and, in a debug
     *                   build, logged) if the FIFO is full.
     */
    void addMessage (const juce::var& message);

    /** Tracks free/ready read and write regions of storage for the producer
     *  (background thread) and consumer (main thread) sides of tryPop().
     */
    juce::AbstractFifo fifo { fifoCapacity };

    /** Backing slots for queued messages, indexed by the ranges fifo hands out. */
    std::vector<juce::var> storage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Reader)
};

} // namespace dap
