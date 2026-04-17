#include <JuceHeader.h>
#include <cstdint>
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

bool BreakpointManager::isUserBreakpoint (std::uint32_t engineId) const noexcept
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
//
ResolveResult BreakpointManager::tryResolve (const juce::String& windowsPath,
                                             std::uint32_t requestedLine) noexcept
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
    // engineBusy = symbol engine not ready (target still running) — stop.
    // notFound   = no code at this line (blank/comment) OR module not loaded.
    std::uint64_t offset       { 0 };
    std::uint32_t resolvedLine { 0 };
    bool          isResolved   { false };
    bool          isBusy       { false };

    for (std::uint32_t delta { 0 }; delta <= lineSearchWindow and not isResolved and not isBusy; ++delta)
    {
        const std::uint32_t candidate { requestedLine + delta };

        // a) Full path attempt.
        const ResolveStatus fullStatus { session.getOffsetByLine (windowsPath, candidate, &offset) };

        if (fullStatus == ResolveStatus::engineBusy)
        {
            logWrite ("WHATDBG: getOffsetByLine returned E_UNEXPECTED — symbol engine not ready\n");
            isBusy = true;
        }

        if (not isBusy and fullStatus == ResolveStatus::resolved)
        {
            resolvedLine = candidate;
            isResolved   = true;
            logWrite ("WHATDBG: resolved (full path) %s:%lu -> 0x%llX\n",
                      windowsPath.toRawUTF8 (),
                      static_cast<unsigned long> (candidate),
                      static_cast<unsigned long long> (offset));
        }

        if (not isBusy and not isResolved)
        {
            // b) Basename fallback.
            const ResolveStatus baseStatus { session.getOffsetByLine (basename, candidate, &offset) };

            if (baseStatus == ResolveStatus::engineBusy)
            {
                logWrite ("WHATDBG: getOffsetByLine returned E_UNEXPECTED — symbol engine not ready\n");
                isBusy = true;
            }

            if (not isBusy and baseStatus == ResolveStatus::resolved)
            {
                // Reverse-verify: confirm this address belongs to our source file,
                // not a same-named file in another DLL.
                juce::String  verifyFile;
                std::uint32_t verifyLine { 0 };

                const juce::Result verifyResult { session.getLineByOffset (offset, verifyFile, &verifyLine) };

                if (verifyResult.wasOk ())
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
                    logWrite ("WHATDBG: basename resolved but reverse verify failed: %s\n",
                              verifyResult.getErrorMessage ().toRawUTF8 ());
                }
            }
            else if (not isBusy and delta == 0)
            {
                // First attempt failed — no code here or module not loaded.
                // Subsequent delta attempts are silent.
                logWrite ("WHATDBG: getOffsetByLine notFound for %s:%lu\n",
                          basename.toRawUTF8 (),
                          static_cast<unsigned long> (candidate));
            }
        }
    }

    ResolveResult result {};

    if (not isResolved)
    {
        logWrite ("WHATDBG: tryResolve failed for %s:%lu (and %lu lines forward) — pending\n",
                  windowsPath.toRawUTF8 (),
                  static_cast<unsigned long> (requestedLine),
                  static_cast<unsigned long> (lineSearchWindow));
    }

    // Step 2 — Create the breakpoint at the resolved address.
    if (isResolved)
    {
        std::uint32_t     engineId  { 0 };
        const juce::Result addResult { session.addBreakpoint (offset, &engineId) };

        if (addResult.failed ())
        {
            logWrite ("WHATDBG: addBreakpoint failed: %s\n",
                      addResult.getErrorMessage ().toRawUTF8 ());
        }

        if (addResult.wasOk ())
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

juce::var BreakpointManager::onBreakpointHit (std::uint32_t engineId, std::uint32_t threadId)
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

// ---------------------------------------------------------------------------
// BreakpointManager::onBreakpointLocationsResolved
// ---------------------------------------------------------------------------

int BreakpointManager::onBreakpointLocationsResolved (std::uint32_t engineId,
                                                      std::uint32_t resolvedLine) noexcept
{
    int dapId { 0 };

    if (engineToDap.count (engineId) > 0)
    {
        dapId = static_cast<int> (engineToDap.at (engineId));

        if (breakpoints.count (static_cast<uint32_t> (dapId)) > 0)
        {
            BreakpointInfo& info { breakpoints.at (static_cast<uint32_t> (dapId)) };
            info.isVerified = true;
            info.line       = resolvedLine;
        }
    }

    return dapId;
}

} // namespace debug
