/** @file WHATDBGModel.cpp
 *  @brief Creates the session row and the exception row, and their parameters.
 */

#include "debug/State.h"

WHATDBGModel::WHATDBGModel ()
    : jam::Model (jam::Format::toType (Id::whatdbg))
{
    auto sessionRow { getOrCreateChildWithName (sessionTag) };
    auto exceptionRow { getOrCreateChildWithName (exceptionTag) };

    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::executionState, static_cast<int> (debug::ExecutionState::idle));
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::stopReason, static_cast<int> (debug::StopReason::none));
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::processExitCode, 0);
    createAndAddParameter<jam::Parameter<int64_t>> (sessionRow, Id::targetProcessId, int64_t { 0 });
    createAndAddParameter<jam::Parameter<int64_t>> (sessionRow, Id::terminateDeadlineMs, int64_t { 0 });
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::isRunning, 1);
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::shouldTerminateOnExit, 0);
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::isConfigurationDone, 0);
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::isStepPending, 0);
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::isPausePending, 0);
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::breakpointEngineId, 0);
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::nextSequenceNumber, 1);
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::numMessages, 0);
    createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::adapterExitCode, 0);
    createAndAddParameter<jam::Parameter<int64_t>> (exceptionRow, Id::exceptionCode, int64_t { 0 });
    createAndAddParameter<jam::Parameter<int64_t>> (exceptionRow, Id::exceptionAddress, int64_t { 0 });
    createAndAddParameter<jam::Parameter<int>> (exceptionRow, Id::isMachException, 0);

    stopTimer ();
}
