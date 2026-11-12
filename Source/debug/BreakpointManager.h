#pragma once
#include <JuceHeader.h>
#include "Session.h"
#include "../dap/Types.h"

namespace debug
{

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

    /** Engine-assigned breakpoint ID. Empty until the engine has assigned one.
     *
     *  Used to match a stop or a location change to its entry, and to call
     *  Session::removeBreakpoint during cleanup.
     */
    std::optional<std::int32_t> engineId {};
};

/** Manages the full lifecycle of DAP breakpoints against a debug engine session.
 *
 *  Responsibilities:
 *  - Translate DAP setBreakpoints requests into engine breakpoint objects.
 *  - Retry unresolved breakpoints when a module loads or symbols reload.
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
     *  - If resolution fails (symbols not yet loaded), the entry is recorded with an
     *    empty engineId. onReload and onModuleLoad retry it.
     *
     *  Old breakpoints for the file that are not in the new set are removed via
     *  Session::removeBreakpoint. This method does not reload symbols. Whatdbg calls
     *  onReload after it writes the response.
     *
     *  @param rawSourcePath          Raw source path from the DAP client (may use any separator).
     *  @param requestedBreakpoints   Array of DAP breakpoint objects, each with a "line" field.
     *  @return Array of DAP breakpoint response objects (verified/unverified, with resolved lines).
     */
    juce::Array<juce::var> onSetBreakpoints (const juce::String& rawSourcePath,
                                                 const juce::var&    requestedBreakpoints);

    /** Reload the symbols of one module and resolve breakpoints that are still unresolved.
     *
     *  Calls Session::loadModuleSymbols for imageName only when an entry has an empty
     *  engineId. Then resolves every unresolved entry through onSymbolsLoaded.
     *
     *  @param imageName  Image file name of the module that loaded, from State::lastLoadedImageName.
     *  @return Array of DAP breakpoint(changed) events, one per newly resolved entry.
     *          The caller is responsible for sending these as DAP events.
     */
    juce::Array<juce::var> onModuleLoad (const juce::String& imageName);

    /** Reload all symbols and resolve breakpoints that are still unresolved.
     *
     *  Calls Session::forceReloadAllSymbols only when an entry has an empty engineId.
     *  Then resolves every unresolved entry through onSymbolsLoaded.
     *
     *  @return Array of DAP breakpoint(changed) events, one per newly resolved entry.
     */
    juce::Array<juce::var> onReload ();

    /** Build the DAP stopped event for a breakpoint or step stop.
     *
     *  Gives reason "breakpoint" with hitBreakpointIds when an entry holds engineId.
     *  Gives reason "step" when no entry holds engineId.
     *
     *  @param engineId  Engine breakpoint ID from State::breakpointEngineId.
     *  @param threadId  OS thread ID of the thread that stopped.
     *  @return DAP stopped event.
     */
    juce::var getBreakpointStoppedEvent (std::int32_t engineId, std::uint32_t threadId) const;

    /** Update the stored line of a tracked breakpoint after asynchronous liblldb resolution.
     *
     *  Called from Whatdbg::drainBreakpointLocationResolved when
     *  State::hasBreakpointLocationsResolved is set. Writes resolvedLine into the
     *  entry that holds engineId.
     *
     *  @param engineId      The engine breakpoint ID.
     *  @param resolvedLine  The source line the engine resolved the BP to.
     *  @return Array holding one DAP breakpoint(changed) event for the matched entry,
     *          or an empty array when no entry holds engineId.
     */
    juce::Array<juce::var> onBreakpointLocationFound (std::int32_t  engineId,
                                                      std::uint16_t resolvedLine);

private:
    /** Return true if any tracked breakpoint has not yet been assigned an engine ID. */
    bool hasUnresolvedBreakpoints () const noexcept;

    /** Resolve every entry without an engineId through setBreakpointLocation.
     *
     *  @return Array of DAP breakpoint(changed) events, one per entry that resolved.
     */
    juce::Array<juce::var> onSymbolsLoaded ();

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
     *  entryInfo is left unchanged. Called by onSymbolsLoaded for each unresolved entry.
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
     *  @param requestedLines   Requested lines from the current setBreakpoints call.
     *  @return Map of requested line -> DAP ID for breakpoints that remain registered.
     */
    std::unordered_map<std::uint16_t, std::uint16_t> removeOrphanedBreakpoints (
        const juce::String&               normalizedKey,
        const std::vector<std::uint16_t>& requestedLines);

    /** Create and register a new breakpoint for a single requested line.
     *
     *  Resolves via Session::addBreakpointByLocation and records the entry in the
     *  primary registry under dapId. An entry whose location is not found keeps an
     *  empty engineId.
     *
     *  @param rawSourcePath  Raw source path from the DAP client.
     *  @param line           Requested one-based source line number.
     *  @param dapId          DAP breakpoint ID assigned to this entry.
     */
    void addBreakpoint (const juce::String& rawSourcePath,
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
