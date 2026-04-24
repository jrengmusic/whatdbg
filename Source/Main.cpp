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
#include <exception>
#include "Log.h"
#include "Whatdbg.h"

#if JUCE_MAC
#include <unistd.h>
#endif

static constexpr const char* sidecarDirName { "whatdbg" };
static constexpr const char* dbgengSubdir   { "dbgeng" };
static constexpr const char* liblldbSubdir  { "liblldb" };
static constexpr const char* logFileName    { "whatdbg.log" };

#if JUCE_MAC
static constexpr const char* reexecMarker { "WHATDBG_REEXEC" };
#endif

/** Return the whatdbg configuration directory.
 *
 *  Resolves to the platform-specific application data directory with a "whatdbg"
 *  subdirectory: ~/Library/Application Support/whatdbg/ on macOS,
 *  %APPDATA%/whatdbg/ on Windows.
 */
static juce::File getConfigDirectory () noexcept
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile (sidecarDirName);
}

#if JUCE_DEBUG
/** Crash handler — logs stack trace to whatdbg.log before OS terminates the process. */
static void onApplicationCrash (void* context) noexcept
{
    juce::ignoreUnused (context);
    logWrite ("WHATDBG: CRASH\n%s\n", juce::SystemStats::getStackBacktrace ().toRawUTF8 ());
    if (g_logFile != nullptr)
        fflush (g_logFile);
}

/** Terminate handler — logs active exception (if any) and stack trace before abort. */
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
            logWrite ("WHATDBG: TERMINATE std::exception: %s\n", e.what ());
        }
        catch (...)
        {
            logWrite ("WHATDBG: TERMINATE unknown exception\n");
        }
    }
    else
    {
        logWrite ("WHATDBG: TERMINATE (no active exception)\n");
    }

    logWrite ("%s\n", juce::SystemStats::getStackBacktrace ().toRawUTF8 ());

    if (g_logFile != nullptr)
        fflush (g_logFile);

    std::abort ();
}
#endif

/** Entry describing an embedded binary to extract from BinaryData.
 *
 *  Each entry maps a BinaryData symbol (data pointer + size) to a filename
 *  for extraction to the sidecar directory.
 */
struct BinaryEntry
{
    const char* name;
    const char* data;
    int         size;
};

/** Extract embedded binaries from BinaryData to a sidecar subdirectory.
 *
 *  Writes each entry to disk only if the file is missing or its size differs
 *  from the embedded version (cheap upgrade detection).
 *
 *  @param subdir      Subdirectory name within the config directory (e.g., "dbgeng", "liblldb").
 *  @param entries     Array of BinaryEntry descriptors to extract.
 *  @param entryCount  Number of entries in the array.
 *  @return The sidecar directory on success, or an invalid File on failure.
 */
static juce::File extractSidecarBinaries (const char* subdir,
                                          const BinaryEntry* entries,
                                          int entryCount) noexcept
{
    const juce::File sidecarDir { getConfigDirectory ().getChildFile (subdir) };

    if (not sidecarDir.exists ())
        sidecarDir.createDirectory ();

    bool isAllOk { true };

    for (int i { 0 }; i < entryCount; ++i)
    {
        const auto& entry { entries[i] };
        const juce::File dest { sidecarDir.getChildFile (entry.name) };
        const bool shouldWrite { not dest.existsAsFile ()
                                 or dest.getSize () != static_cast<juce::int64> (entry.size) };

        if (shouldWrite)
        {
            juce::FileOutputStream stream { dest };

            if (stream.openedOk ())
            {
                stream.write (entry.data, static_cast<size_t> (entry.size));
            }
            else
            {
                isAllOk = false;
            }
        }
    }

    return isAllOk ? sidecarDir : juce::File {};
}

#if JUCE_WINDOWS
/** Extract Windows dbgeng DLLs (dbgeng.dll, dbghelp.dll, dbgcore.dll, symsrv.dll). */
static juce::File extractSidecarBinaries () noexcept
{
    static const BinaryEntry entries[]
    {
        { "dbgeng.dll",  BinaryData::dbgeng_dll,  BinaryData::dbgeng_dllSize  },
        { "dbghelp.dll", BinaryData::dbghelp_dll, BinaryData::dbghelp_dllSize },
        { "dbgcore.dll", BinaryData::dbgcore_dll, BinaryData::dbgcore_dllSize },
        { "symsrv.dll",  BinaryData::symsrv_dll,  BinaryData::symsrv_dllSize  }
    };

    return extractSidecarBinaries (dbgengSubdir, entries, 4);
}
#endif

#if JUCE_MAC
/** Extract macOS liblldb.dylib (per-arch, embedded as BinaryData). */
static juce::File extractSidecarBinaries () noexcept
{
    static const BinaryEntry entries[]
    {
        { "liblldb.dylib", BinaryData::liblldb_dylib, BinaryData::liblldb_dylibSize }
    };

    return extractSidecarBinaries (liblldbSubdir, entries, 1);
}
#endif

int main (int argc, char* argv[])
{
    juce::ignoreUnused (argc, argv);

    // ── macOS re-exec trampoline ─────────────────────────────────────────
    // First invocation: extract liblldb.dylib, set DYLD_LIBRARY_PATH to the
    // sidecar directory, mark WHATDBG_REEXEC=1, and execv(self). Second
    // invocation: WHATDBG_REEXEC is set, skip trampoline, dyld has already
    // resolved all SB API symbols via DYLD_LIBRARY_PATH.
#if JUCE_MAC
    if (getenv (reexecMarker) == nullptr)
    {
        const juce::File configDir { getConfigDirectory () };

        if (not configDir.exists ())
            configDir.createDirectory ();

        const juce::File sidecarDir { extractSidecarBinaries () };

        if (sidecarDir != juce::File {})
        {
            setenv ("DYLD_LIBRARY_PATH", sidecarDir.getFullPathName ().toRawUTF8 (), 1);
            setenv (reexecMarker, "1", 1);
            execv (argv[0], argv);

            // execv only returns on failure
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

    const juce::File configDir { getConfigDirectory () };

    if (not configDir.exists ())
        configDir.createDirectory ();

#if JUCE_DEBUG
    const juce::File logPath { configDir.getChildFile (logFileName) };
    g_logFile = fopen (logPath.getFullPathName ().toRawUTF8 (), "w");
    juce::SystemStats::setApplicationCrashHandler (&onApplicationCrash);
    std::set_terminate (&onApplicationTerminate);
#endif

    logWrite ("WHATDBG: started\n");

    const auto sidecarDir { extractSidecarBinaries () };

    if (sidecarDir != juce::File {})
    {
        logWrite ("WHATDBG: sidecar at %s\n", sidecarDir.getFullPathName ().toRawUTF8 ());

        Whatdbg whatdbg;
        const bool isInitialized { whatdbg.initialize (sidecarDir) };

        if (isInitialized)
        {
            logWrite ("WHATDBG: initialized\n");
            whatdbg.run ();
        }
        else
        {
            logWrite ("WHATDBG: initialization failed\n");
            exitCode = 1;
        }
    }
    else
    {
        logWrite ("WHATDBG: sidecar extraction failed\n");
        exitCode = 1;
    }

    logWrite ("WHATDBG: exit code %d\n", exitCode);

#if JUCE_DEBUG
    if (g_logFile != nullptr)
    {
        fclose (g_logFile);
        g_logFile = nullptr;
    }
#endif

    return exitCode;
}
