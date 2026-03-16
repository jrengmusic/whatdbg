// smoke_test.cpp
//
// WHATDBG Phase 2.5 — Attach Smoke Test
//
// Validates that dbgeng can attach to a real DAW process and enumerate its
// loaded modules, including JUCE plugin DLLs.  Also exercises symbol
// resolution via IDebugSymbols3::GetOffsetByLine.
//
// Usage:
//   whatdbg_smoke.exe <pid>
//
// All output goes to stderr.  stdout is intentionally unused (reserved for
// the DAP stream in the main whatdbg binary).
//
// Build: see CMakeLists.txt target "whatdbg_smoke"

#include "DbgEngSession.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr const char* kVersion          { "0.1.0" };
static constexpr ULONG       kWaitTimeoutMs    { 10000 };   // 10 s attach timeout
static constexpr ULONG       kMaxModuleNameLen { 512 };
static constexpr int         kSymbolTestLine   { 65 };
static constexpr const char* kSymbolTestFile   { "PluginEditor.cpp" };

// Plugin search keywords — match either JRENG brand name or generic "Filter"
static constexpr const char* kPluginKeyword1   { "JRENG" };
static constexpr const char* kPluginKeyword2   { "Filter" };

// ---------------------------------------------------------------------------
// ModuleInfo — plain data record for one loaded module
// ---------------------------------------------------------------------------

struct ModuleInfo
{
    ULONG       index       { 0 };
    std::string moduleName;
    std::string imagePath;
    std::string symbolPath;
};

// ---------------------------------------------------------------------------
// printUsage
// ---------------------------------------------------------------------------

static void printUsage (const char* programName)
{
    fprintf (stderr, "Usage: %s <pid>\n", programName);
    fprintf (stderr, "  pid  — Process ID of the running DAW to attach to\n");
    fprintf (stderr, "\n");
    fprintf (stderr, "Example:\n");
    fprintf (stderr, "  %s 12345\n", programName);
}

// ---------------------------------------------------------------------------
// getModuleNameString — wrapper around IDebugSymbols3::GetModuleNameString
//
// Returns an empty string on failure rather than propagating HRESULT.
// The caller decides whether an empty name is acceptable.
// ---------------------------------------------------------------------------

static std::string getModuleNameString (IDebugSymbols3* symbols,
                                        ULONG           which,
                                        ULONG           index)
{
    char   buffer[kMaxModuleNameLen] = {};
    ULONG  needed { 0 };

    HRESULT hr { symbols->GetModuleNameString (which,
                                               index,
                                               DEBUG_INVALID_OFFSET,
                                               buffer,
                                               kMaxModuleNameLen,
                                               &needed) };
    std::string result;

    if (SUCCEEDED (hr))
    {
        result = std::string (buffer);
    }

    return result;
}

// ---------------------------------------------------------------------------
// enumerateModules — fills a vector<ModuleInfo> from the loaded module list
// ---------------------------------------------------------------------------

static std::vector<ModuleInfo> enumerateModules (IDebugSymbols3* symbols)
{
    std::vector<ModuleInfo> modules;

    ULONG loadedCount  { 0 };
    ULONG unloadedCount { 0 };

    HRESULT hr { symbols->GetNumberModules (&loadedCount, &unloadedCount) };

    if (SUCCEEDED (hr))
    {
        for (ULONG i { 0 }; i < loadedCount; ++i)
        {
            ModuleInfo info;
            info.index      = i;
            info.moduleName = getModuleNameString (symbols,
                                                   DEBUG_MODNAME_MODULE,
                                                   i);
            info.imagePath  = getModuleNameString (symbols,
                                                   DEBUG_MODNAME_IMAGE,
                                                   i);
            info.symbolPath = getModuleNameString (symbols,
                                                   DEBUG_MODNAME_SYMBOL_FILE,
                                                   i);

            modules.push_back (info);
        }
    }
    else
    {
        fprintf (stderr,
                 "WHATDBG: GetNumberModules failed: 0x%08lX\n",
                 static_cast<unsigned long> (hr));
    }

    return modules;
}

// ---------------------------------------------------------------------------
// printModuleTable — prints the formatted module list to stderr
// ---------------------------------------------------------------------------

static void printModuleTable (const std::vector<ModuleInfo>& modules)
{
    fprintf (stderr,
             "\n=== Loaded Modules (%zu total) ===\n",
             modules.size ());

    for (const ModuleInfo& m : modules)
    {
        // Format: [index] moduleName   imagePath   [PDB: symbolPath]
        // Index is right-aligned in a 3-digit field.
        if (m.symbolPath.empty () == false)
        {
            fprintf (stderr,
                     "[%3u] %-32s  %s  [PDB: %s]\n",
                     m.index,
                     m.moduleName.c_str (),
                     m.imagePath.c_str (),
                     m.symbolPath.c_str ());
        }
        else
        {
            fprintf (stderr,
                     "[%3u] %-32s  %s\n",
                     m.index,
                     m.moduleName.c_str (),
                     m.imagePath.c_str ());
        }
    }
}

// ---------------------------------------------------------------------------
// findPluginModule — searches the module list for a JRENG or Filter DLL
//
// Returns a pointer into the vector (valid as long as the vector lives),
// or nullptr if no match is found.
// ---------------------------------------------------------------------------

static const ModuleInfo* findPluginModule (const std::vector<ModuleInfo>& modules)
{
    const ModuleInfo* found { nullptr };

    for (const ModuleInfo& m : modules)
    {
        bool hasKeyword1 { m.moduleName.find (kPluginKeyword1) != std::string::npos
                           or m.imagePath.find (kPluginKeyword1) != std::string::npos };

        bool hasKeyword2 { m.moduleName.find (kPluginKeyword2) != std::string::npos
                           or m.imagePath.find (kPluginKeyword2) != std::string::npos };

        if ((hasKeyword1 == true or hasKeyword2 == true) and found == nullptr)
        {
            found = &m;
        }
    }

    return found;
}

// ---------------------------------------------------------------------------
// tryResolveSymbol — attempts GetOffsetByLine on kSymbolTestFile:kSymbolTestLine
//
// Returns true and writes the resolved offset into outOffset on success.
// Returns false if resolution fails (no PDB, wrong module, etc.).
// ---------------------------------------------------------------------------

static bool tryResolveSymbol (IDebugSymbols3* symbols, ULONG64& outOffset)
{
    bool isResolved { false };

    ULONG64 offset { 0 };
    ULONG   line   { 0 };
    char    fileBuffer[kMaxModuleNameLen] = {};

    // GetOffsetByLine takes the source file name and line number.
    // It searches all loaded symbol files for a match.
    HRESULT hr { symbols->GetOffsetByLine (static_cast<ULONG> (kSymbolTestLine),
                                           kSymbolTestFile,
                                           &offset) };

    if (SUCCEEDED (hr))
    {
        outOffset  = offset;
        isResolved = true;
    }
    else
    {
        // Attempt the reverse lookup to confirm symbols are at least partially
        // loaded — GetLineByOffset on offset 0 will fail, but a non-E_FAIL
        // HRESULT tells us the symbol engine is alive.
        fprintf (stderr,
                 "  GetOffsetByLine failed: 0x%08lX\n",
                 static_cast<unsigned long> (hr));

        // Suppress unused-variable warning from the zero-init above.
        (void) line;
        (void) fileBuffer;
    }

    return isResolved;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main (int argc, char* argv[])
{
    fprintf (stderr, "WHATDBG Smoke Test v%s\n", kVersion);

    // -----------------------------------------------------------------------
    // Step 1 — Parse PID argument
    // -----------------------------------------------------------------------

    bool isArgValid { argc == 2 };

    if (isArgValid == false)
    {
        fprintf (stderr, "ERROR: No PID argument provided.\n\n");
        printUsage (argv[0]);
        return 1;
    }

    // strtoul returns 0 on parse failure; PID 0 is never valid on Windows.
    char*         endPtr { nullptr };
    unsigned long pidRaw { std::strtoul (argv[1], &endPtr, 10) };

    bool isPidParsed { endPtr != argv[1] and *endPtr == '\0' and pidRaw > 0 };

    if (isPidParsed == false)
    {
        fprintf (stderr, "ERROR: Invalid PID: \"%s\"\n\n", argv[1]);
        printUsage (argv[0]);
        return 1;
    }

    ULONG pid { static_cast<ULONG> (pidRaw) };

    // -----------------------------------------------------------------------
    // Step 2 — Initialize DbgEngSession
    // -----------------------------------------------------------------------

    DbgEngSession session;
    bool isSessionOk { session.initialize () };

    if (isSessionOk == false)
    {
        // initialize() already printed the HRESULT to stderr.
        fprintf (stderr, "ERROR: DbgEngSession::initialize() failed.\n");
        return 1;
    }

    // -----------------------------------------------------------------------
    // Step 3 — Attach to the target process
    // -----------------------------------------------------------------------

    fprintf (stderr, "Attaching to PID %lu...\n", static_cast<unsigned long> (pid));

    // DEBUG_ATTACH_DEFAULT: non-invasive attach is NOT used here — we need
    // full invasive attach so that symbol resolution and module enumeration
    // work correctly.  The target process is suspended during attach.
    HRESULT hrAttach { session.client ()->AttachProcess (0,
                                                         pid,
                                                         DEBUG_ATTACH_DEFAULT) };

    bool isAttachOk { SUCCEEDED (hrAttach) };

    if (isAttachOk == false)
    {
        fprintf (stderr,
                 "ERROR: AttachProcess failed: 0x%08lX\n",
                 static_cast<unsigned long> (hrAttach));
        fprintf (stderr,
                 "  Hint: Run as Administrator for SeDebugPrivilege.\n");
        fprintf (stderr,
                 "  Hint: Process may already be under a debugger.\n");
        session.shutdown ();
        return 1;
    }

    // Mark that we have a target so shutdown() calls EndSession correctly.
    session.setHasTarget ();

    // -----------------------------------------------------------------------
    // Step 4 — Wait for attach to complete
    //
    // WaitForEvent drives the dbgeng event loop until the initial attach
    // break-in event fires.  S_OK means an event was processed.
    // S_FALSE means timeout with no event (should not happen at attach time
    // with a generous timeout, but we handle it gracefully).
    // -----------------------------------------------------------------------

    fprintf (stderr, "Waiting for attach event (timeout %lu ms)...\n",
             static_cast<unsigned long> (kWaitTimeoutMs));

    HRESULT hrWait { session.control ()->WaitForEvent (0, kWaitTimeoutMs) };

    bool isWaitOk { SUCCEEDED (hrWait) };

    if (isWaitOk == false)
    {
        fprintf (stderr,
                 "ERROR: WaitForEvent failed: 0x%08lX\n",
                 static_cast<unsigned long> (hrWait));
        fprintf (stderr,
                 "  Hint: Attach may have been rejected by the target process.\n");
        session.shutdown ();
        return 1;
    }

    if (hrWait == S_FALSE)
    {
        fprintf (stderr,
                 "WARNING: WaitForEvent timed out — attach may be incomplete.\n");
        fprintf (stderr,
                 "  Continuing anyway; module list may be partial.\n");
    }
    else
    {
        fprintf (stderr, "Attach succeeded.\n");
    }

    // -----------------------------------------------------------------------
    // Step 5 — Enumerate all loaded modules
    // -----------------------------------------------------------------------

    std::vector<ModuleInfo> modules { enumerateModules (session.symbols ()) };

    // -----------------------------------------------------------------------
    // Step 6 — Print module table
    // -----------------------------------------------------------------------

    printModuleTable (modules);

    // -----------------------------------------------------------------------
    // Step 7 — Search for plugin module
    // -----------------------------------------------------------------------

    fprintf (stderr, "\n=== Plugin Search ===\n");

    const ModuleInfo* pluginModule { findPluginModule (modules) };

    bool isPluginFound { pluginModule != nullptr };

    if (isPluginFound == true)
    {
        fprintf (stderr,
                 "Found plugin module: %s (index %u)\n",
                 pluginModule->moduleName.c_str (),
                 pluginModule->index);
    }
    else
    {
        fprintf (stderr,
                 "No module matching \"%s\" or \"%s\" found in module list.\n",
                 kPluginKeyword1,
                 kPluginKeyword2);
        fprintf (stderr,
                 "  (Plugin may not be loaded, or name does not match keywords.)\n");
    }

    // -----------------------------------------------------------------------
    // Step 8 — Symbol resolution test
    // -----------------------------------------------------------------------

    fprintf (stderr, "\n=== Symbol Resolution Test ===\n");
    fprintf (stderr,
             "Resolving %s:%d...\n",
             kSymbolTestFile,
             kSymbolTestLine);

    ULONG64 resolvedOffset { 0 };
    bool    isSymbolResolved { tryResolveSymbol (session.symbols (),
                                                 resolvedOffset) };

    if (isSymbolResolved == true)
    {
        fprintf (stderr,
                 "Resolved to offset 0x%016llX\n",
                 static_cast<unsigned long long> (resolvedOffset));
    }
    else
    {
        fprintf (stderr,
                 "Could not resolve %s:%d — PDB may not be loaded.\n",
                 kSymbolTestFile,
                 kSymbolTestLine);
    }

    // -----------------------------------------------------------------------
    // Step 9 — Detach
    // -----------------------------------------------------------------------

    fprintf (stderr, "\nDetaching...\n");

    // DetachProcesses leaves the target running and removes all breakpoints.
    // We call this explicitly before shutdown() so the target is not killed.
    HRESULT hrDetach { session.client ()->DetachProcesses () };

    if (FAILED (hrDetach))
    {
        fprintf (stderr,
                 "WARNING: DetachProcesses failed: 0x%08lX\n",
                 static_cast<unsigned long> (hrDetach));
        fprintf (stderr,
                 "  Target process may have been left in a suspended state.\n");
    }

    // Clear the hasTarget flag so shutdown() does not call EndSession again
    // (DetachProcesses already ended the session cleanly).
    // DbgEngSession does not expose a clearHasTarget(), so we call shutdown()
    // directly — it will call EndSession(DETACH) again, which is harmless
    // (returns E_UNEXPECTED when no target is active, and shutdown() ignores
    // the return value).
    session.shutdown ();

    // -----------------------------------------------------------------------
    // Step 10 — Summary
    // -----------------------------------------------------------------------

    fprintf (stderr, "\n=== Summary ===\n");
    fprintf (stderr, "Modules loaded:     %zu\n", modules.size ());

    if (isPluginFound == true)
    {
        fprintf (stderr, "Plugin found:       YES (%s)\n",
                 pluginModule->moduleName.c_str ());
    }
    else
    {
        fprintf (stderr, "Plugin found:       NO\n");
    }

    if (isSymbolResolved == true)
    {
        fprintf (stderr,
                 "Symbol resolution:  YES (0x%016llX)\n",
                 static_cast<unsigned long long> (resolvedOffset));
    }
    else
    {
        fprintf (stderr, "Symbol resolution:  NO (no PDB)\n");
    }

    fprintf (stderr, "\nDone.\n");

    return 0;
}
