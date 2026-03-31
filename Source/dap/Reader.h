#pragma once
#include <JuceHeader.h>

#include <vector>

namespace dap
{

class Reader : private juce::Thread
{
public:
    static constexpr int kFifoCapacity { 64 };

    Reader ();
    ~Reader () override;

    void start ();
    void stop ();

    // Main thread calls this to pop a message. Returns true if a message was available.
    bool tryPop (juce::var& outMessage) noexcept;

private:
    void run () override;

    juce::AbstractFifo fifo { kFifoCapacity };
    std::vector<juce::var> storage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Reader)
};

} // namespace dap
