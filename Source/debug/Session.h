#pragma once
#include <JuceHeader.h>
#include <windows.h>
#include <wrl/client.h>
#include <dbgeng.h>
#include "Loader.h"
#include "Callbacks.h"

namespace debug
{

class Session
{
public:
    Session () = default;
    ~Session ();

    // Initialize COM, load dbgeng DLLs, create client, QI interfaces, register callbacks.
    bool initialize (const juce::File& sidecarDir) noexcept;

    // Launch a process for debugging. Does NOT call WaitForEvent.
    bool launch (const juce::String& program) noexcept;

    // Attach to a running process by PID. Does NOT call WaitForEvent.
    bool attach (ULONG processId) noexcept;

    // Resume execution after a break.
    void resume () noexcept;

    // Poll for debug events with given timeout (ms).
    HRESULT pollEvents (ULONG timeoutMs) noexcept;

    // Detach and clean up.
    void shutdown (bool shouldTerminate = false) noexcept;

    // ── Breakpoint API (used by BreakpointManager) ─────────────────────

    // Resolve source:line to an address. Returns S_OK on success.
    HRESULT getOffsetByLine (const juce::String& filePath, ULONG line, ULONG64* outOffset) noexcept;

    // Reverse lookup: address to source:line. Returns S_OK on success.
    HRESULT getLineByOffset (ULONG64 offset, juce::String& outFilePath, ULONG* outLine) noexcept;

    // Create a code breakpoint at the given address. Returns engine ID.
    HRESULT addBreakpoint (ULONG64 offset, ULONG* outEngineId) noexcept;

    // Remove a breakpoint by engine ID.
    HRESULT removeBreakpoint (ULONG engineId) noexcept;

    // Force-reload symbols for a specific module (".reload /f <imageName>").
    HRESULT loadModuleSymbols (const juce::String& imageName) noexcept;

    // Force-reload symbols for ALL loaded modules (".reload /f").
    HRESULT forceReloadAllSymbols () noexcept;

    // Stepping — source-level (requires SetCodeLevel(DEBUG_LEVEL_SOURCE) at init)
    void stepOver () noexcept;
    void stepInto () noexcept;
    void stepOut () noexcept;

    // Interrupt a running target.
    void interrupt (ULONG processId) noexcept;

    void appendSymbolPath (const juce::String& path) noexcept;
    void appendSourcePath (const juce::String& path) noexcept;

    // Stack trace — returns frames as DAP-formatted juce::var array
    juce::Array<juce::var> getStackTrace (int maxFrames) noexcept;

    // Get local variables at the given frame index.
    // Returns array of {name, value, type, variablesReference, symbolIndex} objects.
    juce::Array<juce::var> getLocals (int frameIndex) noexcept;

    // Get children of an expanded variable at the given frame and symbol index.
    juce::Array<juce::var> getVariableChildren (int frameIndex, int symbolIndex) noexcept;

    // Evaluate a C++ expression in the context of the given frame.
    // Returns the formatted result string from dbgeng's "??" evaluator.
    juce::String evaluateExpression (const juce::String& expression, int frameIndex) noexcept;

    // Enumerate all threads. Returns array of {id (OS TID), name} objects.
    juce::Array<juce::var> getThreads () noexcept;

    // Get the OS TID of the thread that triggered the last debug event.
    ULONG getEventThreadSystemId () noexcept;

    // Set the current thread context by OS TID (for stack trace / scope).
    void setCurrentThreadBySystemId (ULONG systemId) noexcept;

    // Invalidate cached symbol group (call on every stop event).
    void resetSymbolGroupCache () noexcept;

private:
    // Returns the cached symbol group for the given frame, creating or updating as needed.
    IDebugSymbolGroup2* getOrCreateSymbolGroup (int frameIndex) noexcept;

    Loader           loader;
    OutputCallbacks  outputCallbacks;
    EventCallbacks   eventCallbacks;

    Microsoft::WRL::ComPtr<IDebugClient5>       client;
    Microsoft::WRL::ComPtr<IDebugControl4>      control;
    Microsoft::WRL::ComPtr<IDebugSymbols3>      symbols;
    Microsoft::WRL::ComPtr<IDebugDataSpaces4>   dataSpaces;
    Microsoft::WRL::ComPtr<IDebugSystemObjects> systemObjects;

    IDebugSymbolGroup2* cachedSymbolGroup { nullptr };
    int                 cachedFrameIndex  { -1 };

    bool isComOwned { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

} // namespace debug
