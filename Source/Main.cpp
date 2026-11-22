/** @file Main.cpp
 *  @brief Entry point of whatdbg: the help page, the launcher mode, the macOS re-exec, and the JUCE startup.
 *
 *  Startup sequence:
 *  1. [Windows] Set _NO_DEBUG_HEAP to 1. Each target that whatdbg creates inherits the variable.
 *     A target that starts with a debugger attached gets the Windows debug heap.
 *     That heap checks each allocation and free, so the target runs and exits slowly.
 *  2. --help: render the help page to stdout and return.
 *  3. --comm-file and --launch-target: run the Launcher and return its exit code. The
 *     adapter does not start. The client runs this command in a terminal (Processor::launchInTerminal).
 *  4. [macOS] Without the re-exec marker, extract liblldb.dylib and re-exec the
 *     process with DYLD_LIBRARY_PATH set to the sidecar folder, so that dyld
 *     resolves the liblldb SB API symbols on the second invocation.
 *  5. [Debug builds] Open whatdbg.log in the config folder and install the crash
 *     and terminate handlers.
 *  6. Extract the sidecar binaries into the sidecar folder.
 *  7. Call JUCEApplicationBase::main with the program path and the sidecar path.
 *     JUCE then calls Whatdbg::initialise.
 *
 *  Sidecar folders, under userApplicationDataDirectory/whatdbg:
 *  - Windows: dbgeng DLLs in dbgeng/. Loader loads dbgeng.dll from that folder.
 *  - macOS:   liblldb.dylib in liblldb/.
 */
#include <JuceHeader.h>
#include <BinaryData.h>
#include "Whatdbg.h"
#include "Launcher.h"
#include "ToolServer.h"
#include "generated/Generated.h"

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
static constexpr const char* enabledValue   { "1" };

#if JUCE_MAC
static constexpr const char* reexecMarker { "WHATDBG_REEXEC" };
#endif

#if JUCE_WINDOWS
static constexpr const char* noDebugHeapVariable { "_NO_DEBUG_HEAP" };
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

static int execWithSidecar (char* argv[])
{
    juce::ignoreUnused (getOrCreateConfigDirectory ());

    const juce::File sidecarDir { getOrCreateSidecar () };

    if (sidecarDir != juce::File {})
    {
        setenv ("DYLD_LIBRARY_PATH", sidecarDir.getFullPathName ().toRawUTF8 (), 1);
        setenv (reexecMarker, enabledValue, 1);
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

JUCE_CREATE_APPLICATION_DEFINE (Whatdbg)

static int runAdapter (const char* programPath)
{
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

        const juce::String sidecarPath { sidecarDir.getFullPathName () };
        const char* adapterArguments[] { programPath, sidecarPath.toRawUTF8 () };

        juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
        exitCode = juce::JUCEApplicationBase::main (static_cast<int> (std::size (adapterArguments)), adapterArguments);
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

static int runLauncher (const juce::ArgumentList& arguments)
{
    juce::StringArray launchTarget;

    for (int i { arguments.indexOfOption (Id::launchTargetOption.toString ()) + 1 }; i < arguments.size (); ++i)
        launchTarget.add (arguments[i].text);

    return Launcher { arguments[arguments.indexOfOption (Id::commFileOption.toString ()) + 1].text, launchTarget }.run ();
}

static juce::JUCEApplicationBase* createToolServer ()
{
    return new ToolServer ();
}

static int runToolServer (const char* programPath)
{
    const char* toolArguments[] { programPath };
    juce::JUCEApplicationBase::createInstance = &createToolServer;
    return juce::JUCEApplicationBase::main (static_cast<int> (std::size (toolArguments)), toolArguments);
}

int main (int argc, char* argv[])
{
#if JUCE_WINDOWS
    const BOOL isSet { SetEnvironmentVariableA (noDebugHeapVariable, enabledValue) };
    jassert (isSet);
    juce::ignoreUnused (isSet);
#endif

    const juce::ArgumentList arguments (argc, argv);

    if (arguments.containsOption ("--help"))
    {
#if JUCE_WINDOWS
        SetConsoleOutputCP (CP_UTF8);
#endif

        juce::ScopedJuceInitialiser_GUI juceInitialiser;

        jam::TerminalPage page { { BinaryData::fetcher }, jam::Strings { files::styleSheet } };
        page.setDocument (std::move (*jam::MarkdownDocument::parse (BinaryData::getString (files::whatdbgHelp), files::whatdbgHelp)));
        juce::MemoryOutputStream frame;
        page.writeToStream (frame);
        fwrite (frame.getData(), sizeof (char), frame.getDataSize(), stdout);
        return 0;
    }

    if (arguments.containsOption (Id::commFileOption.toString ()) and arguments.containsOption (Id::launchTargetOption.toString ()))
        return runLauncher (arguments);

    if (arguments.containsOption (Id::mcpOption.toString ()))
        return runToolServer (argv[0]);

#if JUCE_MAC
    if (getenv (reexecMarker) == nullptr)
        return execWithSidecar (argv);
#endif

    return runAdapter (argv[0]);
}
