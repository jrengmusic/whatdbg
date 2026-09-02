#pragma once
#include <JuceHeader.h>
#include "Session.h"
#include "../dap/Types.h"

namespace debug
{

/** A set of one-based source line numbers requested in a single setBreakpoints call. */
using Lines = std::unordered_set<std::uint16_t>;

/** Persistent record for a single breakpoint known to the DAP client.
 *
 *  Created by onSetBreakpoints and kept alive until the client removes it
 *  or sends a new setBreakpoints for the same source file (which replaces all
 *  breakpoints for that file).
 */
struct BreakpointInfo
{
    /** Source path in backend-native form for Session::getOffsetStatus /
     *  BreakpointManager::getBreakpointOffset (backslash-separated on Windows,
     *  forward-slash on macOS).
     */
    juce::String  sourcePath;

    /** DAP-assigned breakpoint ID, reported to the client in the setBreakpoints response. */
    std::uint16_t dapId        { 0 };

    /** Requested source line number from the DAP client. */
    std::uint16_t line         { 0 };

    /** Resolved source line number, which may differ from the requested line.
     *
     *  The engine resolves breakpoints to the nearest valid instruction, so the
     *  actual line may be offset from what the client requested. 0 until resolved.
     */
    std::uint16_t resolvedLine { 0 };

    /** Engine-assigned breakpoint ID. 0 until the engine has assigned one.
     *
     *  Used to match incoming Breakpoint callback notifications and to call
     *  Session::removeBreakpoint during cleanup.
     */
    std::int32_t  engineId     { 0 };
};

/** Manages the full lifecycle of DAP breakpoints against a debug engine session.
 *
 *  Responsibilities:
 *  - Translate DAP setBreakpoints requests into engine breakpoint objects.
 *  - Retry unresolved breakpoints each time a new module loads.
 *  - Match incoming engine Breakpoint callbacks to DAP IDs for stopped events.
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
     *  - If symbols are available, resolves immediately via Session::addBreakpointByLocation.
     *  - If resolution fails (symbols not yet loaded), the entry is recorded with
     *    engineId == 0 and retried by onModuleLoad.
     *
     *  Old breakpoints for the file that are not in the new set are removed via
     *  Session::removeBreakpoint.
     *
     *  @param rawSourcePath          Raw source path from the DAP client (may use any separator).
     *  @param requestedBreakpoints   Array of DAP breakpoint objects, each with a "line" field.
     *  @return Array of DAP breakpoint response objects (verified/unverified, with resolved lines).
     */
    juce::Array<juce::var> onSetBreakpoints (const juce::String& rawSourcePath,
                                                 const juce::var&    requestedBreakpoints);

    /** Attempt to resolve all unresolved breakpoints after a new module has loaded.
     *
     *  Iterates the breakpoint registry and calls setBreakpointLocation for each
     *  entry whose engineId is 0. Successfully resolved entries have engineId and
     *  resolvedLine written in place.
     *
     *  @return Array of DAP breakpoint changed event bodies for each newly resolved breakpoint.
     *          The caller is responsible for sending these as DAP events.
     */
    juce::Array<juce::var> onModuleLoad ();

    /** Build the DAP stopped event body for a breakpoint hit.
     *
     *  Finds the tracked breakpoint whose engineId matches, then constructs a
     *  stopped-event body with reason "breakpoint" and the hitBreakpointIds array.
     *
     *  @param engineId  Engine breakpoint ID from the Breakpoint callback.
     *  @param threadId  OS thread ID of the thread that hit the breakpoint.
     *  @return DAP stopped event body object, or an empty object if engineId is not found.
     */
    juce::var onBreakpointHit (std::int32_t engineId, std::uint32_t threadId);

    /** Update an existing breakpoint to reflect async liblldb resolution.
     *
     *  Called from `processDeferredEvents` when `state.hasBreakpointLocationsResolved`
     *  fires — typically when a module loads that resolves a BP created via
     *  `Session::addBreakpointByLocation` before the module was present.
     *
     *  Updates the stored `BreakpointInfo::resolvedLine`. Returns the DAP breakpoint
     *  ID so the caller can emit a DAP `breakpoint` event with reason=`changed`.
     *
     *  @param engineId      The engine breakpoint ID.
     *  @param resolvedLine  The source line the engine resolved the BP to.
     *  @return Matching DAP BP ID, or 0 if the engineId is not tracked
     *          (e.g. spurious event for a BP we didn't create).
     */
    int onBreakpointLocationFound (std::int32_t  engineId,
                                   std::uint16_t resolvedLine) noexcept;

    /** Return true if engineId belongs to a user-registered breakpoint.
     *
     *  Used by Whatdbg to distinguish user breakpoints from the initial loader
     *  breakpoint (which has no DAP registration).
     *
     *  @param engineId  Engine breakpoint ID to check.
     *  @return true if engineId matches a tracked breakpoint's engineId.
     */
    bool isUserBreakpoint (std::int32_t engineId) const noexcept;

private:
    /** Return true if any tracked breakpoint has not yet been assigned an engine ID. */
    bool hasUnresolvedBreakpoints () const noexcept;

    /** Resolve sourcePath:requestedLine to a target offset, without creating
     *  an engine breakpoint.
     *
     *  Tries Session::getOffsetStatus / Session::getOffset, falling back to a
     *  forward search of up to lineSearchWindow lines if the exact line fails.
     *  Named and shaped after its siblings Session::getOffset (the value it
     *  resolves) and BreakpointManager::getBreakpointResponse (get returns,
     *  never stores) — the caller is responsible for calling
     *  Session::addBreakpoint on the returned offset.
     *
     *  @param sourcePath     Backend-native source path for the engine's breakpoint query.
     *  @param requestedLine  One-based source line number from the client.
     *  @return (offset, resolvedLine); offset is 0 on failure.
     */
    std::pair<std::uint64_t, std::uint16_t> getBreakpointOffset (const juce::String& sourcePath,
                                                                  std::uint16_t       requestedLine) noexcept;

    /** Resolve entryInfo's pending breakpoint location and update it in place.
     *
     *  Calls getBreakpointOffset to resolve entryInfo.sourcePath:entryInfo.line
     *  to a target offset, then creates the engine breakpoint via
     *  Session::addBreakpoint. On success, writes engineId and resolvedLine
     *  into entryInfo. No-op if resolution or breakpoint creation fails —
     *  entryInfo is left unchanged. Shared by addReloadedBreakpoints and
     *  onModuleLoad, which each apply the result differently.
     *
     *  @param entryInfo  Breakpoint entry to resolve and update in place.
     */
    void setBreakpointLocation (BreakpointInfo& entryInfo) noexcept;

    /** Remove breakpoints for normalizedKey that are no longer in requestedLines,
     *  and return the requested-line -> DAP-ID map for the lines that remain.
     *
     *  Scans the primary breakpoint registry for entries whose normalized
     *  sourcePath matches normalizedKey. Entries whose requested line is not in
     *  requestedLines are removed from the engine (Session::removeBreakpoint)
     *  and from the registry. Entries whose requested line is in requestedLines
     *  are returned so the caller can reuse their existing DAP ID.
     *
     *  @param normalizedKey    Normalized source path key for the file being processed.
     *  @param requestedLines   Set of requested lines from the current setBreakpoints call.
     *  @return Map of requested line -> DAP ID for breakpoints that remain registered.
     */
    std::unordered_map<std::uint16_t, std::uint16_t> removeOrphanedBreakpoints (
        const std::string& normalizedKey,
        const Lines&       requestedLines);

    /** Create and register a new breakpoint for a single requested line.
     *
     *  Resolves via Session::addBreakpointByLocation, records the result in the
     *  primary registry under dapId, and returns the stored entry.
     *
     *  @param rawSourcePath  Raw source path from the DAP client.
     *  @param line           Requested one-based source line number.
     *  @param dapId          DAP breakpoint ID assigned to this entry.
     *  @return The stored BreakpointInfo.
     */
    BreakpointInfo addBreakpoint (const juce::String& rawSourcePath,
                                  std::uint16_t       line,
                                  std::uint16_t       dapId);

    /** Build the DAP setBreakpoints response object for a single breakpoint entry.
     *
     *  @param info           Breakpoint entry to describe.
     *  @param rawSourcePath  Raw source path from the DAP client, echoed in the response.
     *  @return DAP breakpoint response object.
     */
    juce::var getBreakpointResponse (const BreakpointInfo& info,
                                     const juce::String&   rawSourcePath) const noexcept;

    /** Force a global symbol reload and retry resolution for every unresolved
     *  breakpoint.
     *
     *  @return Map of DAP ID -> BreakpointInfo for every entry newly resolved
     *          by this call. The caller rebuilds and replaces the matching
     *          setBreakpoints response entries via getBreakpointResponse.
     */
    std::unordered_map<std::uint16_t, BreakpointInfo> addReloadedBreakpoints ();

    /** The debug session breakpoints are created against. Not owned — must outlive this object. */
    Session& session;

    /** Primary breakpoint registry: DAP ID -> BreakpointInfo. */
    std::unordered_map<std::uint16_t, BreakpointInfo> breakpoints;

    /** Monotonically increasing counter for assigning unique DAP breakpoint IDs. */
    std::uint16_t nextDapId { 1 };

    /** Number of lines forward of the requested line to search when exact resolution fails. */
    static constexpr std::uint32_t lineSearchWindow { 4 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakpointManager)
};

} // namespace debug
