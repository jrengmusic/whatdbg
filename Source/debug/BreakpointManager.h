#pragma once
#include <JuceHeader.h>
#include "Session.h"
#include "../dap/Types.h"
#include <unordered_map>

namespace debug
{

struct BreakpointInfo
{
    uint32_t     dapId       { 0 };
    juce::String sourcePath;          // normalized (forward slash, lowercase)
    ULONG        line        { 0 };   // resolved line (may differ from requested)
    bool         isVerified  { false };
    bool         hasEngineId { false };
    ULONG        engineId    { 0 };
};

struct PendingBreakpoint
{
    uint32_t     dapId { 0 };
    juce::String sourcePath;          // Windows path (for getOffsetByLine)
    juce::String normalizedPath;      // for registry lookup
    ULONG        line { 0 };          // requested line
};

struct ResolveResult
{
    ULONG engineId     { 0 };
    ULONG resolvedLine { 0 };
    bool  isSuccess    { false };
};

class BreakpointManager
{
public:
    explicit BreakpointManager (Session& session);

    // Handle DAP setBreakpoints request. Returns the breakpoints array for the response.
    juce::Array<juce::var> handleSetBreakpoints (const juce::String& rawSourcePath,
                                                 const juce::var&    requestedBreakpoints);

    // Called when a module loads — resolve pending breakpoints.
    // Returns DAP events to emit (breakpoint changed events).
    juce::Array<juce::var> onModuleLoad ();

    // Called when a breakpoint is hit. Returns DAP stopped body.
    juce::var onBreakpointHit (ULONG engineId, ULONG threadId);

    bool hasPending () const noexcept;

private:
    ResolveResult tryResolve (const juce::String& windowsPath, ULONG requestedLine) noexcept;

    Session& session;

    std::unordered_map<uint32_t, BreakpointInfo>                          breakpoints;       // dapId -> info
    std::unordered_map<std::string, std::unordered_map<int, uint32_t>>    sourceBreakpoints; // path -> line -> dapId
    std::unordered_map<ULONG, uint32_t>                                   engineToDap;       // engineId -> dapId
    juce::Array<PendingBreakpoint>                                        pending;

    uint32_t nextDapId { 1 };

    static constexpr ULONG kLineSearchWindow { 4 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakpointManager)
};

} // namespace debug
