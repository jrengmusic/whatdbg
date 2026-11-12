/** @file BreakpointManager.cpp
 *  @brief Breakpoint registry for DAP setBreakpoints, resolution, and DAP event builders.
 *
 *  Implements all of BreakpointManager. It maps DAP breakpoint requests (file:line)
 *  to engine breakpoint IDs, keeps unresolved entries and retries them on symbol
 *  reload or module load, builds the setBreakpoints response, and builds the stopped
 *  event for a breakpoint or step stop. Path normalization helpers are file-local.
 */

#include <JuceHeader.h>
#include "BreakpointManager.h"

namespace debug
{

using dap::DynObj;

BreakpointManager::BreakpointManager (Session& newSession)
    : session { newSession }
{
}

bool BreakpointManager::hasUnresolvedBreakpoints () const noexcept
{
    return std::any_of (breakpoints.begin (), breakpoints.end (), [] (const auto& breakpointEntry)
    {
        const auto& [entryDapId, entryInfo] { breakpointEntry };
        return not entryInfo.engineId.has_value ();
    });
}

std::pair<std::uint64_t, std::uint16_t> BreakpointManager::getBreakpointOffset (
    const juce::String& sourcePath,
    std::uint16_t       requestedLine) noexcept
{
#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: getBreakpointOffset attempting " + sourcePath + ":"
                             + juce::String (static_cast<unsigned long> (requestedLine)));
#endif

    std::uint64_t offset       { 0 };
    std::uint16_t resolvedLine { 0 };
    bool          isBusy       { false };

    for (std::uint32_t delta { 0 }; delta <= lineSearchWindow and offset == 0 and not isBusy; ++delta)
    {
        const std::uint16_t candidate { static_cast<std::uint16_t> (requestedLine + delta) };
        const OffsetStatus  status    { session.getOffsetStatus (sourcePath, candidate) };

        isBusy = status == OffsetStatus::engineBusy;

#if JUCE_DEBUG
        if (isBusy)
        {
            jam::debug::Log::write ("WHATDBG: getOffsetStatus returned E_UNEXPECTED -- symbol engine not ready");
        }
#endif

        if (status == OffsetStatus::found)
        {
            offset       = session.getOffset (sourcePath, candidate);
            resolvedLine = candidate;

#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: resolved " + sourcePath + ":"
                                     + juce::String (static_cast<unsigned long> (candidate)) + " -> 0x"
                                     + juce::String::toHexString (static_cast<unsigned long long> (offset)));
#endif
        }

#if JUCE_DEBUG
        if (status == OffsetStatus::notFound and delta == 0)
        {
            jam::debug::Log::write ("WHATDBG: getOffsetStatus notFound for " + sourcePath + ":"
                                     + juce::String (static_cast<unsigned long> (candidate)));
        }
#endif
    }

    const bool isResolved { offset != 0 };

#if JUCE_DEBUG
    if (not isResolved)
    {
        jam::debug::Log::write ("WHATDBG: getBreakpointOffset failed for " + sourcePath + ":"
                                 + juce::String (static_cast<unsigned long> (requestedLine)) + " (and "
                                 + juce::String (static_cast<unsigned long> (lineSearchWindow))
                                 + " lines forward) -- pending");
    }
    else
    {
        jam::debug::Log::write ("WHATDBG: resolved offset for " + sourcePath + ":"
                                 + juce::String (static_cast<unsigned long> (resolvedLine))
                                 + " offset=0x"
                                 + juce::String::toHexString (static_cast<unsigned long long> (offset)));
    }
#endif

    return { offset, resolvedLine };
}

void BreakpointManager::setBreakpointLocation (BreakpointInfo& entryInfo) noexcept
{
    const auto [offset, candidateResolvedLine] { getBreakpointOffset (entryInfo.sourcePath, entryInfo.line) };

    if (offset != 0)
    {
        const std::optional<std::int32_t> engineId { session.addBreakpoint (offset) };

        if (engineId.has_value ())
        {
            entryInfo.engineId     = engineId;
            entryInfo.resolvedLine = candidateResolvedLine;

#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: breakpoint set dapId="
                                     + juce::String (static_cast<unsigned long> (entryInfo.dapId))
                                     + " line=" + juce::String (static_cast<unsigned long> (candidateResolvedLine))
                                     + " engineId=" + juce::String (*engineId));
#endif
        }
#if JUCE_DEBUG
        else
        {
            jam::debug::Log::write ("WHATDBG: addBreakpoint failed");
        }
#endif
    }
}

juce::var BreakpointManager::getBreakpointStoppedEvent (std::int32_t engineId, std::uint32_t threadId) const
{
    DynObj body { new juce::DynamicObject () };
    body->setProperty ("threadId",          static_cast<int> (threadId));
    body->setProperty ("allThreadsStopped", true);

    const auto breakpointEntry { std::find_if (breakpoints.begin (), breakpoints.end (),
        [engineId] (const auto& entry)
        {
            const auto& [entryDapId, entryInfo] { entry };
            return entryInfo.engineId == engineId;
        }) };

    if (breakpointEntry != breakpoints.end ())
    {
        const auto& [matchedDapId, matchedInfo] { *breakpointEntry };

        juce::Array<juce::var> hitIds;
        hitIds.add (static_cast<int> (matchedDapId));

        body->setProperty ("reason",          "breakpoint");
        body->setProperty ("hitBreakpointIds", juce::var (hitIds));
    }
    else
    {
        body->setProperty ("reason", "step");
    }

    return dap::getEvent ("stopped", juce::var (body));
}

juce::Array<juce::var> BreakpointManager::onBreakpointLocationFound (std::int32_t  engineId,
                                                                     std::uint16_t resolvedLine)
{
    juce::Array<juce::var> events;

    const auto breakpointEntry { std::find_if (breakpoints.begin (), breakpoints.end (),
        [engineId] (auto& entry)
        {
            const auto& [entryDapId, entryInfo] { entry };
            return entryInfo.engineId == engineId;
        }) };

    if (breakpointEntry != breakpoints.end ())
    {
        auto& [matchedDapId, matchedInfo] { *breakpointEntry };
        matchedInfo.resolvedLine = resolvedLine;
        events.add (dap::getBreakpointChangedEvent (static_cast<int> (matchedDapId), static_cast<int> (resolvedLine)));
    }

    return events;
}

static juce::String normalizePath (const juce::String& path) noexcept
{
    return path.replace ("\\", "/");
}

#if JUCE_WINDOWS
static juce::String toWindowsPath (const juce::String& path) noexcept
{
    return path.replace ("/", "\\");
}
#endif

std::unordered_map<std::uint16_t, std::uint16_t> BreakpointManager::removeOrphanedBreakpoints (
    const juce::String&               normalizedKey,
    const std::vector<std::uint16_t>& requestedLines)
{
    std::unordered_map<std::uint16_t, std::uint16_t> existingLines;
    std::vector<std::uint16_t> orphanedDapIds;

    for (const auto& [entryDapId, entryInfo] : breakpoints)
    {
        if (normalizePath (entryInfo.sourcePath).compare (normalizedKey) == 0)
        {
            if (std::find (requestedLines.begin (), requestedLines.end (), entryInfo.line) != requestedLines.end ())
                existingLines.insert_or_assign (entryInfo.line, entryDapId);
            else
                orphanedDapIds.push_back (entryDapId);
        }
    }

    for (const auto orphanedDapId : orphanedDapIds)
    {
        const auto& info { breakpoints.at (orphanedDapId) };

        if (info.engineId.has_value ())
            juce::ignoreUnused (session.removeBreakpoint (*info.engineId));

        breakpoints.erase (orphanedDapId);
    }

    return existingLines;
}

void BreakpointManager::addBreakpoint (const juce::String& rawSourcePath,
                                       std::uint16_t       line,
                                       std::uint16_t       dapId)
{
   #if JUCE_WINDOWS
    const juce::String nativePath { toWindowsPath (rawSourcePath) };
   #else
    const juce::String nativePath { rawSourcePath };
   #endif

    const auto location { session.addBreakpointByLocation (nativePath, line) };

    BreakpointInfo info {};
    info.dapId      = dapId;
    info.sourcePath = nativePath;
    info.line       = line;

    if (location.has_value ())
    {
        const auto [engineId, resolvedLine] { *location };
        info.engineId     = engineId;
        info.resolvedLine = resolvedLine;
    }

#if JUCE_DEBUG
    if (not info.engineId.has_value ())
    {
        jam::debug::Log::write ("WHATDBG: breakpoint pending (module not loaded) " + nativePath + ":"
                                 + juce::String (line) + " dapId="
                                 + juce::String (static_cast<unsigned long> (dapId)));
    }
#endif

    breakpoints.insert_or_assign (dapId, std::move (info));
}

juce::var BreakpointManager::getBreakpointResponse (const BreakpointInfo& info,
                                                     const juce::String&   rawSourcePath) const noexcept
{
    const bool isResolved { info.resolvedLine != 0 };

    DynObj bpObj { new juce::DynamicObject () };
    bpObj->setProperty ("id",       static_cast<int> (info.dapId));
    bpObj->setProperty ("verified", isResolved);
    bpObj->setProperty ("line",     isResolved ? static_cast<int> (info.resolvedLine)
                                                : static_cast<int> (info.line));

    if (not isResolved)
    {
        bpObj->setProperty ("message",
            "WHATDBG: pending - module not loaded for "
            + rawSourcePath + ":" + juce::String (info.line));
    }

    DynObj sourceObj { new juce::DynamicObject () };
    sourceObj->setProperty ("path", rawSourcePath);
    bpObj->setProperty ("source", juce::var (sourceObj));

    return juce::var (bpObj);
}

juce::Array<juce::var> BreakpointManager::onSetBreakpoints (
    const juce::String& rawSourcePath,
    const juce::var&    requestedBreakpoints)
{
    const juce::String normalizedKey { normalizePath (rawSourcePath) };
    auto*              bpsArr        { requestedBreakpoints.getArray () };
    const int          bpsCount      { bpsArr != nullptr ? bpsArr->size () : 0 };

    std::vector<std::uint16_t> requestedLines;

    for (int i { 0 }; i < bpsCount; ++i)
    {
        auto* obj { bpsArr->getReference (i).getDynamicObject () };
        jassert (obj != nullptr);

        requestedLines.push_back (static_cast<std::uint16_t> (static_cast<int> (obj->getProperty ("line"))));
    }

    const auto existingLines { removeOrphanedBreakpoints (normalizedKey, requestedLines) };

    juce::Array<juce::var> responses;

    for (const auto line : requestedLines)
    {
        std::uint16_t dapId { nextDapId };

        const auto existingEntry { existingLines.find (line) };

        if (existingEntry != existingLines.end ())
        {
            const auto& [existingLine, existingDapId] { *existingEntry };
            dapId = existingDapId;
        }
        else
        {
            addBreakpoint (rawSourcePath, line, nextDapId);
            ++nextDapId;
        }

        responses.add (getBreakpointResponse (breakpoints.at (dapId), rawSourcePath));
    }

    return responses;
}

juce::Array<juce::var> BreakpointManager::onModuleLoad (const juce::String& imageName)
{
    juce::Array<juce::var> events;

    if (hasUnresolvedBreakpoints ())
    {
        juce::ignoreUnused (session.loadModuleSymbols (imageName));
        events = onSymbolsLoaded ();
    }

    return events;
}

juce::Array<juce::var> BreakpointManager::onReload ()
{
    juce::Array<juce::var> events;

    if (hasUnresolvedBreakpoints ())
    {
        juce::ignoreUnused (session.forceReloadAllSymbols ());
        events = onSymbolsLoaded ();
    }

    return events;
}

juce::Array<juce::var> BreakpointManager::onSymbolsLoaded ()
{
    juce::Array<juce::var> events;

    for (auto& [entryDapId, entryInfo] : breakpoints)
    {
        if (not entryInfo.engineId.has_value ())
        {
            setBreakpointLocation (entryInfo);

            if (entryInfo.engineId.has_value ())
            {
                events.add (dap::getBreakpointChangedEvent (static_cast<int> (entryDapId), static_cast<int> (entryInfo.resolvedLine)));

#if JUCE_DEBUG
                jam::debug::Log::write ("WHATDBG: deferred breakpoint resolved on module load"
                                         " dapId=" + juce::String (static_cast<unsigned long> (entryDapId))
                                         + " requested=" + juce::String (static_cast<unsigned long> (entryInfo.line))
                                         + " resolved=" + juce::String (static_cast<unsigned long> (entryInfo.resolvedLine)));
#endif
            }
        }
    }

    return events;
}

} // namespace debug
