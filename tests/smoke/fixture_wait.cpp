#include <chrono>
#include <cstdio>
#include <thread>

int main ()
{
    std::printf ("fixture_wait started\n");
    std::fflush (stdout);

    while (true)
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

    return 0;
}
