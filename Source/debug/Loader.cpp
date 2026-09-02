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

// Intentionally leaks dbgengModule: FreeLibrary on dbgeng.dll hangs or crashes
// because dbgeng spawns symsrv threads and holds COM state that cannot be safely
// torn down at module unload. The HMODULE lives for the entire process lifetime,
// so OS process teardown reclaims it. Named threat: FreeLibrary(dbgeng) hang.
Loader::~Loader () = default;

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

    return dbgengModule != nullptr and debugCreateFn != nullptr;
}

IDebugClient5* Loader::createDebugClient () const noexcept
{
    jassert (debugCreateFn != nullptr);

    IDebugClient5* client { nullptr };
    const HRESULT result { debugCreateFn (__uuidof (IDebugClient5), reinterpret_cast<PVOID*> (&client)) };

    if (FAILED (result))
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: DebugCreate failed, hr=0x"
                                 + juce::String::toHexString (static_cast<unsigned long> (result)));
#endif
    }

    return client;
}

} // namespace debug
