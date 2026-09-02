/** @file Main.cpp
 *  @brief Entry point for whatdbg — sidecar extraction and process bootstrap.
 *
 *  Startup sequence:
 *  1. Extract platform debug engine from BinaryData to sidecar directory.
 *  2. [macOS] Re-exec with DYLD_LIBRARY_PATH pointing to sidecar dir so dyld
 *     resolves liblldb SB API symbols on the second invocation.
 *  3. Initialize logging (Debug builds only).
 *  4. Initialize Whatdbg with the sidecar directory.
 *  5. Run the DAP main loop.
 *
 *  Sidecar pattern is identical on both platforms:
 *  - Windows: dbgeng DLLs → ~/.config/whatdbg/dbgeng/ → LoadLibrary
 *  - macOS:   liblldb.dylib → ~/Library/Application Support/whatdbg/liblldb/ → DYLD_LIBRARY_PATH + re-exec
 */
#include <JuceHeader.h>
#include <BinaryData.h>
#include "Whatdbg.h"

#if JUCE_MAC
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#endif

static constexpr const char* sidecarDirName { "whatdbg" };
static constexpr const char* dbgengSubdir   { "dbgeng" };
static constexpr const char* liblldbSubdir  { "liblldb" };
static constexpr const char* logFileName    { "whatdbg.log" };

#if JUCE_MAC
static constexpr const char* reexecMarker { "WHATDBG_REEXEC" };
#endif

static juce::File getConfigDirectory () noexcept
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile (sidecarDirName);
}

static juce::File getOrCreateConfigDirectory () noexcept
{
    const juce::File configDir { getConfigDirectory () };
    configDir.createDirectory ();

    return configDir;
}

#if JUCE_DEBUG
static void onApplicationCrash (void* context) noexcept
{
    juce::ignoreUnused (context);
    jam::debug::Log::write (juce::String ("WHATDBG: CRASH\n") + juce::SystemStats::getStackBacktrace ());
}

static void onApplicationTerminate () noexcept
{
    const auto activeException { std::current_exception () };

    if (activeException != nullptr)
    {
        try
        {
            std::rethrow_exception (activeException);
        }
        catch (const std::exception& e)
        {
            jam::debug::Log::write (juce::String ("WHATDBG: TERMINATE std::exception: ") + e.what ());
        }
        catch (...)
        {
            jam::debug::Log::write ("WHATDBG: TERMINATE unknown exception");
        }
    }
    else
    {
        jam::debug::Log::write ("WHATDBG: TERMINATE (no active exception)");
    }

    jam::debug::Log::write (juce::SystemStats::getStackBacktrace ());

    std::abort ();
}
#endif

static juce::File getOrCreateSidecarDirectory (const char* subdir) noexcept
{
    const juce::File sidecarDir { getConfigDirectory ().getChildFile (subdir) };
    sidecarDir.createDirectory ();

    return sidecarDir;
}

static bool applySidecarBinary (const juce::File& sidecarDir, const char* name,
                                const char* data, int size) noexcept
{
    const juce::File dest { sidecarDir.getChildFile (name) };
    const bool shouldWrite { not dest.existsAsFile ()
                             or dest.getSize () != static_cast<juce::int64> (size) };

    bool isOk { true };

    if (shouldWrite)
    {
        juce::FileOutputStream stream { dest };

        isOk = stream.openedOk () and stream.write (data, static_cast<size_t> (size));
    }

    return isOk;
}

#if JUCE_WINDOWS
static juce::File getOrCreateSidecar () noexcept
{
    const juce::File sidecarDir { getOrCreateSidecarDirectory (dbgengSubdir) };

    const bool wroteDbgeng  { applySidecarBinary (sidecarDir, "dbgeng.dll",
                                                  BinaryData::dbgeng_dll, BinaryData::dbgeng_dllSize) };
    const bool wroteDbghelp { applySidecarBinary (sidecarDir, "dbghelp.dll",
                                                  BinaryData::dbghelp_dll, BinaryData::dbghelp_dllSize) };
    const bool wroteDbgcore { applySidecarBinary (sidecarDir, "dbgcore.dll",
                                                  BinaryData::dbgcore_dll, BinaryData::dbgcore_dllSize) };
    const bool wroteSymsrv  { applySidecarBinary (sidecarDir, "symsrv.dll",
                                                  BinaryData::symsrv_dll, BinaryData::symsrv_dllSize) };

    const bool isAllOk { wroteDbgeng and wroteDbghelp and wroteDbgcore and wroteSymsrv };

    return isAllOk ? sidecarDir : juce::File {};
}
#endif

#if JUCE_MAC
static juce::File getOrCreateSidecar () noexcept
{
    const juce::File sidecarDir { getOrCreateSidecarDirectory (liblldbSubdir) };

    const bool isAllOk { applySidecarBinary (sidecarDir, "liblldb.dylib",
                                             BinaryData::liblldb_dylib, BinaryData::liblldb_dylibSize) };

    return isAllOk ? sidecarDir : juce::File {};
}
#endif

int main (int argc, char* argv[])
{
    juce::ignoreUnused (argc, argv);

#if JUCE_MAC
    if (getenv (reexecMarker) == nullptr)
    {
        juce::ignoreUnused (getOrCreateConfigDirectory ());

        const juce::File sidecarDir { getOrCreateSidecar () };

        if (sidecarDir != juce::File {})
        {
            setenv ("DYLD_LIBRARY_PATH", sidecarDir.getFullPathName ().toRawUTF8 (), 1);
            setenv (reexecMarker, "1", 1);
            execv (argv[0], argv);

            fprintf (stderr, "whatdbg: re-exec failed: %s\n", strerror (errno));
        }
        else
        {
            fprintf (stderr, "whatdbg: sidecar extraction failed\n");
        }

        return 1;
    }
#endif

    int exitCode { 0 };

    const juce::File configDir { getOrCreateConfigDirectory () };

#if JUCE_DEBUG
    const juce::File logPath { configDir.getChildFile (logFileName) };
    jam::debug::Log::Scope logScope { logPath };
    juce::SystemStats::setApplicationCrashHandler (&onApplicationCrash);
    std::set_terminate (&onApplicationTerminate);

    jam::debug::Log::write ("WHATDBG: started");
#endif

    const auto sidecarDir { getOrCreateSidecar () };

    if (sidecarDir != juce::File {})
    {
#if JUCE_DEBUG
        jam::debug::Log::write (juce::String ("WHATDBG: sidecar at ") + sidecarDir.getFullPathName ());
#endif

        Whatdbg whatdbg;
        const bool isInitialized { whatdbg.initialize (sidecarDir) };

        if (isInitialized)
        {
#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: initialized");
#endif
            whatdbg.run ();
        }
        else
        {
#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: initialization failed");
#endif
            exitCode = 1;
        }
    }
    else
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: sidecar extraction failed");
#endif
        exitCode = 1;
    }

#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: exit code", exitCode);
#endif

    return exitCode;
}
