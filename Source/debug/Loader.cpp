/** @file Loader.cpp
 *  @brief Windows dbgeng.dll explicit loader via LoadLibrary + GetProcAddress.
 *
 *  Loads the sidecar dbgeng.dll from the extraction directory and resolves
 *  the DebugCreate export. All other debug interfaces (IDebugControl,
 *  IDebugSymbols, etc.) are obtained via COM QueryInterface from the root
 *  IDebugClient5 pointer.
 */
#include <JuceHeader.h>
#include "Loader.h"

namespace debug
{

/** @brief Intentionally leaks @c dbgengModule — see body comment for rationale. */
Loader::~Loader ()
{
    // Intentionally leak dbgengModule: FreeLibrary on dbgeng.dll hangs or crashes
    // because dbgeng spawns symsrv threads and holds COM state that cannot be safely
    // torn down at module unload. The HMODULE lives for the entire process lifetime,
    // so OS process teardown reclaims it. Named threat: FreeLibrary(dbgeng) hang.
    dbgengModule = nullptr;
}

/** @brief Loads dbgeng.dll from @p sidecarDirectory and resolves the DebugCreate export.
 *  @return True if both LoadLibrary and GetProcAddress succeeded.
 */
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

/** @brief Invokes the resolved DebugCreate export to obtain an IDebugClient5 instance.
 *  @pre @c isLoaded() must be true.
 */
HRESULT Loader::createDebugClient (IDebugClient5** outClient) const noexcept
{
    jassert (debugCreateFn != nullptr);
    return debugCreateFn (__uuidof (IDebugClient5), reinterpret_cast<PVOID*> (outClient));
}

/** @brief Returns true if the module is loaded and DebugCreate is resolved. */
bool Loader::isLoaded () const noexcept
{
    return dbgengModule != nullptr and debugCreateFn != nullptr;
}

} // namespace debug
