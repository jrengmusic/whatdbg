#pragma once
#include <JuceHeader.h>
#include <windows.h>
#include <dbgeng.h>

namespace debug
{

/** Loads dbgeng.dll from a sidecar directory and exposes DebugCreate.
 *
 *  Windows ships dbgeng.dll in the system directory, but the version bundled
 *  with the Debugging Tools for Windows SDK may be newer and more capable.
 *  Loader loads the DLL explicitly from a caller-supplied sidecar directory
 *  so that the correct version is used regardless of what is installed system-wide.
 *
 *  Usage sequence:
 *  1. Call load() to LoadLibraryW the DLL and resolve the DebugCreate export.
 *  2. Call createDebugClient() to obtain an IDebugClient5 pointer.
 *  3. Loader's destructor intentionally does NOT call FreeLibrary — dbgeng.dll
 *     spawns symsrv threads and holds COM state that cannot be safely torn
 *     down at module unload, and FreeLibrary on it hangs or crashes. The
 *     loaded module lives for the entire process lifetime; OS process
 *     teardown reclaims it.
 */
class Loader
{
public:
    Loader () = default;
    ~Loader ();

    /** Load dbgeng.dll from the given sidecar directory.
     *
     *  Calls LoadLibraryW on "<sidecarDirectory>/dbgeng.dll" and resolves
     *  the "DebugCreate" export. On success createDebugClient() may be called.
     *
     *  @param sidecarDirectory  Directory containing dbgeng.dll and its dependencies.
     *  @return true if the DLL was loaded and DebugCreate was resolved successfully.
     *
     *  @note Calling load() more than once is undefined. Create a new Loader instead.
     */
    bool load (const juce::File& sidecarDirectory) noexcept;

    /** Call DebugCreate to obtain an IDebugClient5 interface pointer.
     *
     *  Delegates to the resolved DebugCreate function pointer. The caller
     *  receives ownership of the returned COM pointer and must call Release
     *  (or wrap it in a ComPtr) when done.
     *
     *  @return the IDebugClient5 pointer on success, or nullptr on failure.
     *
     *  @note Requires a prior successful call to load(). Asserts if not loaded.
     */
    IDebugClient5* createDebugClient () const noexcept;

private:
    /** Signature of dbgeng.dll's exported DebugCreate function. */
    using DebugCreateFn = HRESULT (STDAPICALLTYPE*) (REFIID, PVOID*);

    /** Handle returned by LoadLibraryW for dbgeng.dll. Never freed — see class docs. */
    HMODULE dbgengModule { nullptr };

    /** Address of the resolved "DebugCreate" export, or nullptr before a successful load(). */
    DebugCreateFn debugCreateFn { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Loader)
};

} // namespace debug
