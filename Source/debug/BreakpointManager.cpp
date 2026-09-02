/** @file BreakpointManager.cpp
 *  @brief Breakpoint lifecycle management — add, remove, resolve, and track.
 *
 *  Manages the mapping between DAP breakpoint requests (file:line) and platform
 *  debug engine breakpoint IDs. Handles deferred resolution for breakpoints set
 *  before modules are loaded.
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

bool BreakpointManager::isUserBreakpoint (std::int32_t engineId) const noexcept
{
    return std::any_of (breakpoints.begin (), breakpoints.end (), [engineId] (const auto& breakpointEntry)
    {
        const auto& [entryDapId, entryInfo] { breakpointEntry };
        return entryInfo.engineId == engineId;
    });
}

bool BreakpointManager::hasUnresolvedBreakpoints () const noexcept
{
    return std::any_of (breakpoints.begin (), breakpoints.end (), [] (const auto& breakpointEntry)
    {
        const auto& [entryDapId, entryInfo] { breakpointEntry };
        return entryInfo.engineId == 0;
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

        if (not isBusy and status == OffsetStatus::found)
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
        if (not isBusy and status != OffsetStatus::found and delta == 0)
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
        const std::int32_t engineId { session.addBreakpoint (offset) };

        if (engineId != 0)
        {
            entryInfo.engineId     = engineId;
            entryInfo.resolvedLine = candidateResolvedLine;

#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: breakpoint set dapId="
                                     + juce::String (static_cast<unsigned long> (entryInfo.dapId))
                                     + " line=" + juce::String (static_cast<unsigned long> (candidateResolvedLine))
                                     + " engineId=" + juce::String (engineId));
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

juce::var BreakpointManager::onBreakpointHit (std::int32_t engineId, std::uint32_t threadId)
{
    DynObj body { new juce::DynamicObject () };
    body->setProperty ("reason",            "breakpoint");
    body->setProperty ("threadId",          static_cast<int> (threadId));
    body->setProperty ("allThreadsStopped", true);

    juce::Array<juce::var> hitIds;

    const auto breakpointEntry { std::find_if (breakpoints.begin (), breakpoints.end (),
        [engineId] (const auto& entry)
        {
            const auto& [entryDapId, entryInfo] { entry };
            return entryInfo.engineId == engineId;
        }) };

    if (breakpointEntry != breakpoints.end ())
    {
        const auto& [matchedDapId, matchedInfo] { *breakpointEntry };
        hitIds.add (static_cast<int> (matchedDapId));
    }

    body->setProperty ("hitBreakpointIds", juce::var (hitIds));

    return juce::var (body);
}

int BreakpointManager::onBreakpointLocationFound (std::int32_t  engineId,
                                                  std::uint16_t resolvedLine) noexcept
{
    int dapId { 0 };

    const auto breakpointEntry { std::find_if (breakpoints.begin (), breakpoints.end (),
        [engineId] (auto& entry)
        {
            const auto& [entryDapId, entryInfo] { entry };
            return entryInfo.engineId == engineId;
        }) };

    if (breakpointEntry != breakpoints.end ())
    {
        auto& [matchedDapId, matchedInfo] { *breakpointEntry };
        dapId = static_cast<int> (matchedDapId);
        matchedInfo.resolvedLine = resolvedLine;
    }

    return dapId;
}

} // namespace debug
