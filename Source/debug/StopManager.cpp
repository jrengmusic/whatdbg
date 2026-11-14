/** @file StopManager.cpp
 *  @brief Stop-reason table and the drains that turn each stop into DAP messages.
 *
 *  The session fires the stop events. Each handler stores the stop reason, and most
 *  handlers also store the execution state. onStop then runs the drain for the stored reason.
 */

#include <JuceHeader.h>
#include "StopManager.h"

namespace debug
{

StopManager::StopManager (Session& newSession, BreakpointManager& newBreakpointManager, VariableManager& newVariableManager)
    : session { newSession },
      breakpointManager { newBreakpointManager },
      variableManager { newVariableManager }
{
    stops.add (StopReason::initialBreak, [this] (const StopReason&, const juce::String& targetProgram) { return drainInitialBreak (targetProgram); });
    stops.add (StopReason::moduleLoad,   [this] (const StopReason&, const juce::String&) { return drainModuleLoaded (); });
    stops.add (StopReason::breakpoint,   [this] (const StopReason&, const juce::String&) { return drainBreakpointHit (); });
    stops.add (StopReason::step,         [this] (const StopReason&, const juce::String&) { return drainStepCompleted (); });
    stops.add (StopReason::pause,        [this] (const StopReason&, const juce::String&) { return drainPauseCompleted (); });
    stops.add (StopReason::exception,    [this] (const StopReason&, const juce::String&) { return drainExceptionStopped (); });

    registerEvents ();
}

void StopManager::registerEvents ()
{
    session.events.add (Id::initialBreak, [this] (const juce::Identifier&)
    {
        model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::initialBreak));
        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (ExecutionState::stopped));
    });

    session.events.add (Id::pause, [this] (const juce::Identifier&)
    {
        model.storeValue (model.sessionTag, Id::isPausePending, 0);
        model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::pause));
        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (ExecutionState::stopped));
    });

    session.events.add<juce::int64, juce::int64, int> (Id::exception, [this] (const juce::Identifier&, juce::int64 code, juce::int64 address, int isMach)
    {
        model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::exception));
        model.storeValue (model.exceptionTag, Id::exceptionCode, static_cast<int64_t> (static_cast<std::uint32_t> (code)));
        model.storeValue (model.exceptionTag, Id::exceptionAddress, static_cast<int64_t> (address));
        model.storeValue (model.exceptionTag, Id::isMachException, static_cast<int> (isMach != 0));
        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (ExecutionState::stopped));
    });

    session.events.add<int> (Id::breakpoint, [this] (const juce::Identifier&, int engineId)
    {
        model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::breakpoint));
        model.storeValue (model.sessionTag, Id::breakpointEngineId, engineId);
        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (ExecutionState::stopped));
    });

    session.events.add (Id::step, [this] (const juce::Identifier&)
    {
        model.storeValue (model.sessionTag, Id::isStepPending, 0);
        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (ExecutionState::stopped));

        if (static_cast<StopReason> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::stopReason)->getValue ()) == StopReason::none)
            model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::step));
    });

    session.events.add<const juce::String&> (Id::moduleLoad, [this] (const juce::Identifier&, const juce::String& imageName)
    {
        model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::moduleLoad));
        lastLoadedImageName = imageName;
    });
}

jam::Strings StopManager::onStop (const juce::String& targetProgram)
{
    const auto stop { stops.find (static_cast<StopReason> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::stopReason)->getValue ())) };

    if (stop != stops.end ())
    {
        const auto& [stopReason, drain] { *stop };
        return drain (stopReason, targetProgram);
    }

    return jam::Strings {};
}

jam::Strings StopManager::drainInitialBreak (const juce::String& targetProgram)
{
    if (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isConfigurationDone)->getValue () != 0)
    {
        model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::none));
        return resumeAfterInitialBreak (targetProgram);
    }

    return jam::Strings {};
}

jam::Strings StopManager::drainBreakpointHit ()
{
    jam::Strings messages;

    model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::none));

    std::string json;

    breakpointManager.appendBreakpointStoppedEvent (json, model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::breakpointEngineId)->getValue (), session.getEventThreadSystemId ());
    messages.add (json);
    variableManager.resetVariablesState ();

    return messages;
}

jam::Strings StopManager::drainStepCompleted ()
{
    jam::Strings messages;

    model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::none));

    messages.add (dap::getStoppedEvent (jam::Strings::getView (Id::step.toString ()), static_cast<int> (session.getEventThreadSystemId ())));
    variableManager.resetVariablesState ();
    messages.addArray (breakpointManager.onReload (), 0, -1);
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] step completed, emitted stopped event");
#endif

    return messages;
}

jam::Strings StopManager::drainPauseCompleted ()
{
    jam::Strings messages;

    model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::none));

    messages.add (dap::getStoppedEvent (jam::Strings::getView (Id::pause.toString ()), static_cast<int> (session.getEventThreadSystemId ())));
    variableManager.resetVariablesState ();
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] pause completed, emitted stopped event");
#endif

    return messages;
}

jam::Strings StopManager::drainModuleLoaded ()
{
    jam::Strings messages;

    model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::none));

    messages.addArray (breakpointManager.onModuleLoad (lastLoadedImageName), 0, -1);
    session.resume ();

    return messages;
}

jam::Strings StopManager::drainExceptionStopped ()
{
    jam::Strings messages;

    model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (StopReason::none));

    const juce::String exceptionName { getExceptionName (static_cast<std::uint32_t> (model.getParameter<jam::Parameter<int64_t>> (model.exceptionTag, Id::exceptionCode)->getValue ()), model.getParameter<jam::Parameter<int>> (model.exceptionTag, Id::isMachException)->getValue () != 0) };
    const juce::String description   { dap::getExceptionDescription (model.getParameter<jam::Parameter<int64_t>> (model.exceptionTag, Id::exceptionCode)->getValue (), model.getParameter<jam::Parameter<int64_t>> (model.exceptionTag, Id::exceptionAddress)->getValue ()) };
    const int threadId { static_cast<int> (session.getEventThreadSystemId ()) };

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] exception stopped: " + exceptionName + " " + description
                             + " (threadId=" + juce::String (threadId) + ")");
#endif

    messages.add (dap::getExceptionStoppedEvent (exceptionName, description, threadId));
    messages.add (dap::getExceptionOutputEvent (exceptionName, description));
    variableManager.resetVariablesState ();

    return messages;
}

jam::Strings StopManager::resumeAfterInitialBreak (const juce::String& targetProgram)
{
    jam::Strings messages;

    messages.addArray (breakpointManager.onReload (), 0, -1);

    const bool isLaunchSession { targetProgram.isNotEmpty () };

    if (isLaunchSession)
        messages.add (dap::getProcessEvent (targetProgram, static_cast<std::uint32_t> (model.getParameter<jam::Parameter<int64_t>> (model.sessionTag, Id::targetProcessId)->getValue ()), jam::Strings::getView (Id::launch.toString ())));

    resumeExecution ();

    std::string json;

    dap::appendEvent (json, jam::Strings::getView (Id::thread.toString ()), [this] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::reason);
        jam::JsonWriter::appendString (body, jam::Strings::getView (Id::started.toString ()));
        jam::JsonWriter::appendKey (body, Id::threadId);
        jam::Format::appendDecimal (body, static_cast<int> (session.getEventThreadSystemId ()));
    });

    messages.add (json);

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] resumed after initial break");
#endif

    return messages;
}

void StopManager::resumeExecution ()
{
    session.resume ();
    model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (ExecutionState::running));
}

} // namespace debug
