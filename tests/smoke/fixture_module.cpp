#include <cstdio>

#ifdef _WIN32
__declspec (dllexport)
#endif
extern "C" void runModule ()
{
    std::printf ("module loaded\n"); // SMOKE_MODULE_LINE
    std::fflush (stdout);
}
