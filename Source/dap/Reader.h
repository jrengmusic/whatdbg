#pragma once
#include <JuceHeader.h>

#include <vector>

namespace dap
{

/** Asynchronous stdin reader that parses DAP messages and queues them for the main thread.
 *
 *  Runs a background JUCE thread that reads Content-Length framed JSON from stdin,
 *  parses each message into a juce::var, and enqueues it in a lock-free FIFO.
 *  The main thread drains the FIFO via tryPop() on each event-loop iteration.
 *
 *  Capacity is fixed at fifoCapacity slots. If the FIFO is full the background
 *  thread blocks until space is available.
 *
 *  @note start() must be called before the first tryPop(). stop() must be called
 *        before destruction to join the background thread cleanly.
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

    /** Signal the background thread to stop and wait for it to exit.
     *
     *  @note Blocks until the background thread has finished. Safe to call from
     *        the main thread during shutdown.
     */
    void stop ();

    /** Pop one parsed DAP message from the FIFO.
     *
     *  Non-blocking. Returns immediately with false if no message is available.
     *
     *  @param outMessage  Receives the parsed message if one was available.
     *  @return true if a message was dequeued into outMessage, false if the FIFO was empty.
     *
     *  @note Must be called on the main thread only.
     */
    bool tryPop (juce::var& outMessage) noexcept;

private:
    /** Background thread entry point — reads and parses stdin until signalled to stop. */
    void run () override;

    juce::AbstractFifo fifo { fifoCapacity };
    std::vector<juce::var> storage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Reader)
};

} // namespace dap
