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
 *  3. Loader's destructor calls FreeLibrary on the loaded module.
 */
class Loader
{
public:
    Loader () = default;
    ~Loader ();

    /** Load dbgeng.dll from the given sidecar directory.
     *
     *  Calls LoadLibraryW on "<sidecarDirectory>/dbgeng.dll" and resolves
     *  the "DebugCreate" export. On success isLoaded() returns true and
     *  createDebugClient() may be called.
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
     *  @param outClient  Receives the IDebugClient5 pointer on success.
     *  @return S_OK on success, or the HRESULT from DebugCreate on failure.
     *
     *  @note Requires a prior successful call to load(). Asserts if not loaded.
     */
    HRESULT createDebugClient (IDebugClient5** outClient) const noexcept;

    /** Return true if dbgeng.dll is loaded and DebugCreate is resolved.
     *
     *  @return true if load() succeeded, false otherwise.
     */
    bool isLoaded () const noexcept;

private:
    using DebugCreateFn = HRESULT (STDAPICALLTYPE*) (REFIID, PVOID*);

    HMODULE dbgengModule { nullptr };
    DebugCreateFn debugCreateFn { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Loader)
};

} // namespace debug
