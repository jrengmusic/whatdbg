/** @file VariableManager.cpp
 *  @brief Frame-id and variables-reference registries for the stack, scopes, and variables requests.
 */

#include <JuceHeader.h>
#include "VariableManager.h"

namespace debug
{

static constexpr int maxStackFrames { 50 };

VariableManager::VariableManager (Session& newSession)
    : session { newSession }
{
}

void VariableManager::resetVariablesState () noexcept
{
    nextVariablesRef = 1;
    variablesRefMap.clear ();
    nextFrameId = 1;
    frameIdMap.clear ();
    lastScopesThreadId = 0;
    session.resetSymbolGroupCache ();
}

int VariableManager::onStackTrace (std::string& json, std::uint32_t threadId)
{
    return session.appendStackFrames (json, maxStackFrames, [this, threadId] (int frameIndex)
    {
        const int frameId { nextFrameId++ };

        frameIdMap.insert_or_assign (frameId, std::pair<std::uint32_t, int> { threadId, frameIndex });

        return frameId;
    });
}

std::pair<std::uint32_t, int> VariableManager::selectFrameFromId (int frameId)
{
    std::uint32_t threadSystemId { 0 };
    int frameIndex { frameId };

    const auto frameEntry { frameIdMap.find (frameId) };

    if (frameEntry != frameIdMap.end ())
    {
        const auto& [frameKey, frameLocation] { *frameEntry };
        const auto& [entryThreadSystemId, entryFrameIndex] { frameLocation };
        threadSystemId = entryThreadSystemId;
        frameIndex = entryFrameIndex;

        if (threadSystemId > 0)
        {
            session.setCurrentThreadBySystemId (threadSystemId);
        }
    }

    return { threadSystemId, frameIndex };
}

int VariableManager::onScopes (int frameId)
{
    const auto [threadSystemId, frameIndex] { selectFrameFromId (frameId) };

    if (threadSystemId > 0)
    {
        lastScopesThreadId = threadSystemId;
    }

    const int localsRef { nextVariablesRef++ };
    variablesRefMap.insert_or_assign (localsRef, std::pair<int, int> { frameIndex, -1 });

    return localsRef;
}

void VariableManager::onVariables (std::string& json, int ref)
{
    if (lastScopesThreadId > 0)
    {
        session.setCurrentThreadBySystemId (lastScopesThreadId);
    }

    appendVariables (json, ref);
}

void VariableManager::appendVariables (std::string& json, int ref)
{
    const auto variablesEntry { variablesRefMap.find (ref) };

    if (variablesEntry != variablesRefMap.end ())
    {
        const auto& [variablesKey, variablesLocation] { *variablesEntry };
        const auto& [frameIndex, symbolIndex] { variablesLocation };

        const auto appendLocal = [this, &json, variableFrameIndex = frameIndex] (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int childSymbolIndex)
        {
            appendVariable (json, name, value, type, hasChildren, childSymbolIndex, variableFrameIndex);
        };

        if (symbolIndex < 0)
            session.applyToLocals (frameIndex, appendLocal);
        else
            session.applyToVariableChildren (frameIndex, symbolIndex, appendLocal);
    }
}

void VariableManager::appendVariable (std::string& json, const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex, int frameIndex)
{
    int childRef { 0 };

    if (hasChildren)
    {
        childRef = nextVariablesRef++;
        variablesRefMap.insert_or_assign (childRef, std::pair<int, int> { frameIndex, symbolIndex });
    }

    jam::JsonWriter::appendSeparator (json);
    jam::JsonWriter::appendObject (json, [&name, &value, &type, childRef] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::name);
        jam::JsonWriter::appendString (members, jam::Strings::getView (name));
        jam::JsonWriter::appendKey (members, Id::value);
        jam::JsonWriter::appendString (members, jam::Strings::getView (value));
        jam::JsonWriter::appendKey (members, Id::type);
        jam::JsonWriter::appendString (members, jam::Strings::getView (type));
        jam::JsonWriter::appendKey (members, Id::variablesReference);
        jam::Format::appendDecimal (members, childRef);
    });
}

} // namespace debug
