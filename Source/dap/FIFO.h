#pragma once
#include <JuceHeader.h>

namespace dap
{

/** Fixed-capacity lane for one reader and one writer, built on juce::AbstractFifo.
 *
 *  The lane holds MessageType values in preallocated slots. Each slot is cleared after
 *  it is read. A write to a full lane asserts and drops the message.
 *
 *  @tparam MessageType  Type of the queued message. Default-constructible and movable.
 */
template <typename MessageType>
class FIFO
{
public:
    /** Allocates capacity slots.
     *
     *  @param capacity  Number of messages the lane holds.
     */
    explicit FIFO (int capacity)
        : fifo { capacity }
    {
        storage.resize (static_cast<size_t> (capacity));
    }

    /** Returns the number of slots that a write can fill.
     *
     *  @return Free slot count.
     */
    int getFreeSpace () const noexcept
    {
        return fifo.getFreeSpace ();
    }

    /** Stores message in the next free slot. Asserts and drops the message when the lane is full.
     *
     *  @param message  Message to store. Moved into the slot.
     */
    void write (MessageType message)
    {
        jassert (getFreeSpace () > 0);

        if (getFreeSpace () > 0)
        {
            const auto scope { fifo.write (1) };

            storage.at (static_cast<size_t> (scope.startIndex1)) = std::move (message);
        }
    }

    /** Calls callback with every message that is ready, in write order, then clears each slot.
     *
     *  @tparam Callback  Callable that takes a const MessageType&.
     *  @param callback   Receives each ready message.
     */
    template <typename Callback>
    void applyPendingData (Callback&& callback)
    {
        const auto scope { fifo.read (fifo.getNumReady ()) };

        scope.forEach ([this, &callback] (int index)
        {
            callback (std::as_const (storage.at (static_cast<size_t> (index))));
            storage.at (static_cast<size_t> (index)) = MessageType {};
        });
    }

private:
    juce::AbstractFifo fifo;
    std::vector<MessageType> storage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FIFO)
};

} // namespace dap
