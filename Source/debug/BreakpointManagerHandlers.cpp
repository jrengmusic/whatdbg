/** @file BreakpointManagerHandlers.cpp
 *  @brief Breakpoint event handlers — hit, resolved, and modified notifications.
 *
 *  Processes breakpoint events from the debug engine and updates BreakpointManager
 *  state. Produces DAP breakpoint events for the adapter to emit.
 */

#include <JuceHeader.h>
#include <cstdint>
#include "BreakpointManager.h"
#include "State.h"
#include "../Log.h"

namespace debug
{

using dap::DynObj;

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

/** Converts backslashes to forward slashes and lowercases the path for cross-platform key comparison. */
static juce::String normalizePath (const juce::String& path) noexcept
{
    return path.replace ("\\", "/").toLowerCase ();
}

/** Converts forward slashes to backslashes for Windows symbol engine queries. */
static juce::String toWindowsPath (const juce::String& path) noexcept
{
    return path.replace ("/", "\\");
}

// ---------------------------------------------------------------------------
// BreakpointManager::handleSetBreakpoints
// ---------------------------------------------------------------------------

/** Processes a DAP setBreakpoints request for one source file.
 *  Removes breakpoints absent from the new request, reuses existing ones for unchanged lines,
 *  and creates new breakpoints for novel lines. Pending BPs trigger a forced symbol reload
 *  and immediate retry before returning the response array. */
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
                    juce::ignoreUnused (session.removeBreakpoint (info.engineId));
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
            // New breakpoint — create via platform-native BP-by-location.
            // Windows: wraps getOffsetByLine + addBreakpoint(offset). Returns
            //   fail if module not loaded → we add to pending and onModuleLoad
            //   retries on each LoadModule event.
            // macOS: always succeeds; resolvedLine == 0 means liblldb will
            //   auto-resolve as modules load (no adapter retry — lldb tracks).
           #if JUCE_WINDOWS
            const juce::String resolvePath { toWindowsPath (rawSourcePath) };
           #else
            const juce::String resolvePath { rawSourcePath };
           #endif

            std::uint32_t engineId     { 0 };
            std::uint32_t resolvedLine { 0 };
            const juce::Result createResult {
                session.addBreakpointByLocation (resolvePath,
                                                 static_cast<std::uint32_t> (line),
                                                 &engineId,
                                                 &resolvedLine) };

            const bool isImmediatelyResolved { createResult.wasOk () and resolvedLine != 0 };
            const bool isDeferredPending     { createResult.wasOk () and resolvedLine == 0 };

            BreakpointInfo info {};
            info.dapId      = dapId;
            info.sourcePath = normalizedPath;
            info.line       = isImmediatelyResolved ? resolvedLine
                                                    : static_cast<std::uint32_t> (line);
            info.isVerified = isImmediatelyResolved;

            if (isImmediatelyResolved)
            {
                info.hasEngineId      = true;
                info.engineId         = engineId;
                engineToDap[engineId] = dapId;
            }
            else if (isDeferredPending)
            {
                // macOS branch: liblldb tracks the BP, will resolve on module
                // load. Still record engineId so subsequent removeBreakpoint
                // can reach the lldb BP.
                info.hasEngineId      = true;
                info.engineId         = engineId;
                engineToDap[engineId] = dapId;
            }
            else
            {
                // Windows branch: getOffsetByLine said not-found (or addBreakpoint
                // failed). Fall back to pending-list retry via onModuleLoad.
                PendingBreakpoint pendingBp {};
                pendingBp.dapId          = dapId;
                pendingBp.sourcePath     = resolvePath;
                pendingBp.normalizedPath = normalizedPath;
                pendingBp.line           = static_cast<std::uint32_t> (line);

                pending.add (pendingBp);
                State::getContext ()->hasPendingBreakpoints = not pending.isEmpty ();

                logWrite ("WHATDBG: breakpoint pending (module not loaded) %s:%d dapId=%lu\n",
                          resolvePath.toRawUTF8 (),
                          line,
                          static_cast<unsigned long> (dapId));
            }

            breakpoints[dapId] = info;

            bpObj->setProperty ("verified", isImmediatelyResolved);
            bpObj->setProperty ("line",     static_cast<int> (info.line));

            if (not isImmediatelyResolved)
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
        juce::ignoreUnused (session.forceReloadAllSymbols ());

        juce::Array<int> resolvedIndices;

        for (int i { 0 }; i < pending.size (); ++i)
        {
            const PendingBreakpoint& pend   { pending.getReference (i) };
            const ResolveResult      result { tryResolve (pend.sourcePath, pend.line) };

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
            pending.remove (resolvedIndices[k]);

        State::getContext ()->hasPendingBreakpoints = not pending.isEmpty ();
    }

    // Replace the file-level index with the new one.
    sourceBreakpoints[normalizedKey] = newFileIndex;

    return responseArray;
}

// ---------------------------------------------------------------------------
// BreakpointManager::onModuleLoad
// ---------------------------------------------------------------------------

/** Retries resolution of all pending breakpoints after a module load event.
 *  For each newly resolved BP, updates the master registry and returns a DAP
 *  breakpoint changed event so nvim-dap can move the gutter marker. */
juce::Array<juce::var> BreakpointManager::onModuleLoad ()
{
    juce::Array<juce::var> events;

    // Collect resolved indices so we can remove them from pending after the loop.
    juce::Array<int> resolvedIndices;

    for (int i { 0 }; i < pending.size (); ++i)
    {
        const PendingBreakpoint& pend { pending.getReference (i) };

        // pend.sourcePath is stored in backend-native form
        // (backslash on Windows, forward slash on macOS).
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

            logWrite ("WHATDBG: deferred breakpoint resolved on module load"
                      " dapId=%lu requested=%lu resolved=%lu\n",
                      static_cast<unsigned long> (pend.dapId),
                      static_cast<unsigned long> (pend.line),
                      static_cast<unsigned long> (result.resolvedLine));
        }
    }

    // Remove resolved entries from pending — iterate in reverse to preserve indices.
    for (int k { resolvedIndices.size () - 1 }; k >= 0; --k)
        pending.remove (resolvedIndices[k]);

    State::getContext ()->hasPendingBreakpoints = not pending.isEmpty ();

    return events;
}

} // namespace debug
