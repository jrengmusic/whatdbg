#include "BreakpointManager.hpp"
#include "DapTypes.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// BreakpointManager::BreakpointManager
// ---------------------------------------------------------------------------

BreakpointManager::BreakpointManager (IDebugControl4* ctrl, IDebugSymbols3* sym)
    : control (ctrl)
    , symbols (sym)
{
}

// ---------------------------------------------------------------------------
// BreakpointManager::normalizePath
// ---------------------------------------------------------------------------

std::string BreakpointManager::normalizePath (const std::string& path)
{
    std::string normalized { path };

    std::replace (normalized.begin (), normalized.end (), '\\', '/');

    std::transform (normalized.begin (), normalized.end (), normalized.begin (),
        [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });

    return normalized;
}

std::string BreakpointManager::toWindowsPath (const std::string& path)
{
    std::string windowsPath { path };
    std::replace (windowsPath.begin (), windowsPath.end (), '/', '\\');
    return windowsPath;
}

// ---------------------------------------------------------------------------
// BreakpointManager::tryResolve
// ---------------------------------------------------------------------------

// Returns {engineId, isSuccess}. engineId 0 is valid — don't use it as failure sentinel.
//
// Uses GetOffsetByLine to resolve file:line to a direct code address, then
// creates the breakpoint with SetOffset.
//
// Resolution strategy (ordered by specificity):
//   1. Full Windows path — exact match, no ambiguity.
//   2. Filename only — lets dbgeng search all loaded PDBs by basename.
//      This is necessary because PDBs may store relative paths that don't
//      match the absolute path nvim-dap sends.  The basename fallback is
//      safe here (unlike SetOffsetExpression) because we verify the resolved
//      address with GetLineByOffset before creating the breakpoint.
//
// Why NOT SetOffsetExpression:
//   SetOffsetExpression with backtick source-line syntax uses partial
//   filename matching and creates deferred breakpoints that dbgeng
//   re-evaluates on every module load.  In multi-DLL processes (REAPER
//   loads 100+ JUCE-based DLLs), it resolves to wrong addresses → crash.
BreakpointManager::ResolveResult BreakpointManager::tryResolve (const std::string& sourcePath, int line)
{
    fprintf (stderr, "WHATDBG: tryResolve attempting %s:%d\n", sourcePath.c_str (), line);

    // Extract basename once — used as fallback when PDB stores relative paths.
    std::string basename { sourcePath };
    {
        const auto lastSep { sourcePath.rfind ('\\') };
        if (lastSep != std::string::npos)
            basename = sourcePath.substr (lastSep + 1);
    }

    // Step 1 — Resolve file:line to a code address.
    //
    // Strategy: try the requested line first, then advance up to
    // kLineSearchWindow lines forward.  This handles blank lines, comments,
    // and closing braces that have no associated machine code in the PDB.
    //
    // For each candidate line:
    //   a) Try full path (exact match, no cross-module ambiguity).
    //   b) Try basename (handles PDBs that store relative paths).
    //      Verify the resolved address with GetLineByOffset to prevent
    //      wrong-module hits in multi-DLL processes like REAPER.
    //
    // E_FAIL  = no code at this line (blank/comment) OR module not loaded.
    // E_UNEXPECTED = symbol engine not ready (target still running) — stop.
    ULONG64 offset      { 0 };
    int     resolvedLine { 0 };
    bool    isResolved  { false };

    for (int delta { 0 }; delta <= kLineSearchWindow and not isResolved; ++delta)
    {
        const int candidate { line + delta };

        // a) Full path attempt.
        HRESULT hrFull { symbols->GetOffsetByLine (
            static_cast<ULONG> (candidate),
            sourcePath.c_str (),
            &offset) };

        if (hrFull == E_UNEXPECTED)
        {
            // Symbol engine not ready — no point trying further lines.
            fprintf (stderr,
                    "WHATDBG: GetOffsetByLine returned E_UNEXPECTED — symbol engine not ready\n");
            return { 0, 0, false };
        }

        if (SUCCEEDED (hrFull))
        {
            resolvedLine = candidate;
            isResolved   = true;
            fprintf (stderr,
                    "WHATDBG: resolved (full path) %s:%d → 0x%llX\n",
                    sourcePath.c_str (), candidate,
                    static_cast<unsigned long long> (offset));
            break;
        }

        // b) Basename fallback.
        HRESULT hrBase { symbols->GetOffsetByLine (
            static_cast<ULONG> (candidate),
            basename.c_str (),
            &offset) };

        if (hrBase == E_UNEXPECTED)
        {
            fprintf (stderr,
                    "WHATDBG: GetOffsetByLine returned E_UNEXPECTED — symbol engine not ready\n");
            return { 0, 0, false };
        }

        if (SUCCEEDED (hrBase))
        {
            // Reverse-verify: confirm this address belongs to our source file,
            // not a same-named file in another DLL.
            ULONG verifyLine { 0 };
            char  verifyFile[512] { };

            HRESULT hrVerify { symbols->GetLineByOffset (
                offset, &verifyLine, verifyFile, sizeof (verifyFile), nullptr, nullptr) };

            if (SUCCEEDED (hrVerify))
            {
                resolvedLine = static_cast<int> (verifyLine);
                isResolved   = true;
                fprintf (stderr,
                        "WHATDBG: resolved (basename) %s:%d → 0x%llX (PDB: %s:%lu)\n",
                        basename.c_str (), candidate,
                        static_cast<unsigned long long> (offset),
                        verifyFile,
                        static_cast<unsigned long> (verifyLine));
            }
            else
            {
                fprintf (stderr,
                        "WHATDBG: basename resolved but reverse verify failed hr=0x%08lX\n",
                        static_cast<unsigned long> (hrVerify));
            }
        }
        else if (delta == 0)
        {
            // First attempt failed — log so we know whether it's "no code here"
            // or "module not loaded".  Subsequent delta attempts are silent.
            fprintf (stderr,
                    "WHATDBG: GetOffsetByLine failed for %s:%d hr=0x%08lX%s\n",
                    basename.c_str (), candidate,
                    static_cast<unsigned long> (hrBase),
                    hrBase == static_cast<HRESULT> (0x80004005) ? " — module not loaded or no code at line" : "");
        }
    }

    if (not isResolved)
    {
        if (resolvedLine == 0)
        {
            fprintf (stderr,
                    "WHATDBG: tryResolve failed for %s:%d (and %d lines forward) — pending\n",
                    sourcePath.c_str (), line, kLineSearchWindow);
        }
        return { 0, 0, false };
    }

    // Step 2 — Create the breakpoint object.
    IDebugBreakpoint2* bp { nullptr };

    HRESULT hrAdd { control->AddBreakpoint2 (DEBUG_BREAKPOINT_CODE, DEBUG_ANY_ID, &bp) };

    if (FAILED (hrAdd) or bp == nullptr)
    {
        fprintf (stderr,
                "WHATDBG: AddBreakpoint2 failed hr=0x%08lX\n",
                static_cast<unsigned long> (hrAdd));
        return { 0, 0, false };
    }

    // Step 3 — Set the direct address.
    HRESULT hrOffset { bp->SetOffset (offset) };

    if (FAILED (hrOffset))
    {
        fprintf (stderr,
                "WHATDBG: SetOffset failed for %s:%d offset=0x%llX hr=0x%08lX\n",
                sourcePath.c_str (), resolvedLine,
                static_cast<unsigned long long> (offset),
                static_cast<unsigned long> (hrOffset));
        control->RemoveBreakpoint2 (bp);
        return { 0, 0, false };
    }

    bp->AddFlags (DEBUG_BREAKPOINT_ENABLED);

    ULONG engineId { 0 };
    bp->GetId (&engineId);
    engineToDap[engineId] = 0;

    fprintf (stderr,
            "WHATDBG: breakpoint set %s:%d (requested %d) engineId=%lu offset=0x%llX\n",
            sourcePath.c_str (), resolvedLine, line,
            static_cast<unsigned long> (engineId),
            static_cast<unsigned long long> (offset));

    return { engineId, resolvedLine, true };
}

// ---------------------------------------------------------------------------
// BreakpointManager::removeEngineBreakpoint
// ---------------------------------------------------------------------------

void BreakpointManager::removeEngineBreakpoint (ULONG engineId)
{
    IDebugBreakpoint2* bp { nullptr };

    HRESULT hr { control->GetBreakpointById2 (engineId, &bp) };

    if (SUCCEEDED (hr) and bp != nullptr)
    {
        control->RemoveBreakpoint2 (bp);
    }
}

// ---------------------------------------------------------------------------
// BreakpointManager::handleSetBreakpoints
// ---------------------------------------------------------------------------

nlohmann::json BreakpointManager::handleSetBreakpoints (const nlohmann::json& args)
{
    nlohmann::json requestArgs { args.value ("arguments", nlohmann::json::object ()) };

    // Extract source path
    nlohmann::json sourceObj { requestArgs.value ("source", nlohmann::json::object ()) };
    std::string rawPath      { sourceObj.value ("path", std::string {}) };
    std::string normalizedPath { normalizePath (rawPath) };

    // Extract requested breakpoints array
    nlohmann::json requestedBps { requestArgs.value ("breakpoints", nlohmann::json::array ()) };

    // Build a set of lines requested in this call so we can detect orphans
    std::unordered_map<int, int> requestedLines;  // line -> index in requestedBps

    for (int i { 0 }; i < static_cast<int> (requestedBps.size ()); ++i)
    {
        int line { requestedBps.at (static_cast<size_t> (i)).value ("line", 0) };
        requestedLines[line] = i;
    }

    // Collect existing DAP IDs for this file so we can remove orphans
    std::unordered_map<int, uint32_t> existingForFile;

    if (sourceBreakpoints.count (normalizedPath) > 0)
    {
        existingForFile = sourceBreakpoints.at (normalizedPath);
    }

    // Remove orphaned breakpoints — those in existingForFile but NOT in requestedLines
    for (const auto& existingEntry : existingForFile)
    {
        int      existingLine  { existingEntry.first };
        uint32_t existingDapId { existingEntry.second };

        if (requestedLines.count (existingLine) == 0)
        {
            // This line is no longer requested — remove it
            if (breakpoints.count (existingDapId) > 0)
            {
                const BreakpointInfo& info { breakpoints.at (existingDapId) };

                if (info.hasEngineId == true)
                {
                    removeEngineBreakpoint (info.engineId);
                    engineToDap.erase (info.engineId);
                }

                // Also remove from pending if it was there
                pending.erase (
                    std::remove_if (pending.begin (), pending.end (),
                        [existingDapId] (const PendingBreakpoint& p)
                        {
                            return p.dapId == existingDapId;
                        }),
                    pending.end ());

                breakpoints.erase (existingDapId);
            }
        }
    }

    // Build the new file-level index and response array
    std::unordered_map<int, uint32_t> newFileIndex;
    nlohmann::json responseArray { nlohmann::json::array () };

    for (size_t i { 0 }; i < requestedBps.size (); ++i)
    {
        const nlohmann::json& bpReq { requestedBps.at (i) };
        int line { bpReq.value ("line", 0) };

        uint32_t dapId { 0 };
        bool     isReuse { false };

        // Reuse existing DAP ID if this line was already tracked
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

        nlohmann::json bpResponse;
        bpResponse["id"] = static_cast<int> (dapId);

        if (isReuse == true and breakpoints.count (dapId) > 0)
        {
            // Already tracked — return current state unchanged
            const BreakpointInfo& existing { breakpoints.at (dapId) };
            bpResponse["verified"] = existing.isVerified;
            bpResponse["line"]     = existing.line;

            if (existing.isVerified == false)
            {
                bpResponse["message"] = "WHATDBG: could not resolve breakpoint at "
                    + rawPath + ":" + std::to_string (line);
            }
        }
        else
        {
            // New breakpoint — attempt resolution via GetOffsetByLine.
            // If the module isn't loaded yet, tryResolve returns isSuccess=false
            // and we add the breakpoint to the pending list for deferred
            // resolution when LoadModule fires.
            std::string windowsPath { toWindowsPath (rawPath) };
            auto [engineId, resolvedLine, isCreated] { tryResolve (windowsPath, line) };

            BreakpointInfo info {};
            info.dapId      = dapId;
            info.sourcePath = normalizedPath;
            info.line       = isCreated ? resolvedLine : line;
            info.isVerified = isCreated;

            if (isCreated == true)
            {
                info.hasEngineId = true;
                info.engineId    = engineId;
                engineToDap[engineId] = dapId;
            }
            else
            {
                // Module not loaded yet — add to pending for deferred resolution.
                // onModuleLoad will retry GetOffsetByLine after each LoadModule event.
                PendingBreakpoint pendingBp {};
                pendingBp.dapId          = dapId;
                pendingBp.sourcePath     = windowsPath;
                pendingBp.normalizedPath = normalizedPath;
                pendingBp.line           = line;

                pending.push_back (pendingBp);

                fprintf (stderr,
                        "WHATDBG: breakpoint pending (module not loaded) %s:%d dapId=%u\n",
                        windowsPath.c_str (),
                        line,
                        static_cast<unsigned> (dapId));
            }

            breakpoints[dapId] = info;

            bpResponse["verified"] = isCreated;
            bpResponse["line"]     = info.line;

            if (isCreated == false)
            {
                bpResponse["message"] = "WHATDBG: pending — module not loaded for "
                    + rawPath + ":" + std::to_string (line);
            }
        }

        newFileIndex[line] = dapId;
        responseArray.push_back (bpResponse);
    }

    // Replace the file-level index with the new one
    sourceBreakpoints[normalizedPath] = newFileIndex;

    return responseArray;
}

// ---------------------------------------------------------------------------
// BreakpointManager::onModuleLoad
// ---------------------------------------------------------------------------

std::vector<nlohmann::json> BreakpointManager::onModuleLoad (const std::string& moduleName)
{
    std::vector<nlohmann::json> events;

    // Iterate pending list in reverse so we can erase by index safely
    // (we collect indices to erase, then erase after the loop)
    std::vector<size_t> resolvedIndices;

    for (size_t i { 0 }; i < pending.size (); ++i)
    {
        const PendingBreakpoint& pend { pending.at (i) };

        // pend.sourcePath is already in Windows backslash format
        // (stored that way by handleSetBreakpoints).
        auto [engineId, resolvedLine, isCreated] { tryResolve (pend.sourcePath, pend.line) };

        if (isCreated == true)
        {
            // Fix up engineToDap with the real dapId
            engineToDap[engineId] = pend.dapId;

            // Update the master registry — use resolvedLine (may differ from
            // requested line when tryResolve advanced past a blank/comment line)
            if (breakpoints.count (pend.dapId) > 0)
            {
                BreakpointInfo& info { breakpoints.at (pend.dapId) };
                info.isVerified  = true;
                info.hasEngineId = true;
                info.engineId    = engineId;
                info.line        = resolvedLine;
            }

            // Build DAP breakpoint changed event — report resolvedLine so
            // nvim-dap moves the gutter marker to the correct line
            nlohmann::json bpBody;
            bpBody["reason"] = "changed";

            nlohmann::json bpObj;
            bpObj["id"]       = static_cast<int> (pend.dapId);
            bpObj["verified"] = true;
            bpObj["line"]     = resolvedLine;

            bpBody["breakpoint"] = bpObj;

            events.push_back (dap::makeEvent ("breakpoint", bpBody));

            resolvedIndices.push_back (i);

            fprintf (stderr,
                    "WHATDBG: deferred breakpoint resolved on module load (%s) "
                    "dapId=%u requested=%d resolved=%d\n",
                    moduleName.c_str (),
                    static_cast<unsigned> (pend.dapId),
                    pend.line,
                    resolvedLine);
        }
    }

    // Remove resolved entries from pending (iterate in reverse to preserve indices)
    for (size_t k { resolvedIndices.size () }; k > 0; --k)
    {
        pending.erase (pending.begin ()
            + static_cast<std::ptrdiff_t> (resolvedIndices.at (k - 1)));
    }

    return events;
}

// ---------------------------------------------------------------------------
// BreakpointManager::hasPending
// ---------------------------------------------------------------------------

bool BreakpointManager::hasPending () const
{
    return pending.empty () == false;
}

// ---------------------------------------------------------------------------
// BreakpointManager::onBreakpointHit
// ---------------------------------------------------------------------------

nlohmann::json BreakpointManager::onBreakpointHit (IDebugBreakpoint2* bp, ULONG threadId)
{
    nlohmann::json body {};
    body["reason"]            = "breakpoint";
    body["threadId"]          = static_cast<int> (threadId);
    body["allThreadsStopped"] = true;

    nlohmann::json hitIds { nlohmann::json::array () };

    if (bp != nullptr)
    {
        ULONG engineId { 0 };
        bp->GetId (&engineId);

        if (engineToDap.count (engineId) > 0)
        {
            uint32_t dapId { engineToDap.at (engineId) };
            hitIds.push_back (static_cast<int> (dapId));
        }
    }

    body["hitBreakpointIds"] = hitIds;

    return body;
}
