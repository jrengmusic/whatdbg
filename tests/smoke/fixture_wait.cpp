// whatdbg smoke-test fixture — long-running target for attach + pause scenarios.
//
// Sleeps for up to 30 seconds so a DAP client can attach to it or pause it
// while it is running. Exits 0 if not interrupted earlier.

#include <chrono>
#include <thread>

int main ()
{
    std::this_thread::sleep_for (std::chrono::seconds { 30 });
    return 0;
}
