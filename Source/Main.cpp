#include <JuceHeader.h>
#include <BinaryData.h>
#include "Log.h"
#include "Whatdbg.h"

static constexpr const char* kSidecarDirName { "whatdbg" };
static constexpr const char* kDbgEngSubdir   { "dbgeng" };
static constexpr const char* kLogFileName    { "whatdbg.log" };

static juce::File getConfigDirectory () noexcept
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile (kSidecarDirName);
}

static juce::File extractSidecarBinaries () noexcept
{
    const juce::File sidecarDir { getConfigDirectory ().getChildFile (kDbgEngSubdir) };

    if (not sidecarDir.exists ())
        sidecarDir.createDirectory ();

    struct BinaryEntry
    {
        const char* name;
        const char* data;
        int         size;
    };

    const std::array<BinaryEntry, 4> entries
    {{
        { "dbgeng.dll",  BinaryData::dbgeng_dll,  BinaryData::dbgeng_dllSize  },
        { "dbghelp.dll", BinaryData::dbghelp_dll, BinaryData::dbghelp_dllSize },
        { "dbgcore.dll", BinaryData::dbgcore_dll, BinaryData::dbgcore_dllSize },
        { "symsrv.dll",  BinaryData::symsrv_dll,  BinaryData::symsrv_dllSize  }
    }};

    bool isAllOk { true };

    for (const auto& entry : entries)
    {
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

int main (int argc, char* argv[])
{
    juce::ignoreUnused (argc, argv);

    int exitCode { 0 };

    const juce::File configDir { getConfigDirectory () };

    if (not configDir.exists ())
        configDir.createDirectory ();

#if JUCE_DEBUG
    const juce::File logPath { configDir.getChildFile (kLogFileName) };
    g_logFile = fopen (logPath.getFullPathName ().toRawUTF8 (), "w");
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
