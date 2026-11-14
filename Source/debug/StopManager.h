#pragma once
#include <JuceHeader.h>
#include "State.h"
#include "Session.h"
#include "BreakpointManager.h"
#include "VariableManager.h"
#include "../dap/Types.h"

namespace debug
{

/** Turns each stop of the target into DAP messages.
 *
 *  The session fires the stop events. The handlers that StopManager registers store the
 *  stop reason in the model, and most of them also store the execution state. onStop then
 *  runs the drain for the stored reason. A drain returns the DAP messages to send.
 */
class StopManager
{
public:
    /** Registers the stop drains and the stop event handlers.
     *
     *  @param newSession             Session that fires the stop events. Not owned.
     *  @param newBreakpointManager   Breakpoint registry that builds the breakpoint messages. Not owned.
     *  @param newVariableManager     Registry that the breakpoint, step, pause, and exception drains reset. Not owned.
     */
    StopManager (Session& newSession, BreakpointManager& newBreakpointManager, VariableManager& newVariableManager);

    /** Runs the drain for the stored stop reason, and returns the DAP messages it builds.
     *
     *  Processor::process calls it on each pass of the engine context. A stop reason of none
     *  gives no messages.
     *
     *  @param targetProgram  Program path of a launch. Empty for an attach.
     *  @return DAP messages to send, in order. Empty when no drain runs.
     */
    jam::Strings onStop (const juce::String& targetProgram);

private:
    void registerEvents ();

    jam::Strings drainInitialBreak (const juce::String& targetProgram);

    jam::Strings drainBreakpointHit ();

    jam::Strings drainStepCompleted ();

    jam::Strings drainPauseCompleted ();

    jam::Strings drainModuleLoaded ();

    jam::Strings drainExceptionStopped ();

    jam::Strings resumeAfterInitialBreak (const juce::String& targetProgram);

    void resumeExecution ();

    jam::Function::Map<StopReason, jam::Function::pointerCapacity, jam::Strings (const juce::String&)> stops;

    juce::String lastLoadedImageName;

    Session& session;

    BreakpointManager& breakpointManager;

    VariableManager& variableManager;

    WHATDBGModel& model { *WHATDBGModel::getInstance() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StopManager)
};

} // namespace debug
