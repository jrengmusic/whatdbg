/*******************************************************************************
                        Codegen Annotated Source of Truth
————————————————————————————————————————————————————————————————————————————————

            ░░████████████░░████████████░░████████████░░████████████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████        ░░████  ░░████░░████            ░░████
            ░░████        ░░████████████░░████████████    ░░████
            ░░████        ░░████  ░░████        ░░████    ░░████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████████████░░████  ░░████░░████████████    ░░████

————————————————————————————————————————————————————————————————————————————————
                         FOR YOUR EYES ONLY, DO NOT EDIT
********************************************************************************/

#include <JuceHeader.h>
#include "Generated.h"

namespace Id
{
/*_____________________________________________________________________________*/

/** @brief whatdbg vocabulary — the Model names, the engine events, and the words of the Debug Adapter Protocol. */

    const juce::Identifier adapterExitCode                    { juce::String::fromUTF8 ("adapterExitCode") };
    const juce::Identifier allThreadsContinued                { juce::String::fromUTF8 ("allThreadsContinued") };
    const juce::Identifier allThreadsStopped                  { juce::String::fromUTF8 ("allThreadsStopped") };
    const juce::Identifier arguments                          { juce::String::fromUTF8 ("arguments") };
    const juce::Identifier attach                             { juce::String::fromUTF8 ("attach") };
    const juce::Identifier breakMode                          { juce::String::fromUTF8 ("breakMode") };
    const juce::Identifier breakpoint                         { juce::String::fromUTF8 ("breakpoint") };
    const juce::Identifier breakpointEngineId                 { juce::String::fromUTF8 ("breakpointEngineId") };
    const juce::Identifier breakpointLocationFound            { juce::String::fromUTF8 ("breakpointLocationFound") };
    const juce::Identifier breakpoints                        { juce::String::fromUTF8 ("breakpoints") };
    const juce::Identifier category                           { juce::String::fromUTF8 ("category") };
    const juce::Identifier changed                            { juce::String::fromUTF8 ("changed") };
    const juce::Identifier column                             { juce::String::fromUTF8 ("column") };
    const juce::Identifier configurationDone                  { juce::String::fromUTF8 ("configurationDone") };
    const juce::Identifier console                            { juce::String::fromUTF8 ("console") };
    const juce::Identifier context                            { juce::String::fromUTF8 ("context") };
    const juce::Identifier continueCommand                    { juce::String::fromUTF8 ("continue") };
    const juce::Identifier disconnect                         { juce::String::fromUTF8 ("disconnect") };
    const juce::Identifier evaluate                           { juce::String::fromUTF8 ("evaluate") };
    const juce::Identifier exception                          { juce::String::fromUTF8 ("exception") };
    const juce::Identifier exceptionAddress                   { juce::String::fromUTF8 ("exceptionAddress") };
    const juce::Identifier exceptionCode                      { juce::String::fromUTF8 ("exceptionCode") };
    const juce::Identifier exceptionId                        { juce::String::fromUTF8 ("exceptionId") };
    const juce::Identifier exceptionInfo                      { juce::String::fromUTF8 ("exceptionInfo") };
    const juce::Identifier executionState                     { juce::String::fromUTF8 ("executionState") };
    const juce::Identifier exitCode                           { juce::String::fromUTF8 ("exitCode") };
    const juce::Identifier exited                             { juce::String::fromUTF8 ("exited") };
    const juce::Identifier expensive                          { juce::String::fromUTF8 ("expensive") };
    const juce::Identifier expression                         { juce::String::fromUTF8 ("expression") };
    const juce::Identifier frameId                            { juce::String::fromUTF8 ("frameId") };
    const juce::Identifier hitBreakpointIds                   { juce::String::fromUTF8 ("hitBreakpointIds") };
    const juce::Identifier hover                              { juce::String::fromUTF8 ("hover") };
    const juce::Identifier initialBreak                       { juce::String::fromUTF8 ("initialBreak") };
    const juce::Identifier initialize                         { juce::String::fromUTF8 ("initialize") };
    const juce::Identifier initialized                        { juce::String::fromUTF8 ("initialized") };
    const juce::Identifier isConfigurationDone                { juce::String::fromUTF8 ("isConfigurationDone") };
    const juce::Identifier isLocalProcess                     { juce::String::fromUTF8 ("isLocalProcess") };
    const juce::Identifier isMachException                    { juce::String::fromUTF8 ("isMachException") };
    const juce::Identifier isPausePending                     { juce::String::fromUTF8 ("isPausePending") };
    const juce::Identifier isRunning                          { juce::String::fromUTF8 ("isRunning") };
    const juce::Identifier isStepPending                      { juce::String::fromUTF8 ("isStepPending") };
    const juce::Identifier launch                             { juce::String::fromUTF8 ("launch") };
    const juce::Identifier moduleLoad                         { juce::String::fromUTF8 ("moduleLoad") };
    const juce::Identifier next                               { juce::String::fromUTF8 ("next") };
    const juce::Identifier nextSequenceNumber                 { juce::String::fromUTF8 ("nextSequenceNumber") };
    const juce::Identifier numMessages                        { juce::String::fromUTF8 ("numMessages") };
    const juce::Identifier output                             { juce::String::fromUTF8 ("output") };
    const juce::Identifier pause                              { juce::String::fromUTF8 ("pause") };
    const juce::Identifier pid                                { juce::String::fromUTF8 ("pid") };
    const juce::Identifier prepare                            { juce::String::fromUTF8 ("prepare") };
    const juce::Identifier process                            { juce::String::fromUTF8 ("process") };
    const juce::Identifier processExitCode                    { juce::String::fromUTF8 ("processExitCode") };
    const juce::Identifier program                            { juce::String::fromUTF8 ("program") };
    const juce::Identifier reason                             { juce::String::fromUTF8 ("reason") };
    const juce::Identifier releaseResources                   { juce::String::fromUTF8 ("releaseResources") };
    const juce::Identifier request                            { juce::String::fromUTF8 ("request") };
    const juce::Identifier requestSeq                         { juce::String::fromUTF8 ("request_seq") };
    const juce::Identifier response                           { juce::String::fromUTF8 ("response") };
    const juce::Identifier result                             { juce::String::fromUTF8 ("result") };
    const juce::Identifier running                            { juce::String::fromUTF8 ("running") };
    const juce::Identifier scopes                             { juce::String::fromUTF8 ("scopes") };
    const juce::Identifier seq                                { juce::String::fromUTF8 ("seq") };
    const juce::Identifier session                            { juce::String::fromUTF8 ("session") };
    const juce::Identifier setBreakpoints                     { juce::String::fromUTF8 ("setBreakpoints") };
    const juce::Identifier shouldTerminateOnExit              { juce::String::fromUTF8 ("shouldTerminateOnExit") };
    const juce::Identifier source                             { juce::String::fromUTF8 ("source") };
    const juce::Identifier stackFrames                        { juce::String::fromUTF8 ("stackFrames") };
    const juce::Identifier stackTrace                         { juce::String::fromUTF8 ("stackTrace") };
    const juce::Identifier standardError                      { juce::String::fromUTF8 ("stderr") };
    const juce::Identifier standardOutput                     { juce::String::fromUTF8 ("stdout") };
    const juce::Identifier started                            { juce::String::fromUTF8 ("started") };
    const juce::Identifier startMethod                        { juce::String::fromUTF8 ("startMethod") };
    const juce::Identifier step                               { juce::String::fromUTF8 ("step") };
    const juce::Identifier stepIn                             { juce::String::fromUTF8 ("stepIn") };
    const juce::Identifier stepOut                            { juce::String::fromUTF8 ("stepOut") };
    const juce::Identifier stopped                            { juce::String::fromUTF8 ("stopped") };
    const juce::Identifier stopReason                         { juce::String::fromUTF8 ("stopReason") };
    const juce::Identifier success                            { juce::String::fromUTF8 ("success") };
    const juce::Identifier supportsBreakpointLocationsRequest { juce::String::fromUTF8 ("supportsBreakpointLocationsRequest") };
    const juce::Identifier supportsCancelRequest              { juce::String::fromUTF8 ("supportsCancelRequest") };
    const juce::Identifier supportsClipboardContext           { juce::String::fromUTF8 ("supportsClipboardContext") };
    const juce::Identifier supportsCompletionsRequest         { juce::String::fromUTF8 ("supportsCompletionsRequest") };
    const juce::Identifier supportsConditionalBreakpoints     { juce::String::fromUTF8 ("supportsConditionalBreakpoints") };
    const juce::Identifier supportsConfigurationDoneRequest   { juce::String::fromUTF8 ("supportsConfigurationDoneRequest") };
    const juce::Identifier supportsDataBreakpoints            { juce::String::fromUTF8 ("supportsDataBreakpoints") };
    const juce::Identifier supportsDelayedStackTraceLoading   { juce::String::fromUTF8 ("supportsDelayedStackTraceLoading") };
    const juce::Identifier supportsDisassembleRequest         { juce::String::fromUTF8 ("supportsDisassembleRequest") };
    const juce::Identifier supportsEvaluateForHovers          { juce::String::fromUTF8 ("supportsEvaluateForHovers") };
    const juce::Identifier supportsExceptionFilterOptions     { juce::String::fromUTF8 ("supportsExceptionFilterOptions") };
    const juce::Identifier supportsExceptionInfoRequest       { juce::String::fromUTF8 ("supportsExceptionInfoRequest") };
    const juce::Identifier supportsExceptionOptions           { juce::String::fromUTF8 ("supportsExceptionOptions") };
    const juce::Identifier supportsFunctionBreakpoints        { juce::String::fromUTF8 ("supportsFunctionBreakpoints") };
    const juce::Identifier supportsGotoTargetsRequest         { juce::String::fromUTF8 ("supportsGotoTargetsRequest") };
    const juce::Identifier supportsHitConditionalBreakpoints  { juce::String::fromUTF8 ("supportsHitConditionalBreakpoints") };
    const juce::Identifier supportsInstructionBreakpoints     { juce::String::fromUTF8 ("supportsInstructionBreakpoints") };
    const juce::Identifier supportsLoadedSourcesRequest       { juce::String::fromUTF8 ("supportsLoadedSourcesRequest") };
    const juce::Identifier supportsLogPoints                  { juce::String::fromUTF8 ("supportsLogPoints") };
    const juce::Identifier supportsModulesRequest             { juce::String::fromUTF8 ("supportsModulesRequest") };
    const juce::Identifier supportsReadMemoryRequest          { juce::String::fromUTF8 ("supportsReadMemoryRequest") };
    const juce::Identifier supportsRestartFrame               { juce::String::fromUTF8 ("supportsRestartFrame") };
    const juce::Identifier supportsSetExpression              { juce::String::fromUTF8 ("supportsSetExpression") };
    const juce::Identifier supportsSetVariable                { juce::String::fromUTF8 ("supportsSetVariable") };
    const juce::Identifier supportsStepBack                   { juce::String::fromUTF8 ("supportsStepBack") };
    const juce::Identifier supportsStepInTargetsRequest       { juce::String::fromUTF8 ("supportsStepInTargetsRequest") };
    const juce::Identifier supportsSteppingGranularity        { juce::String::fromUTF8 ("supportsSteppingGranularity") };
    const juce::Identifier supportsTerminateRequest           { juce::String::fromUTF8 ("supportsTerminateRequest") };
    const juce::Identifier supportsTerminateThreadsRequest    { juce::String::fromUTF8 ("supportsTerminateThreadsRequest") };
    const juce::Identifier supportsValueFormattingOptions     { juce::String::fromUTF8 ("supportsValueFormattingOptions") };
    const juce::Identifier supportTerminateDebuggee           { juce::String::fromUTF8 ("supportTerminateDebuggee") };
    const juce::Identifier systemProcessId                    { juce::String::fromUTF8 ("systemProcessId") };
    const juce::Identifier targetProcessId                    { juce::String::fromUTF8 ("targetProcessId") };
    const juce::Identifier terminate                          { juce::String::fromUTF8 ("terminate") };
    const juce::Identifier terminated                         { juce::String::fromUTF8 ("terminated") };
    const juce::Identifier terminateDeadlineMs                { juce::String::fromUTF8 ("terminateDeadlineMs") };
    const juce::Identifier terminateDebuggee                  { juce::String::fromUTF8 ("terminateDebuggee") };
    const juce::Identifier thread                             { juce::String::fromUTF8 ("thread") };
    const juce::Identifier threadId                           { juce::String::fromUTF8 ("threadId") };
    const juce::Identifier threads                            { juce::String::fromUTF8 ("threads") };
    const juce::Identifier totalFrames                        { juce::String::fromUTF8 ("totalFrames") };
    const juce::Identifier unhandled                          { juce::String::fromUTF8 ("unhandled") };
    const juce::Identifier variables                          { juce::String::fromUTF8 ("variables") };
    const juce::Identifier variablesReference                 { juce::String::fromUTF8 ("variablesReference") };
    const juce::Identifier verified                           { juce::String::fromUTF8 ("verified") };
    const juce::Identifier whatdbg                            { juce::String::fromUTF8 ("whatdbg") };

/**______________________________END OF NAMESPACE______________________________*/
}// namespace Id
namespace files
{
/*_____________________________________________________________________________*/

/** @brief File names whatdbg reads: help. */

const juce::String whatdbgHelp { juce::String::fromUTF8 ("HELP.md") };

/**______________________________END OF NAMESPACE______________________________*/
}// namespace files
