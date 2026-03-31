#pragma once
#include <JuceHeader.h>
#include <windows.h>

namespace debug
{

enum class ExecutionState : int
{
    idle      = 0,
    launching = 1,
    running   = 2,
    stopped   = 3,
    exited    = 4
};

class State : public jreng::Context<State>
{
public:
    State () = default;

    // ── Execution ──────────────────────────────────────────────────────
    ExecutionState executionState { ExecutionState::idle };
    bool isInitialBreakSeen { false };
    int processExitCode { 0 };

    // ── Deferred events (set by COM callbacks, consumed by main loop) ──
    // Breakpoint hit: stored body for DAP stopped event
    juce::var pendingStoppedBody;

    // Breakpoint hit: engine ID and thread ID for stopped event
    bool  hasBreakpointHit      { false };
    ULONG breakpointEngineId    { 0 };
    ULONG breakpointThreadId    { 0 };

    // Module load: flag for deferred BP resolution
    bool hasNewModuleLoaded { false };

    // Pending breakpoints: flag for LoadModule to decide whether to break
    bool hasPendingBreakpoints { false };

    // Process exit: flag
    bool hasProcessExited { false };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (State)
};

} // namespace debug
