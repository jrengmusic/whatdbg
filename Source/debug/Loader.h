#pragma once
#include <JuceHeader.h>
#include <windows.h>
#include <dbgeng.h>

namespace debug
{

class Loader
{
public:
    Loader () = default;
    ~Loader ();

    // Load dbgeng.dll from the given directory. Returns true on success.
    bool load (const juce::File& sidecarDirectory) noexcept;

    // Call DebugCreate to obtain an IDebugClient5 interface.
    // Caller owns the returned pointer (must Release).
    HRESULT createDebugClient (IDebugClient5** outClient) const noexcept;

    bool isLoaded () const noexcept;

private:
    using DebugCreateFn = HRESULT (STDAPICALLTYPE*) (REFIID, PVOID*);

    HMODULE dbgengModule { nullptr };
    DebugCreateFn debugCreateFn { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Loader)
};

} // namespace debug
