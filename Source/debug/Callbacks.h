#pragma once
#include <JuceHeader.h>
#include <windows.h>
#include <dbgeng.h>

namespace debug
{

// OutputCallbacks receives debug output from the target process.
// Registered via IDebugClient5::SetOutputCallbacks.
//
// Must implement IDebugOutputCallbacks2 because dbgeng routes through
// Output2 when the callbacks QI for IDebugOutputCallbacks2.
class OutputCallbacks : public IDebugOutputCallbacks2
{
public:
    OutputCallbacks () = default;

    // IUnknown
    STDMETHOD_ (ULONG, AddRef) () override;
    STDMETHOD_ (ULONG, Release) () override;
    STDMETHOD (QueryInterface) (REFIID interfaceId, PVOID* outInterface) override;

    // IDebugOutputCallbacks
    STDMETHOD (Output) (ULONG mask, PCSTR text) override;

    // IDebugOutputCallbacks2
    STDMETHOD (GetInterestMask) (PULONG mask) override;
    STDMETHOD (Output2) (ULONG which, ULONG flags, ULONG64 arg, PCWSTR text) override;

private:
    ULONG refCount { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputCallbacks)
};

//==============================================================================

class EventCallbacks : public IDebugEventCallbacks
{
public:
    EventCallbacks () = default;

    // IUnknown
    STDMETHOD_ (ULONG, AddRef) () override;
    STDMETHOD_ (ULONG, Release) () override;
    STDMETHOD (QueryInterface) (REFIID interfaceId, PVOID* outInterface) override;

    // IDebugEventCallbacks
    STDMETHOD (GetInterestMask) (PULONG mask) override;
    STDMETHOD (Breakpoint) (PDEBUG_BREAKPOINT bp) override;
    STDMETHOD (Exception) (PEXCEPTION_RECORD64 exception, ULONG firstChance) override;
    STDMETHOD (CreateThread) (ULONG64 handle, ULONG64 dataOffset, ULONG64 startOffset) override;
    STDMETHOD (ExitThread) (ULONG exitCode) override;
    STDMETHOD (CreateProcess) (ULONG64 imageFileHandle, ULONG64 handle, ULONG64 baseOffset,
                               ULONG moduleSize, PCSTR moduleName, PCSTR imageName,
                               ULONG checkSum, ULONG timeDateStamp,
                               ULONG64 initialThreadHandle, ULONG64 threadDataOffset,
                               ULONG64 startOffset) override;
    STDMETHOD (ExitProcess) (ULONG exitCode) override;
    STDMETHOD (LoadModule) (ULONG64 imageFileHandle, ULONG64 baseOffset,
                            ULONG moduleSize, PCSTR moduleName, PCSTR imageName,
                            ULONG checkSum, ULONG timeDateStamp) override;
    STDMETHOD (UnloadModule) (PCSTR imageBaseName, ULONG64 baseOffset) override;
    STDMETHOD (SystemError) (ULONG error, ULONG level) override;
    STDMETHOD (SessionStatus) (ULONG status) override;
    STDMETHOD (ChangeDebuggeeState) (ULONG flags, ULONG64 argument) override;
    STDMETHOD (ChangeEngineState) (ULONG flags, ULONG64 argument) override;
    STDMETHOD (ChangeSymbolState) (ULONG flags, ULONG64 argument) override;

private:
    ULONG refCount { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EventCallbacks)
};

} // namespace debug
