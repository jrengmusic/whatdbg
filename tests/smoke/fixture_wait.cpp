#include <chrono>
#include <cstdio>
#include <thread>

int main ()
{
    std::printf ("fixture_wait started\n");
    std::fflush (stdout);

    int ticks { 0 };

    while (true)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
        ++ticks; // SMOKE_BREAKPOINT_LINE
    }

    return 0;
}
