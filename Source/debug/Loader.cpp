#include <JuceHeader.h>
#include "Loader.h"

namespace debug
{

Loader::~Loader ()
{
    // Intentionally leak dbgengModule: FreeLibrary on dbgeng.dll hangs or crashes
    // because dbgeng spawns symsrv threads and holds COM state that cannot be safely
    // torn down at module unload. The HMODULE lives for the entire process lifetime,
    // so OS process teardown reclaims it. Named threat: FreeLibrary(dbgeng) hang.
    dbgengModule = nullptr;
}

bool Loader::load (const juce::File& sidecarDirectory) noexcept
{
    const juce::File dllPath { sidecarDirectory.getChildFile ("dbgeng.dll") };
    HMODULE loadedModule { LoadLibraryW (dllPath.getFullPathName ().toWideCharPointer ()) };

    if (loadedModule != nullptr)
    {
        FARPROC procAddress { GetProcAddress (loadedModule, "DebugCreate") };

        if (procAddress != nullptr)
        {
            dbgengModule = loadedModule;
            debugCreateFn = reinterpret_cast<DebugCreateFn> (procAddress);
        }
        else
        {
            FreeLibrary (loadedModule);
        }
    }

    return isLoaded ();
}

HRESULT Loader::createDebugClient (IDebugClient5** outClient) const noexcept
{
    jassert (debugCreateFn != nullptr);
    return debugCreateFn (__uuidof (IDebugClient5), reinterpret_cast<PVOID*> (outClient));
}

bool Loader::isLoaded () const noexcept
{
    return dbgengModule != nullptr and debugCreateFn != nullptr;
}

} // namespace debug
