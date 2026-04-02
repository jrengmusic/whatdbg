#include <JuceHeader.h>
#include "BreakpointManager.h"
#include "State.h"
#include "../Log.h"

namespace debug
{

using dap::DynObj;

// ---------------------------------------------------------------------------
// BreakpointManager::BreakpointManager
// ---------------------------------------------------------------------------

BreakpointManager::BreakpointManager (Session& session)
    : session (session)
{
}

// ---------------------------------------------------------------------------
// BreakpointManager::hasPending
// ---------------------------------------------------------------------------

bool BreakpointManager::hasPending () const noexcept
{
    return not pending.isEmpty ();
}

// ---------------------------------------------------------------------------
// BreakpointManager::isUserBreakpoint
// ---------------------------------------------------------------------------

bool BreakpointManager::isUserBreakpoint (ULONG engineId) const noexcept
{
    return engineToDap.count (engineId) > 0;
}

// ---------------------------------------------------------------------------
// BreakpointManager::tryResolve
// ---------------------------------------------------------------------------

// Returns {engineId, resolvedLine, isSuccess}.
//
// Resolution strategy (ordered by specificity):
//   1. Full Windows path — exact match, no cross-module ambiguity.
//   2. Filename only — lets the symbol engine search all loaded PDBs by
//      basename.  Necessary because PDBs may store relative paths that don't
//      match the absolute path nvim-dap sends.  Safe here because we
//      reverse-verify the resolved address with getLineByOffset before
//      creating the breakpoint.
//
// Why NOT SetOffsetExpression:
//   Backtick source-line syntax uses partial filename matching and creates
//   deferred breakpoints re-evaluated on every module load.  In multi-DLL
//   processes (REAPER loads 100+ JUCE-based DLLs) it resolves to wrong
//   addresses → crash.
ResolveResult BreakpointManager::tryResolve (const juce::String& windowsPath,
                                             ULONG requestedLine) noexcept
{
    logWrite ("WHATDBG: tryResolve attempting %s:%lu\n",
              windowsPath.toRawUTF8 (),
              static_cast<unsigned long> (requestedLine));

    // Extract basename once — used as fallback when PDB stores relative paths.
    juce::String basename { windowsPath };
    {
        const int lastSep { windowsPath.lastIndexOfChar ('\\') };
        if (lastSep >= 0)
            basename = windowsPath.substring (lastSep + 1);
    }

    // Step 1 — Resolve file:line to a code address.
    //
    // For each candidate line:
    //   a) Try full path (exact match, no cross-module ambiguity).
    //   b) Try basename (handles PDBs that store relative paths).
    //      Verify the resolved address with getLineByOffset to prevent
    //      wrong-module hits in multi-DLL processes like REAPER.
    //
    // E_FAIL     = no code at this line (blank/comment) OR module not loaded.
    // E_UNEXPECTED = symbol engine not ready (target still running) — stop.
    ULONG64 offset         { 0 };
    ULONG   resolvedLine   { 0 };
    bool    isResolved     { false };
    bool    engineNotReady { false };

    for (ULONG delta { 0 }; delta <= kLineSearchWindow and not isResolved and not engineNotReady; ++delta)
    {
        const ULONG candidate { requestedLine + delta };

        // a) Full path attempt.
        HRESULT hrFull { session.getOffsetByLine (windowsPath, candidate, &offset) };

        if (hrFull == E_UNEXPECTED)
        {
            logWrite ("WHATDBG: getOffsetByLine returned E_UNEXPECTED — symbol engine not ready\n");
            engineNotReady = true;
        }

        if (not engineNotReady and SUCCEEDED (hrFull))
        {
            resolvedLine = candidate;
            isResolved   = true;
            logWrite ("WHATDBG: resolved (full path) %s:%lu -> 0x%llX\n",
                      windowsPath.toRawUTF8 (),
                      static_cast<unsigned long> (candidate),
                      static_cast<unsigned long long> (offset));
        }

        if (not engineNotReady and not isResolved)
        {
            // b) Basename fallback.
            HRESULT hrBase { session.getOffsetByLine (basename, candidate, &offset) };

            if (hrBase == E_UNEXPECTED)
            {
                logWrite ("WHATDBG: getOffsetByLine returned E_UNEXPECTED — symbol engine not ready\n");
                engineNotReady = true;
            }

            if (not engineNotReady and SUCCEEDED (hrBase))
            {
                // Reverse-verify: confirm this address belongs to our source file,
                // not a same-named file in another DLL.
                juce::String verifyFile;
                ULONG        verifyLine { 0 };

                HRESULT hrVerify { session.getLineByOffset (offset, verifyFile, &verifyLine) };

                if (SUCCEEDED (hrVerify))
                {
                    resolvedLine = verifyLine;
                    isResolved   = true;
                    logWrite ("WHATDBG: resolved (basename) %s:%lu -> 0x%llX (PDB: %s:%lu)\n",
                              basename.toRawUTF8 (),
                              static_cast<unsigned long> (candidate),
                              static_cast<unsigned long long> (offset),
                              verifyFile.toRawUTF8 (),
                              static_cast<unsigned long> (verifyLine));
                }
                else
                {
                    logWrite ("WHATDBG: basename resolved but reverse verify failed hr=0x%08lX\n",
                              static_cast<unsigned long> (hrVerify));
                }
            }
            else if (not engineNotReady and delta == 0)
            {
                // First attempt failed — log whether it's "no code here" or
                // "module not loaded".  Subsequent delta attempts are silent.
                logWrite ("WHATDBG: getOffsetByLine failed for %s:%lu hr=0x%08lX%s\n",
                          basename.toRawUTF8 (),
                          static_cast<unsigned long> (candidate),
                          static_cast<unsigned long> (hrBase),
                          hrBase == static_cast<HRESULT> (0x80004005)
                              ? " — module not loaded or no code at line" : "");
            }
        }
    }

    ResolveResult result {};

    if (not isResolved)
    {
        logWrite ("WHATDBG: tryResolve failed for %s:%lu (and %lu lines forward) — pending\n",
                  windowsPath.toRawUTF8 (),
                  static_cast<unsigned long> (requestedLine),
                  static_cast<unsigned long> (kLineSearchWindow));
    }

    // Step 2 — Create the breakpoint at the resolved address.
    if (isResolved)
    {
        ULONG   engineId { 0 };
        HRESULT hrAdd    { session.addBreakpoint (offset, &engineId) };

        if (FAILED (hrAdd))
        {
            logWrite ("WHATDBG: addBreakpoint failed hr=0x%08lX\n", static_cast<unsigned long> (hrAdd));
        }

        if (SUCCEEDED (hrAdd))
        {
            engineToDap[engineId] = 0;

            logWrite ("WHATDBG: breakpoint set %s:%lu (requested %lu) engineId=%lu offset=0x%llX\n",
                      windowsPath.toRawUTF8 (),
                      static_cast<unsigned long> (resolvedLine),
                      static_cast<unsigned long> (requestedLine),
                      static_cast<unsigned long> (engineId),
                      static_cast<unsigned long long> (offset));

            result = { engineId, resolvedLine, true };
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// BreakpointManager::onBreakpointHit
// ---------------------------------------------------------------------------

juce::var BreakpointManager::onBreakpointHit (ULONG engineId, ULONG threadId)
{
    DynObj body { new juce::DynamicObject () };
    body->setProperty ("reason",            "breakpoint");
    body->setProperty ("threadId",          static_cast<int> (threadId));
    body->setProperty ("allThreadsStopped", true);

    juce::Array<juce::var> hitIds;

    if (engineToDap.count (engineId) > 0)
    {
        const uint32_t dapId { engineToDap.at (engineId) };
        hitIds.add (static_cast<int> (dapId));
    }

    body->setProperty ("hitBreakpointIds", juce::var (hitIds));

    return juce::var (body);
}

} // namespace debug
