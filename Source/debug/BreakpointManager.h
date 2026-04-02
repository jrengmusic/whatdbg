#pragma once
#include <JuceHeader.h>
#include "Session.h"
#include "../dap/Types.h"
#include <unordered_map>

namespace debug
{

/** Persistent record for a single breakpoint known to the DAP client.
 *
 *  Created by handleSetBreakpoints and kept alive until the client removes it
 *  or sends a new setBreakpoints for the same source file (which replaces all
 *  breakpoints for that file).
 */
struct BreakpointInfo
{
    /** DAP-assigned breakpoint ID, reported to the client in the setBreakpoints response. */
    uint32_t     dapId       { 0 };

    /** Normalized source path (forward slashes, lowercase) used as the registry key.
     *
     *  Normalization ensures consistent lookup regardless of path separator style
     *  from the DAP client.
     */
    juce::String sourcePath;

    /** Resolved source line number, which may differ from the requested line.
     *
     *  dbgeng resolves breakpoints to the nearest valid instruction, so the actual
     *  line may be offset from what the client requested. The resolved line is
     *  reported back in the breakpoint changed event.
     */
    ULONG        line        { 0 };

    /** True if the breakpoint has been resolved to an address and inserted in dbgeng. */
    bool         isVerified  { false };

    /** True if engineId contains a valid dbgeng breakpoint ID. */
    bool         hasEngineId { false };

    /** dbgeng-assigned breakpoint ID, valid only when hasEngineId is true.
     *
     *  Used to match incoming Breakpoint callback notifications and to call
     *  Session::removeBreakpoint during cleanup.
     */
    ULONG        engineId    { 0 };
};

/** Breakpoint that could not be resolved at registration time due to missing symbols.
 *
 *  Stored in the pending list and retried on every subsequent module-load event.
 */
struct PendingBreakpoint
{
    /** DAP-assigned breakpoint ID (same as the BreakpointInfo it will populate). */
    uint32_t     dapId { 0 };

    /** Windows-style source path (backslash) for passing to Session::getOffsetByLine.
     *
     *  dbgeng expects Windows paths for symbol lookups.
     */
    juce::String sourcePath;

    /** Normalized source path (forward slash, lowercase) for registry lookup.
     *
     *  Must match the key used in BreakpointManager::breakpoints.
     */
    juce::String normalizedPath;

    /** Requested source line number from the DAP client. */
    ULONG        line { 0 };
};

/** Result of a single breakpoint resolution attempt.
 *
 *  Returned by tryResolve to communicate both success state and resolved data
 *  to the caller without output parameters.
 */
struct ResolveResult
{
    /** dbgeng-assigned breakpoint ID, valid only when isSuccess is true. */
    ULONG engineId     { 0 };

    /** Actual source line the breakpoint resolved to, valid only when isSuccess is true. */
    ULONG resolvedLine { 0 };

    /** True if Session::getOffsetByLine and Session::addBreakpoint both succeeded. */
    bool  isSuccess    { false };
};

/** Manages the full lifecycle of DAP breakpoints against a dbgeng session.
 *
 *  Responsibilities:
 *  - Translate DAP setBreakpoints requests into dbgeng breakpoint objects.
 *  - Maintain a pending list for breakpoints whose symbols are not yet loaded.
 *  - Retry pending breakpoints each time a new module loads.
 *  - Match incoming dbgeng Breakpoint callbacks to DAP IDs for stopped events.
 *  - Remove stale breakpoints when a source file's breakpoint set is replaced.
 *
 *  The manager holds a reference to Session (does not own it). Session must
 *  outlive BreakpointManager.
 */
class BreakpointManager
{
public:
    /** Construct a BreakpointManager operating against the given Session.
     *
     *  @param session  The active debug session. Must outlive this object.
     */
    explicit BreakpointManager (Session& session);

    /** Process a DAP setBreakpoints request for a single source file.
     *
     *  Replaces all existing breakpoints for rawSourcePath with the new set.
     *  For each requested breakpoint:
     *  - If symbols are available, resolves immediately via Session::getOffsetByLine
     *    and Session::addBreakpoint.
     *  - If resolution fails (symbols not yet loaded), adds to the pending list and
     *    sets State::hasPendingBreakpoints.
     *
     *  Old breakpoints for the file that are not in the new set are removed via
     *  Session::removeBreakpoint.
     *
     *  @param rawSourcePath          Raw source path from the DAP client (may use any separator).
     *  @param requestedBreakpoints   Array of DAP breakpoint objects, each with a "line" field.
     *  @return Array of DAP breakpoint response objects (verified/unverified, with resolved lines).
     */
    juce::Array<juce::var> handleSetBreakpoints (const juce::String& rawSourcePath,
                                                 const juce::var&    requestedBreakpoints);

    /** Attempt to resolve all pending breakpoints after a new module has loaded.
     *
     *  Iterates the pending list and calls tryResolve for each entry. Successfully
     *  resolved entries are promoted to verified BreakpointInfo records and removed
     *  from the pending list. Unresolved entries remain pending.
     *
     *  @return Array of DAP breakpoint changed event bodies for each newly resolved breakpoint.
     *          The caller is responsible for sending these as DAP events.
     */
    juce::Array<juce::var> onModuleLoad ();

    /** Build the DAP stopped event body for a breakpoint hit.
     *
     *  Looks up engineId in the engine-to-DAP map to find the corresponding DAP
     *  breakpoint ID, then constructs a stopped-event body with reason "breakpoint"
     *  and the hitBreakpointIds array.
     *
     *  @param engineId  dbgeng breakpoint ID from the Breakpoint callback.
     *  @param threadId  OS thread ID of the thread that hit the breakpoint.
     *  @return DAP stopped event body object, or an empty object if engineId is not found.
     */
    juce::var onBreakpointHit (ULONG engineId, ULONG threadId);

    /** Return true if there are breakpoints waiting to be resolved.
     *
     *  Read by EventCallbacks::LoadModule to decide whether to set
     *  State::hasNewModuleLoaded. Avoids unnecessary module-load processing
     *  when no breakpoints are pending.
     *
     *  @return true if the pending list is non-empty.
     */
    bool hasPending () const noexcept;

    /** Return true if engineId belongs to a user-registered breakpoint.
     *
     *  Used by EventCallbacks::Breakpoint to distinguish user breakpoints from
     *  the initial loader breakpoint (which has no DAP registration).
     *
     *  @param engineId  dbgeng breakpoint ID to check.
     *  @return true if engineId is in the engine-to-DAP mapping.
     */
    bool isUserBreakpoint (ULONG engineId) const noexcept;

private:
    /** Attempt to resolve a single breakpoint to a dbgeng breakpoint object.
     *
     *  Tries to resolve windowsPath:requestedLine via Session::getOffsetByLine,
     *  falling back to a window search of +/- kLineSearchWindow lines if the
     *  exact line fails. On success calls Session::addBreakpoint.
     *
     *  @param windowsPath    Windows-style source path for getOffsetByLine.
     *  @param requestedLine  One-based source line number from the client.
     *  @return ResolveResult with isSuccess=true and populated fields on success.
     */
    ResolveResult tryResolve (const juce::String& windowsPath, ULONG requestedLine) noexcept;

    Session& session;

    /** Primary breakpoint registry: DAP ID -> BreakpointInfo. */
    std::unordered_map<uint32_t, BreakpointInfo>                          breakpoints;

    /** Per-source breakpoint index: normalized path -> (line -> DAP ID).
     *
     *  Used to quickly find existing breakpoints for a file when processing a
     *  new setBreakpoints request so that stale entries can be removed.
     */
    std::unordered_map<std::string, std::unordered_map<int, uint32_t>>    sourceBreakpoints;

    /** Engine-to-DAP ID mapping: dbgeng breakpoint ID -> DAP ID.
     *
     *  Populated when a breakpoint is successfully added via Session::addBreakpoint.
     *  Consulted by EventCallbacks::Breakpoint to identify user breakpoints.
     */
    std::unordered_map<ULONG, uint32_t>                                   engineToDap;

    /** Breakpoints awaiting symbol load before they can be resolved. */
    juce::Array<PendingBreakpoint>                                        pending;

    /** Monotonically increasing counter for assigning unique DAP breakpoint IDs. */
    uint32_t nextDapId { 1 };

    /** Number of lines above and below the requested line to search when exact resolution fails. */
    static constexpr ULONG kLineSearchWindow { 4 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakpointManager)
};

} // namespace debug
