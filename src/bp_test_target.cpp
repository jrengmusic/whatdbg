// bp_test_target.cpp
//
// Tiny breakpoint test victim for bp_diagnose.
//
// bp_diagnose sets a breakpoint on targetFunction() and resumes.
// If dbgeng breakpoints work correctly, the Breakpoint callback fires
// before "AFTER" is printed.
//
// Build: see CMakeLists.txt target "bp_test_target"

#include <cstdio>
#include <windows.h>

// ---------------------------------------------------------------------------
// targetFunction
//
// THIS IS THE BREAKPOINT TARGET LINE.
// bp_diagnose.cpp kTargetLine must equal the line number of the opening brace
// of this function body (the line with the first executable statement).
//
// BREAKPOINT LINE: 26
// ---------------------------------------------------------------------------

static void targetFunction ()
{
    printf ("targetFunction: running\n"); // <-- LINE 26 — kTargetLine in bp_diagnose.cpp
    fflush (stdout);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main ()
{
    printf ("BEFORE\n");
    fflush (stdout);

    Sleep (1000);

    targetFunction ();

    Sleep (1000);

    printf ("AFTER\n");
    fflush (stdout);

    return 0;
}
