#include <chrono>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static constexpr int preLoadDelayMs { 500 };

int main ()
{
    std::printf ("fixture_load started\n");
    std::fflush (stdout);

    std::this_thread::sleep_for (std::chrono::milliseconds (preLoadDelayMs));

#ifdef _WIN32
    HMODULE library { LoadLibraryA ("fixture_module.dll") };
    auto runModule = reinterpret_cast<void (*) ()> (GetProcAddress (library, "runModule"));
#else
    void* library { dlopen ("@executable_path/libfixture_module.dylib", RTLD_NOW) };
    auto runModule = reinterpret_cast<void (*) ()> (dlsym (library, "runModule"));
#endif

    runModule ();
    return 0;
}
