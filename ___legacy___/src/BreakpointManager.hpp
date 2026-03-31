#pragma once

#include <windows.h>
#include <dbgeng.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../third_party/nlohmann/json.hpp"

// ---------------------------------------------------------------------------
// BreakpointManager
//
// Owns the mapping between DAP breakpoint IDs and dbgeng IDebugBreakpoint2
// objects.  Handles deferred resolution for the attach-to-DAW workflow where
// the plugin DLL loads after the debug session starts.
//
// Lifetime contract:
//   - Constructed with valid IDebugControl4* and IDebugSymbols3* pointers.
//   - Both interface pointers must remain valid for the lifetime of this object.
//   - All methods must be called from the main (dbgeng event loop) thread.
// ---------------------------------------------------------------------------

class BreakpointManager
{
public:
    BreakpointManager (IDebugControl4* control, IDebugSymbols3* symbols);

    // Handle setBreakpoints request — replace-all-for-file semantics.
    // args is the full DAP request object (contains "arguments").
    // Returns the DAP breakpoints array for the response body.
    nlohmann::json handleSetBreakpoints (const nlohmann::json& args);

    // Called from LoadModule callback — re-resolve pending breakpoints
    // against the newly loaded module.
    // moduleName is the image name string from the LoadModule event.
    // Returns list of DAP breakpoint changed events to send to the client.
    std::vector<nlohmann::json> onModuleLoad (const std::string& moduleName);

    // True when there are unresolved breakpoints waiting for a module to load.
    bool hasPending () const;

    // Called from Breakpoint callback — build the DAP stopped event body.
    // threadId comes from IDebugSystemObjects::GetCurrentThreadId.
    nlohmann::json onBreakpointHit (IDebugBreakpoint2* bp, ULONG threadId);

private:
    // Per-breakpoint state tracked by the manager.
    struct BreakpointInfo
    {
        uint32_t    dapId       { 0 };
        std::string sourcePath;          // normalized path
        int         line        { 0 };
        bool        isVerified  { false };
        bool        hasEngineId { false };  // true when a dbgeng breakpoint exists
        ULONG       engineId    { 0 };     // dbgeng breakpoint ID (valid only when hasEngineId is true)
    };

    // A breakpoint that could not be resolved at set-time.
    // Stored until the target module loads, then re-resolved via onModuleLoad.
    struct PendingBreakpoint
    {
        uint32_t    dapId { 0 };
        std::string sourcePath;      // raw path (Windows backslash format for GetOffsetByLine)
        std::string normalizedPath;  // normalized path (for master registry lookup)
        int         line  { 0 };
    };

    IDebugControl4* control;
    IDebugSymbols3* symbols;

    uint32_t nextDapId { 1 };

    // Master registry: DAP ID -> info
    std::unordered_map<uint32_t, BreakpointInfo> breakpoints;

    // File-level index: normalized path -> { line -> dapId }
    std::unordered_map<std::string, std::unordered_map<int, uint32_t>> sourceBreakpoints;

    // Pending (unresolved) breakpoints awaiting deferred resolution
    std::vector<PendingBreakpoint> pending;

    // Engine ID -> DAP ID reverse lookup (for Breakpoint callback)
    std::unordered_map<ULONG, uint32_t> engineToDap;

    // Result of a tryResolve attempt.
    struct ResolveResult
    {
        ULONG engineId    { 0 };
        int   resolvedLine { 0 };   // actual line where the BP was placed (may differ from requested)
        bool  isSuccess   { false };
    };

    // Try to resolve a breakpoint at file:line via GetOffsetByLine + SetOffset.
    // If the requested line has no code (blank/comment), advances up to
    // kLineSearchWindow lines forward to find the next valid statement.
    // Returns isSuccess=true and the actual resolved line on success.
    ResolveResult tryResolve (const std::string& sourcePath, int line);

    // Maximum number of lines to advance when the requested line has no code.
    static constexpr int kLineSearchWindow { 4 };

    // Remove a dbgeng breakpoint by engine ID.
    // Looks up the IDebugBreakpoint2 via GetBreakpointById2 then calls RemoveBreakpoint2.
    void removeEngineBreakpoint (ULONG engineId);

    // Normalize a file path for consistent lookup:
    // backslashes -> forward slashes, all lowercase.
    static std::string normalizePath (const std::string& path);

    // Convert a path to Windows backslash format for dbgeng API calls.
    static std::string toWindowsPath (const std::string& path);
};
