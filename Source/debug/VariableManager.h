#pragma once
#include <JuceHeader.h>
#include "Session.h"

namespace debug
{

/** Registry of the DAP ids for one stop: stack frame ids and variables references.
 *
 *  VariableManager is the only assigner of frame ids. Session::appendStackFrames calls
 *  addFrame for each frame and writes the returned id into the frame object. The
 *  registry is cleared at each stop, so an id from an earlier stop is not valid.
 */
class VariableManager
{
public:
    /** Creates the registry for the given session.
     *
     *  @param newSession  Session that answers the frame and variable queries. Not owned.
     */
    explicit VariableManager (Session& newSession);

    /** Clears every variables reference and frame id, and discards the session symbol cache.
     *
     *  The breakpoint, step, pause, and exception drains of StopManager call it.
     */
    void resetVariablesState () noexcept;

    /** Appends the stack frames of the current thread to json, and registers each frame id.
     *
     *  Processor::onStackTrace makes the thread current before this call. Each registered
     *  id records threadId and the frame index.
     *
     *  @param json      Output text that receives the frame objects.
     *  @param threadId  OS thread id that is recorded with each frame id.
     *  @return Number of frame objects appended.
     */
    int onStackTrace (std::string& json, std::uint32_t threadId);

    /** Creates a variables reference for the locals of a frame.
     *
     *  Records the thread of the frame as the scopes thread, when the frame id is known.
     *  onVariables makes that thread current.
     *
     *  @param frameId  Frame id from a stack trace response.
     *  @return Variables reference for the locals scope.
     */
    int onScopes (int frameId);

    /** Appends the variables that a variables reference names to json.
     *
     *  Makes the scopes thread current first. An unknown reference appends nothing.
     *
     *  @param json  Output text that receives the variable objects.
     *  @param ref   Variables reference from a scopes or variables response.
     */
    void onVariables (std::string& json, int ref);

    /** Maps a frame id to its thread and frame index.
     *
     *  A registered id gives its OS thread id and frame index, and a nonzero thread id is made
     *  current through Session::setCurrentThreadBySystemId. An unknown id gives thread 0, and
     *  the id itself is the frame index.
     *
     *  @param frameId  Frame id to resolve.
     *  @return The OS thread id, or 0 when the id is unknown, and the frame index.
     */
    std::pair<std::uint32_t, int> selectFrameFromId (int frameId);

private:
    void appendVariables (std::string& json, int ref);

    void appendVariable (std::string& json, const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex, int frameIndex);

    Session& session;

    int nextVariablesRef { 1 };

    std::unordered_map<int, std::pair<int, int>> variablesRefMap;

    int nextFrameId { 1 };

    std::unordered_map<int, std::pair<std::uint32_t, int>> frameIdMap;

    std::uint32_t lastScopesThreadId { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VariableManager)
};

} // namespace debug
