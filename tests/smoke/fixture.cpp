// whatdbg smoke-test fixture
//
// Debuggee exercised by tests/smoke/run.lua. Compiled standalone (no JuceHeader)
// and linked against juce_core for juce::String. BLESSED-compliant: no early
// returns, positive nested checks, brace initialization, .at() access,
// not/and/or alternative tokens.
//
// Layout: locals-under-test live in probeLocals(). Breakpoint targets are the
// two std::puts() lines marked BREAKPOINT_TARGET_A / BREAKPOINT_TARGET_B.

#include <juce_core/juce_core.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> workerShouldExit { false };

static void workerThreadBody ()
{
    while (not workerShouldExit.load ())
    {
        std::this_thread::sleep_for (std::chrono::milliseconds { 20 });
    }
}

static void probeLocals ()
{
    const juce::String          greeting { "hello from juce::String" };
    const std::string           name     { "hello from std::string" };
    const std::unique_ptr<int>  counter  { std::make_unique<int> (42) };
    const std::vector<int>      numbers  { 1, 2, 3, 4, 5 };

    std::printf  ("BREAKPOINT_TARGET_A greeting=%s name=%s counter=%d size=%zu\n",
                  greeting.toRawUTF8 (), name.c_str (), *counter, numbers.size ());
    std::fprintf (stderr, "BREAKPOINT_TARGET_B stderr channel alive\n");
}

static void triggerCrash ()
{
    volatile int* nullAddress { nullptr };
    *nullAddress = 0xdead;
}

int main (int argc, char** argv)
{
    const bool isCrashMode { argc > 1 and std::strcmp (argv[1], "crash") == 0 };

    std::thread worker { workerThreadBody };

    probeLocals ();

    if (isCrashMode)
    {
        triggerCrash ();
    }

    workerShouldExit.store (true);
    worker.join ();

    return 0;
}
