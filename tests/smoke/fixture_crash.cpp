// whatdbg smoke-test fixture — deliberate-crash target for exception-surfacing
// scenarios. Writes a marker line to stdout, then dereferences a null pointer
// to trigger SIGSEGV / EXC_BAD_ACCESS.

#include <cstdio>

static void triggerCrash ()
{
    volatile int* nullAddress { nullptr };
    *nullAddress = 0xdead;
}

int main ()
{
    std::puts ("smoke_fixture_crash: about to crash");
    triggerCrash ();
    return 0;
}
