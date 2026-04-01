#include <JuceHeader.h>
#include "BreakpointManager.h"
#include "State.h"
#include "../Log.h"

namespace debug
{

using DynObj = juce::ReferenceCountedObjectPtr<juce::DynamicObject>;

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

static juce::String normalizePath (const juce::String& path) noexcept
{
    return path.replace ("\\", "/").toLowerCase ();
}

static juce::String toWindowsPath (const juce::String& path) noexcept
{
    return path.replace ("/", "\\");
}

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
    juce::Logger::writeToLog ("WHATDBG: tryResolve attempting "
        + windowsPath + ":" + juce::String (requestedLine));

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
    ULONG64 offset       { 0 };
    ULONG   resolvedLine { 0 };
    bool    isResolved   { false };

    for (ULONG delta { 0 }; delta <= kLineSearchWindow and not isResolved; ++delta)
    {
        const ULONG candidate { requestedLine + delta };

        // a) Full path attempt.
        HRESULT hrFull { session.getOffsetByLine (windowsPath, candidate, &offset) };

        if (hrFull == E_UNEXPECTED)
        {
            juce::Logger::writeToLog (
                "WHATDBG: getOffsetByLine returned E_UNEXPECTED — symbol engine not ready");
            return { 0, 0, false };
        }

        if (SUCCEEDED (hrFull))
        {
            resolvedLine = candidate;
            isResolved   = true;
            juce::Logger::writeToLog ("WHATDBG: resolved (full path) "
                + windowsPath + ":" + juce::String (candidate)
                + " -> 0x" + juce::String::toHexString (static_cast<juce::int64> (offset)));
        }

        if (not isResolved)
        {
            // b) Basename fallback.
            HRESULT hrBase { session.getOffsetByLine (basename, candidate, &offset) };

            if (hrBase == E_UNEXPECTED)
            {
                juce::Logger::writeToLog (
                    "WHATDBG: getOffsetByLine returned E_UNEXPECTED — symbol engine not ready");
                return { 0, 0, false };
            }

            if (SUCCEEDED (hrBase))
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
                    juce::Logger::writeToLog ("WHATDBG: resolved (basename) "
                        + basename + ":" + juce::String (candidate)
                        + " -> 0x" + juce::String::toHexString (static_cast<juce::int64> (offset))
                        + " (PDB: " + verifyFile + ":" + juce::String (verifyLine) + ")");
                }
                else
                {
                    juce::Logger::writeToLog (
                        "WHATDBG: basename resolved but reverse verify failed hr=0x"
                        + juce::String::toHexString (static_cast<int> (hrVerify)));
                }
            }
            else if (delta == 0)
            {
                // First attempt failed — log whether it's "no code here" or
                // "module not loaded".  Subsequent delta attempts are silent.
                juce::Logger::writeToLog ("WHATDBG: getOffsetByLine failed for "
                    + basename + ":" + juce::String (candidate)
                    + " hr=0x" + juce::String::toHexString (static_cast<int> (hrBase))
                    + (hrBase == static_cast<HRESULT> (0x80004005)
                        ? " — module not loaded or no code at line" : ""));
            }
        }
    }

    if (not isResolved)
    {
        juce::Logger::writeToLog ("WHATDBG: tryResolve failed for "
            + windowsPath + ":" + juce::String (requestedLine)
            + " (and " + juce::String (kLineSearchWindow) + " lines forward) — pending");
        return { 0, 0, false };
    }

    // Step 2 — Create the breakpoint at the resolved address.
    ULONG engineId { 0 };

    HRESULT hrAdd { session.addBreakpoint (offset, &engineId) };

    if (FAILED (hrAdd))
    {
        juce::Logger::writeToLog ("WHATDBG: addBreakpoint failed hr=0x"
            + juce::String::toHexString (static_cast<int> (hrAdd)));
        return { 0, 0, false };
    }

    engineToDap[engineId] = 0;

    juce::Logger::writeToLog ("WHATDBG: breakpoint set "
        + windowsPath + ":" + juce::String (resolvedLine)
        + " (requested " + juce::String (requestedLine) + ")"
        + " engineId=" + juce::String (engineId)
        + " offset=0x" + juce::String::toHexString (static_cast<juce::int64> (offset)));

    return { engineId, resolvedLine, true };
}

// ---------------------------------------------------------------------------
// BreakpointManager::handleSetBreakpoints
// ---------------------------------------------------------------------------

juce::Array<juce::var> BreakpointManager::handleSetBreakpoints (
    const juce::String& rawSourcePath,
    const juce::var&    requestedBreakpoints)
{
    const juce::String normalizedPath { normalizePath (rawSourcePath) };
    const std::string  normalizedKey  { normalizedPath.toStdString () };

    // Build a map of lines requested in this call so we can detect orphans.
    std::unordered_map<int, int> requestedLines;  // line -> index in requestedBreakpoints

    if (auto* arr { requestedBreakpoints.getArray () })
    {
        for (int i { 0 }; i < arr->size (); ++i)
        {
            const juce::var& bpReq { arr->getReference (i) };
            int line { 0 };
            if (auto* obj { bpReq.getDynamicObject () })
                line = static_cast<int> (obj->getProperty ("line"));
            requestedLines[line] = i;
        }
    }

    // Collect existing DAP IDs for this file so we can remove orphans.
    std::unordered_map<int, uint32_t> existingForFile;

    if (sourceBreakpoints.count (normalizedKey) > 0)
        existingForFile = sourceBreakpoints.at (normalizedKey);

    // Remove orphaned breakpoints — those in existingForFile but NOT in requestedLines.
    for (const auto& existingEntry : existingForFile)
    {
        const int      existingLine  { existingEntry.first };
        const uint32_t existingDapId { existingEntry.second };

        if (requestedLines.count (existingLine) == 0)
        {
            if (breakpoints.count (existingDapId) > 0)
            {
                const BreakpointInfo& info { breakpoints.at (existingDapId) };

                if (info.hasEngineId)
                {
                    session.removeBreakpoint (info.engineId);
                    engineToDap.erase (info.engineId);
                }

                // Also remove from pending if it was there.
                for (int i { pending.size () - 1 }; i >= 0; --i)
                {
                    if (pending[i].dapId == existingDapId)
                        pending.remove (i);
                }
                State::getContext ()->hasPendingBreakpoints = not pending.isEmpty ();

                breakpoints.erase (existingDapId);
            }
        }
    }

    // Build the new file-level index and response array.
    std::unordered_map<int, uint32_t> newFileIndex;
    juce::Array<juce::var>            responseArray;

    auto* bpsArr { requestedBreakpoints.getArray () };
    const int bpsCount { bpsArr != nullptr ? bpsArr->size () : 0 };

    for (int i { 0 }; i < bpsCount; ++i)
    {
        const juce::var& bpReq { bpsArr->getReference (i) };
        int line { 0 };
        if (auto* obj { bpReq.getDynamicObject () })
            line = static_cast<int> (obj->getProperty ("line"));

        uint32_t dapId   { 0 };
        bool     isReuse { false };

        if (existingForFile.count (line) > 0)
        {
            dapId   = existingForFile.at (line);
            isReuse = true;
        }
        else
        {
            dapId = nextDapId;
            ++nextDapId;
        }

        DynObj bpObj { new juce::DynamicObject () };
        bpObj->setProperty ("id", static_cast<int> (dapId));

        if (isReuse and breakpoints.count (dapId) > 0)
        {
            // Already tracked — return current state unchanged.
            const BreakpointInfo& existing { breakpoints.at (dapId) };
            bpObj->setProperty ("verified", existing.isVerified);
            bpObj->setProperty ("line",     static_cast<int> (existing.line));

            if (not existing.isVerified)
            {
                bpObj->setProperty ("message",
                    "WHATDBG: could not resolve breakpoint at "
                    + rawSourcePath + ":" + juce::String (line));
            }
        }
        else
        {
            // New breakpoint — attempt resolution via getOffsetByLine.
            // If the module isn't loaded yet, tryResolve returns isSuccess=false
            // and we add the breakpoint to the pending list for deferred
            // resolution when LoadModule fires.
            const juce::String windowsPath { toWindowsPath (rawSourcePath) };
            const ResolveResult result { tryResolve (windowsPath, static_cast<ULONG> (line)) };

            BreakpointInfo info {};
            info.dapId      = dapId;
            info.sourcePath = normalizedPath;
            info.line       = result.isSuccess ? result.resolvedLine : static_cast<ULONG> (line);
            info.isVerified = result.isSuccess;

            if (result.isSuccess)
            {
                info.hasEngineId      = true;
                info.engineId         = result.engineId;
                engineToDap[result.engineId] = dapId;
            }
            else
            {
                // Module not loaded yet — add to pending for deferred resolution.
                // onModuleLoad will retry getOffsetByLine after each LoadModule event.
                PendingBreakpoint pendingBp {};
                pendingBp.dapId          = dapId;
                pendingBp.sourcePath     = windowsPath;
                pendingBp.normalizedPath = normalizedPath;
                pendingBp.line           = static_cast<ULONG> (line);

                pending.add (pendingBp);
                State::getContext ()->hasPendingBreakpoints = not pending.isEmpty ();

                juce::Logger::writeToLog (
                    "WHATDBG: breakpoint pending (module not loaded) "
                    + windowsPath + ":" + juce::String (line)
                    + " dapId=" + juce::String (dapId));
            }

            breakpoints[dapId] = info;

            bpObj->setProperty ("verified", result.isSuccess);
            bpObj->setProperty ("line",     static_cast<int> (info.line));

            if (not result.isSuccess)
            {
                bpObj->setProperty ("message",
                    "WHATDBG: pending — module not loaded for "
                    + rawSourcePath + ":" + juce::String (line));
            }
        }

        // Attach source object.
        DynObj sourceObj { new juce::DynamicObject () };
        sourceObj->setProperty ("path", rawSourcePath);
        bpObj->setProperty ("source", juce::var (sourceObj));

        newFileIndex[line] = dapId;
        responseArray.add (juce::var (bpObj));
    }

    // If any BPs went pending, force global symbol reload and retry resolution.
    // This handles the case where the module was loaded before BPs were set —
    // symbols were never loaded because there were no pending BPs at load time.
    if (not pending.isEmpty ())
    {
        session.forceReloadAllSymbols ();

        juce::Array<int> resolvedIndices;

        for (int i { 0 }; i < pending.size (); ++i)
        {
            const PendingBreakpoint& pend { pending.getReference (i) };
            const ResolveResult result { tryResolve (pend.sourcePath, pend.line) };

            if (result.isSuccess)
            {
                engineToDap[result.engineId] = pend.dapId;

                if (breakpoints.count (pend.dapId) > 0)
                {
                    BreakpointInfo& info { breakpoints.at (pend.dapId) };
                    info.isVerified  = true;
                    info.hasEngineId = true;
                    info.engineId    = result.engineId;
                    info.line        = result.resolvedLine;
                }

                // Update the response entry for this BP.
                for (int r { 0 }; r < responseArray.size (); ++r)
                {
                    if (auto* respObj { responseArray.getReference (r).getDynamicObject () })
                    {
                        const int respId { static_cast<int> (respObj->getProperty ("id")) };

                        if (respId == static_cast<int> (pend.dapId))
                        {
                            respObj->setProperty ("verified", true);
                            respObj->setProperty ("line", static_cast<int> (result.resolvedLine));
                            respObj->removeProperty ("message");
                        }
                    }
                }

                resolvedIndices.add (i);

                logWrite ("WHATDBG: breakpoint resolved after symbol reload"
                          " dapId=%lu line=%lu\n",
                          static_cast<unsigned long> (pend.dapId),
                          static_cast<unsigned long> (result.resolvedLine));
            }
        }

        for (int k { resolvedIndices.size () - 1 }; k >= 0; --k)
        {
            pending.remove (resolvedIndices[k]);
        }

        State::getContext ()->hasPendingBreakpoints = not pending.isEmpty ();
    }

    // Replace the file-level index with the new one.
    sourceBreakpoints[normalizedKey] = newFileIndex;

    return responseArray;
}

// ---------------------------------------------------------------------------
// BreakpointManager::onModuleLoad
// ---------------------------------------------------------------------------

juce::Array<juce::var> BreakpointManager::onModuleLoad ()
{
    juce::Array<juce::var> events;

    // Collect resolved indices so we can remove them from pending after the loop.
    juce::Array<int> resolvedIndices;

    for (int i { 0 }; i < pending.size (); ++i)
    {
        const PendingBreakpoint& pend { pending.getReference (i) };

        // pend.sourcePath is already in Windows backslash format
        // (stored that way by handleSetBreakpoints).
        const ResolveResult result { tryResolve (pend.sourcePath, pend.line) };

        if (result.isSuccess)
        {
            // Fix up engineToDap with the real dapId.
            engineToDap[result.engineId] = pend.dapId;

            // Update the master registry — use resolvedLine (may differ from
            // requested line when tryResolve advanced past a blank/comment line).
            if (breakpoints.count (pend.dapId) > 0)
            {
                BreakpointInfo& info { breakpoints.at (pend.dapId) };
                info.isVerified  = true;
                info.hasEngineId = true;
                info.engineId    = result.engineId;
                info.line        = result.resolvedLine;
            }

            // Build DAP breakpoint changed event — report resolvedLine so
            // nvim-dap moves the gutter marker to the correct line.
            DynObj bpObj { new juce::DynamicObject () };
            bpObj->setProperty ("id",       static_cast<int> (pend.dapId));
            bpObj->setProperty ("verified", true);
            bpObj->setProperty ("line",     static_cast<int> (result.resolvedLine));

            DynObj body { new juce::DynamicObject () };
            body->setProperty ("reason",     "changed");
            body->setProperty ("breakpoint", juce::var (bpObj));

            events.add (dap::makeEvent ("breakpoint", juce::var (body)));

            resolvedIndices.add (i);

            juce::Logger::writeToLog (
                "WHATDBG: deferred breakpoint resolved on module load"
                " dapId=" + juce::String (pend.dapId)
                + " requested=" + juce::String (pend.line)
                + " resolved=" + juce::String (result.resolvedLine));
        }
    }

    // Remove resolved entries from pending — iterate in reverse to preserve indices.
    for (int k { resolvedIndices.size () - 1 }; k >= 0; --k)
        pending.remove (resolvedIndices[k]);

    State::getContext ()->hasPendingBreakpoints = not pending.isEmpty ();

    return events;
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
