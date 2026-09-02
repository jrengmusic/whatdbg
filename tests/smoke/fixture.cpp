#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static constexpr int postBreakpointDelayMs { 1500 };

static int computeSum (const std::vector<int>& numbers)
{
    int total { 0 };

    for (int value : numbers)
        total += value;

    return total;
}

int main (int argumentCount, char* arguments[])
{
#ifdef SMOKE_FIXTURE_ALWAYS_CRASH
    const bool crashMode { true };
#else
    const bool crashMode { argumentCount > 1 and std::strcmp (arguments[1], "crash") == 0 };
#endif

    std::printf ("BREAKPOINT_TARGET_A\n");
    std::fflush (stdout);
    std::fprintf (stderr, "BREAKPOINT_TARGET_B\n");
    std::fflush (stderr);

    const char* greeting { "hello from fixture" };
    std::string name { "whatdbg" };
    int counter { 42 };
    int* counterPtr { &counter };
    std::vector<int> numbers { 1, 2, 3, 4, 5 };

    const int sum { computeSum (numbers) };
    std::printf ("sum=%d\n", sum); // SMOKE_BREAKPOINT_LINE

    if (crashMode)
    {
        int* nullPointer { nullptr };
        *nullPointer = 1;
    }

    std::this_thread::sleep_for (std::chrono::milliseconds (postBreakpointDelayMs));
    std::printf ("%s %s %d\n", greeting, name.c_str (), counter);
    return 0;
}
