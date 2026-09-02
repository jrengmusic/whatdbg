/** @file BreakpointManagerHandlers.cpp
 *  @brief DAP setBreakpoints request handling and module-load retry.
 *
 *  Implements BreakpointManager::onSetBreakpoints (the DAP setBreakpoints
 *  request handler) and BreakpointManager::onModuleLoad (retries breakpoints
 *  left pending because their module was not yet loaded), along with the
 *  private helpers both depend on.
 */

#include <JuceHeader.h>
#include "BreakpointManager.h"
#include "State.h"

namespace debug
{

using dap::DynObj;

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
    const std::string& normalizedKey,
    const Lines&       requestedLines)
{
    std::unordered_map<std::uint16_t, std::uint16_t> existingLines;
    std::vector<std::uint16_t> orphanedDapIds;

    for (const auto& [entryDapId, entryInfo] : breakpoints)
    {
        if (normalizePath (entryInfo.sourcePath).toStdString () == normalizedKey)
        {
            if (requestedLines.count (entryInfo.line) != 0)
                existingLines.insert_or_assign (entryInfo.line, entryDapId);
            else
                orphanedDapIds.push_back (entryDapId);
        }
    }

    for (const auto orphanedDapId : orphanedDapIds)
    {
        const auto& info { breakpoints.at (orphanedDapId) };

        if (info.engineId != 0)
            juce::ignoreUnused (session.removeBreakpoint (info.engineId));

        breakpoints.erase (orphanedDapId);
    }

    return existingLines;
}

BreakpointInfo BreakpointManager::addBreakpoint (const juce::String& rawSourcePath,
                                                 std::uint16_t       line,
                                                 std::uint16_t       dapId)
{
   #if JUCE_WINDOWS
    const juce::String nativePath { toWindowsPath (rawSourcePath) };
   #else
    const juce::String nativePath { rawSourcePath };
   #endif

    const BreakpointLocation location { session.addBreakpointByLocation (nativePath, line) };
    const auto [engineId, resolvedLine] { location };

    BreakpointInfo info {};
    info.dapId        = dapId;
    info.sourcePath   = nativePath;
    info.line         = line;
    info.resolvedLine = resolvedLine;
    info.engineId     = engineId;

#if JUCE_DEBUG
    if (engineId == 0)
    {
        jam::debug::Log::write ("WHATDBG: breakpoint pending (module not loaded) " + nativePath + ":"
                                 + juce::String (line) + " dapId="
                                 + juce::String (static_cast<unsigned long> (dapId)));
    }
#endif

    breakpoints.insert_or_assign (dapId, info);

    return info;
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
            "WHATDBG: pending — module not loaded for "
            + rawSourcePath + ":" + juce::String (info.line));
    }

    DynObj sourceObj { new juce::DynamicObject () };
    sourceObj->setProperty ("path", rawSourcePath);
    bpObj->setProperty ("source", juce::var (sourceObj));

    return juce::var (bpObj);
}

std::unordered_map<std::uint16_t, BreakpointInfo> BreakpointManager::addReloadedBreakpoints ()
{
    juce::ignoreUnused (session.forceReloadAllSymbols ());

    std::unordered_map<std::uint16_t, BreakpointInfo> resolvedEntries;

    for (auto& [entryDapId, entryInfo] : breakpoints)
    {
        if (entryInfo.engineId == 0)
        {
            setBreakpointLocation (entryInfo);

            if (entryInfo.engineId != 0)
                resolvedEntries.insert_or_assign (entryDapId, entryInfo);
        }
    }

    return resolvedEntries;
}

juce::Array<juce::var> BreakpointManager::onSetBreakpoints (
    const juce::String& rawSourcePath,
    const juce::var&    requestedBreakpoints)
{
    const std::string normalizedKey { normalizePath (rawSourcePath).toStdString () };
    auto*              bpsArr       { requestedBreakpoints.getArray () };
    const int          bpsCount     { bpsArr != nullptr ? bpsArr->size () : 0 };

    Lines requestedLines;

    for (int i { 0 }; i < bpsCount; ++i)
    {
        auto* obj { bpsArr->getReference (i).getDynamicObject () };
        jassert (obj != nullptr);

        requestedLines.insert (static_cast<std::uint16_t> (static_cast<int> (obj->getProperty ("line"))));
    }

    const auto existingLines { removeOrphanedBreakpoints (normalizedKey, requestedLines) };

    juce::Array<juce::var> responseArray;

    for (int i { 0 }; i < bpsCount; ++i)
    {
        auto* obj { bpsArr->getReference (i).getDynamicObject () };
        jassert (obj != nullptr);

        const auto lineNumber { static_cast<std::uint16_t> (static_cast<int> (obj->getProperty ("line"))) };

        const auto  existingEntry { existingLines.find (lineNumber) };
        const bool  isReuse       { existingEntry != existingLines.end () };
        std::uint16_t dapId       { isReuse ? existingEntry->second : nextDapId };

        if (not isReuse) ++nextDapId;

        const BreakpointInfo info { isReuse ? breakpoints.at (dapId)
                                             : addBreakpoint (rawSourcePath, lineNumber, dapId) };
        responseArray.add (getBreakpointResponse (info, rawSourcePath));
    }

    const bool hasUnresolved { hasUnresolvedBreakpoints () };

    if (hasUnresolved)
    {
        const auto resolvedEntries { addReloadedBreakpoints () };

        for (int r { 0 }; r < responseArray.size (); ++r)
        {
            const auto* respObj { responseArray[r].getDynamicObject () };
            jassert (respObj != nullptr);

            const auto  resolvedEntry { resolvedEntries.find (
                static_cast<std::uint16_t> (static_cast<int> (respObj->getProperty ("id")))) };

            if (resolvedEntry != resolvedEntries.end ())
            {
                const auto& [resolvedDapId, resolvedInfo] { *resolvedEntry };
                responseArray.set (r, getBreakpointResponse (resolvedInfo, rawSourcePath));
            }
        }
    }

    State::getInstance ()->hasPendingBreakpoints = hasUnresolved;

    return responseArray;
}

juce::Array<juce::var> BreakpointManager::onModuleLoad ()
{
    juce::Array<juce::var> events;

    for (auto& [entryDapId, entryInfo] : breakpoints)
    {
        if (entryInfo.engineId == 0)
        {
            setBreakpointLocation (entryInfo);

            if (entryInfo.engineId != 0)
            {
                DynObj bpObj { new juce::DynamicObject () };
                bpObj->setProperty ("id",       static_cast<int> (entryDapId));
                bpObj->setProperty ("verified", true);
                bpObj->setProperty ("line",     static_cast<int> (entryInfo.resolvedLine));

                DynObj body { new juce::DynamicObject () };
                body->setProperty ("reason",     "changed");
                body->setProperty ("breakpoint", juce::var (bpObj));

                events.add (dap::getEvent ("breakpoint", juce::var (body)));

#if JUCE_DEBUG
                jam::debug::Log::write ("WHATDBG: deferred breakpoint resolved on module load"
                                         " dapId=" + juce::String (static_cast<unsigned long> (entryDapId))
                                         + " requested=" + juce::String (static_cast<unsigned long> (entryInfo.line))
                                         + " resolved=" + juce::String (static_cast<unsigned long> (entryInfo.resolvedLine)));
#endif
            }
        }
    }

    State::getInstance ()->hasPendingBreakpoints = hasUnresolvedBreakpoints ();

    return events;
}

} // namespace debug
