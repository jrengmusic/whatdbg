#pragma once
#include <JuceHeader.h>
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

    bool isRunning          { true };
    bool isConfigurationDone { false };
    bool isStepPending { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Whatdbg)
};
