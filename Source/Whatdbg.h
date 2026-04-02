#pragma once
#include <JuceHeader.h>
#include <functional>
#include <unordered_map>
#include "debug/State.h"
#include "debug/Session.h"
#include "debug/Callbacks.h"
#include "debug/Loader.h"
#include "debug/BreakpointManager.h"
#include "dap/Reader.h"
#include "dap/Types.h"

class Whatdbg
{
public:
    Whatdbg ();

    // Initialize COM + dbgeng. Must be called on the main thread.
    bool initialize (const juce::File& sidecarDir) noexcept;

    // Run the main loop. Blocks until disconnect/terminate. Called from main().
    void run ();

private:
    // ── DAP command dispatch ───────────────────────────────────────────
    using CommandHandler = std::function<void (const juce::var&)>;
    std::unordered_map<std::string, CommandHandler> commandHandlers;

    void handleCommand (const juce::var& message);
    void handleInitialize (const juce::var& request);
    void handleLaunch (const juce::var& request);
    void handleAttach (const juce::var& request);
    void handleConfigurationDone (const juce::var& request);
    void handleDisconnect (const juce::var& request);
    void handleSetBreakpoints (const juce::var& request);
    void handleThreads (const juce::var& request);
    void handleStackTrace (const juce::var& request);
    void handleScopes (const juce::var& request);
    void handleVariables (const juce::var& request);
    void handleContinue (const juce::var& request);
    void handleNext (const juce::var& request);
    void handleStepIn (const juce::var& request);
    void handleStepOut (const juce::var& request);
    void handlePause (const juce::var& request);
    void handleEvaluate (const juce::var& request);

    // ── Deferred event processing ──────────────────────────────────────
    void processDeferredEvents ();

    // ── stdout writing ─────────────────────────────────────────────────
    void writeMessage (const juce::var& message) noexcept;
    void sendResponse (const juce::var& response) noexcept;
    void sendEvent (const juce::var& event) noexcept;

    // ── Owned objects ──────────────────────────────────────────────────
    debug::State             state;              // SSOT — must be first (Context registration)
    debug::Session           session;            // COM wrapper
    debug::BreakpointManager breakpointManager;
    dap::Reader              reader;             // stdin thread + FIFO

    bool isRunning               { true };
    bool shouldTerminateOnExit   { false };
    bool isConfigurationDone { false };
    bool isStepPending  { false };
    bool isPausePending { false };

    // ── Variable inspection ───────────────────────────────────────────
    int nextVariablesRef { 1 };
    // Maps variablesReference → (frameIndex, symbolIndex). symbolIndex -1 = top-level locals.
    std::unordered_map<int, std::pair<int, int>> variablesRefMap;

    // ── Frame ID mapping (threadId + frameIndex) ──────────────────────
    int nextFrameId { 1 };
    std::unordered_map<int, std::pair<ULONG, int>> frameIdMap; // frameId → (threadSystemId, frameIndex)

    ULONG lastScopesThreadId { 0 };

    void resetVariablesState () noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Whatdbg)
};
