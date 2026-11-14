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

/**
 * @file Identifiers.h
 * @brief whatdbg vocabulary.
 */

#pragma once

namespace Id
{
/*_____________________________________________________________________________*/

/** @brief whatdbg vocabulary — the Model names, the engine events, and the words of the Debug Adapter Protocol. */

extern const juce::Identifier adapterExitCode;
extern const juce::Identifier allThreadsContinued;
extern const juce::Identifier allThreadsStopped;
extern const juce::Identifier arguments;
extern const juce::Identifier attach;
extern const juce::Identifier breakMode;
extern const juce::Identifier breakpoint;
extern const juce::Identifier breakpointEngineId;
extern const juce::Identifier breakpointLocationFound;
extern const juce::Identifier breakpoints;
extern const juce::Identifier category;
extern const juce::Identifier changed;
extern const juce::Identifier column;
extern const juce::Identifier configurationDone;
extern const juce::Identifier console;
extern const juce::Identifier context;
extern const juce::Identifier continueCommand;
extern const juce::Identifier disconnect;
extern const juce::Identifier evaluate;
extern const juce::Identifier exception;
extern const juce::Identifier exceptionAddress;
extern const juce::Identifier exceptionCode;
extern const juce::Identifier exceptionId;
extern const juce::Identifier exceptionInfo;
extern const juce::Identifier executionState;
extern const juce::Identifier exitCode;
extern const juce::Identifier exited;
extern const juce::Identifier expensive;
extern const juce::Identifier expression;
extern const juce::Identifier frameId;
extern const juce::Identifier hitBreakpointIds;
extern const juce::Identifier hover;
extern const juce::Identifier initialBreak;
extern const juce::Identifier initialize;
extern const juce::Identifier initialized;
extern const juce::Identifier isConfigurationDone;
extern const juce::Identifier isLocalProcess;
extern const juce::Identifier isMachException;
extern const juce::Identifier isPausePending;
extern const juce::Identifier isRunning;
extern const juce::Identifier isStepPending;
extern const juce::Identifier launch;
extern const juce::Identifier moduleLoad;
extern const juce::Identifier next;
extern const juce::Identifier nextSequenceNumber;
extern const juce::Identifier numMessages;
extern const juce::Identifier output;
extern const juce::Identifier pause;
extern const juce::Identifier pid;
extern const juce::Identifier prepare;
extern const juce::Identifier process;
extern const juce::Identifier processExitCode;
extern const juce::Identifier program;
extern const juce::Identifier reason;
extern const juce::Identifier releaseResources;
extern const juce::Identifier request;
extern const juce::Identifier requestSeq;
extern const juce::Identifier response;
extern const juce::Identifier result;
extern const juce::Identifier running;
extern const juce::Identifier scopes;
extern const juce::Identifier seq;
extern const juce::Identifier session;
extern const juce::Identifier setBreakpoints;
extern const juce::Identifier shouldTerminateOnExit;
extern const juce::Identifier source;
extern const juce::Identifier stackFrames;
extern const juce::Identifier stackTrace;
extern const juce::Identifier standardError;
extern const juce::Identifier standardOutput;
extern const juce::Identifier started;
extern const juce::Identifier startMethod;
extern const juce::Identifier step;
extern const juce::Identifier stepIn;
extern const juce::Identifier stepOut;
extern const juce::Identifier stopped;
extern const juce::Identifier stopReason;
extern const juce::Identifier success;
extern const juce::Identifier supportsBreakpointLocationsRequest;
extern const juce::Identifier supportsCancelRequest;
extern const juce::Identifier supportsClipboardContext;
extern const juce::Identifier supportsCompletionsRequest;
extern const juce::Identifier supportsConditionalBreakpoints;
extern const juce::Identifier supportsConfigurationDoneRequest;
extern const juce::Identifier supportsDataBreakpoints;
extern const juce::Identifier supportsDelayedStackTraceLoading;
extern const juce::Identifier supportsDisassembleRequest;
extern const juce::Identifier supportsEvaluateForHovers;
extern const juce::Identifier supportsExceptionFilterOptions;
extern const juce::Identifier supportsExceptionInfoRequest;
extern const juce::Identifier supportsExceptionOptions;
extern const juce::Identifier supportsFunctionBreakpoints;
extern const juce::Identifier supportsGotoTargetsRequest;
extern const juce::Identifier supportsHitConditionalBreakpoints;
extern const juce::Identifier supportsInstructionBreakpoints;
extern const juce::Identifier supportsLoadedSourcesRequest;
extern const juce::Identifier supportsLogPoints;
extern const juce::Identifier supportsModulesRequest;
extern const juce::Identifier supportsReadMemoryRequest;
extern const juce::Identifier supportsRestartFrame;
extern const juce::Identifier supportsSetExpression;
extern const juce::Identifier supportsSetVariable;
extern const juce::Identifier supportsStepBack;
extern const juce::Identifier supportsStepInTargetsRequest;
extern const juce::Identifier supportsSteppingGranularity;
extern const juce::Identifier supportsTerminateRequest;
extern const juce::Identifier supportsTerminateThreadsRequest;
extern const juce::Identifier supportsValueFormattingOptions;
extern const juce::Identifier supportTerminateDebuggee;
extern const juce::Identifier systemProcessId;
extern const juce::Identifier targetProcessId;
extern const juce::Identifier terminate;
extern const juce::Identifier terminated;
extern const juce::Identifier terminateDeadlineMs;
extern const juce::Identifier terminateDebuggee;
extern const juce::Identifier thread;
extern const juce::Identifier threadId;
extern const juce::Identifier threads;
extern const juce::Identifier totalFrames;
extern const juce::Identifier unhandled;
extern const juce::Identifier variables;
extern const juce::Identifier variablesReference;
extern const juce::Identifier verified;
extern const juce::Identifier whatdbg;

/**______________________________END OF NAMESPACE______________________________*/
}// namespace Id
