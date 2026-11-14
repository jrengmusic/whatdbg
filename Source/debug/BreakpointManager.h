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

    /** Processes a DAP setBreakpoints request for one source file, and appends the response breakpoints.
     *
     *  Entries for the file whose line is not in the new set are removed through
     *  Session::removeBreakpoint. An entry whose line is still requested keeps its DAP id.
     *  Each new line goes to addBreakpoint, which resolves it through
     *  Session::addBreakpointByLocation. A line that does not resolve keeps an empty engineId,
     *  and onReload and onModuleLoad retry it later. This method does not reload symbols.
     *  Processor::onSetBreakpoints calls onReload after it writes the response.
     *
     *  @param json                  Output text that receives the breakpoint objects, as array elements.
     *  @param rawSourcePath         Raw source path from the DAP client (may use any separator).
     *  @param document              Request document that holds requestedBreakpoints.
     *  @param requestedBreakpoints  Array of DAP breakpoint objects, each with a line member. Null when the request has none.
     */
    void onSetBreakpoints (std::string& json,
                           const juce::String& rawSourcePath,
                           const jam::JsonDocument& document,
                           const jam::Document::Element* requestedBreakpoints);

    /** Reloads the symbols of one module and resolves the breakpoints that are still unresolved.
     *
     *  Calls Session::loadModuleSymbols for imageName only when an entry has an empty
     *  engineId. Then resolves every unresolved entry through onSymbolsLoaded.
     *  StopManager calls it when a moduleLoad stop is drained.
     *
     *  @param imageName  Image file name of the module that loaded, from the moduleLoad event.
     *  @return One DAP breakpoint changed event for each entry that resolved.
     */
    jam::Strings onModuleLoad (const juce::String& imageName);

    /** Reloads all symbols and resolves the breakpoints that are still unresolved.
     *
     *  Calls Session::forceReloadAllSymbols only when an entry has an empty engineId.
     *  Then resolves every unresolved entry through onSymbolsLoaded. Called by
     *  Processor::onSetBreakpoints, by StopManager::drainStepCompleted, and by
     *  StopManager::resumeAfterInitialBreak.
     *
     *  @return One DAP breakpoint changed event for each entry that resolved.
     */
    jam::Strings onReload ();

    /** Appends a DAP stopped event for a breakpoint or step stop, to json.
     *
     *  Gives reason breakpoint with hitBreakpointIds when an entry holds engineId.
     *  Gives reason step when no entry holds engineId.
     *
     *  @param json      Output text that receives the event object.
     *  @param engineId  Engine breakpoint id of the breakpoint that stopped the target.
     *  @param threadId  OS thread id of the thread that stopped.
     */
    void appendBreakpointStoppedEvent (std::string& json, std::int32_t engineId, std::uint32_t threadId) const;

    /** Stores the resolved line of the breakpoint that holds engineId, and returns its changed event.
     *
     *  Processor calls it when the session fires breakpointLocationFound. Only the macOS
     *  session fires that event, after liblldb resolves a location.
     *
     *  @param engineId      The engine breakpoint ID.
     *  @param resolvedLine  The source line the engine resolved the breakpoint to.
     *  @return One DAP breakpoint changed event for the matched entry, or an empty array
     *          when no entry holds engineId.
     */
    jam::Strings onBreakpointLocationFound (std::int32_t  engineId,
                                            std::uint16_t resolvedLine);

private:
    /** Return true if any tracked breakpoint has not yet been assigned an engine ID. */
    bool hasUnresolvedBreakpoints () const noexcept;

    /** Resolve every entry without an engineId through setBreakpointLocation.
     *
     *  @return Array of DAP breakpoint(changed) events, one per entry that resolved.
     */
    jam::Strings onSymbolsLoaded ();

    /** Resolves sourcePath:requestedLine to a target offset, without creating an engine breakpoint.
     *
     *  Tries Session::getOffsetStatus and Session::getOffset on the requested line, then on
     *  each of the next lineSearchWindow lines, until one resolves. The search stops at an
     *  engineBusy status. The caller passes the returned offset to Session::addBreakpoint.
     *
     *  @param sourcePath     Backend-native source path for the engine's breakpoint query.
     *  @param requestedLine  One-based source line number from the client.
     *  @return The offset and the line that resolved. The offset is 0 when nothing resolved.
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
     *  empty engineId. On Windows the path is converted to backslash form first.
     *
     *  @param rawSourcePath  Raw source path from the DAP client.
     *  @param line           Requested one-based source line number.
     *  @param dapId          DAP breakpoint ID assigned to this entry.
     */
    void addBreakpoint (const juce::String& rawSourcePath,
                        std::uint16_t       line,
                        std::uint16_t       dapId);

    std::vector<std::uint16_t> getRequestedLines (const jam::JsonDocument& document,
                                                  const jam::Document::Element* requestedBreakpoints) const;

    /** Appends the DAP setBreakpoints response object for one breakpoint entry, to json.
     *
     *  The entry is verified when resolvedLine is not zero. An unverified entry also gets
     *  a message that names the source file and the requested line.
     *
     *  @param json           Output text that receives the breakpoint object.
     *  @param info           Breakpoint entry to describe.
     *  @param rawSourcePath  Raw source path from the DAP client, echoed in the response.
     */
    void appendBreakpoint (std::string& json,
                           const BreakpointInfo& info,
                           const juce::String&   rawSourcePath) const noexcept;

    /** The debug session breakpoints are created against. Not owned — must outlive this object. */
    Session& session;

    /** Primary breakpoint registry: DAP ID -> BreakpointInfo. */
    std::unordered_map<std::uint16_t, BreakpointInfo> breakpoints;

    /** Next DAP breakpoint ID to assign. Starts at 1. The type is std::uint16_t, so it wraps after 65535. */
    std::uint16_t nextDapId { 1 };

    /** Number of lines forward of the requested line to search when exact resolution fails. */
    static constexpr std::uint32_t lineSearchWindow { 4 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakpointManager)
};

} // namespace debug
