/** @file WhatdbgHandlers.cpp
 *  @brief DAP command handler implementations.
 *
 *  Each handler corresponds to a DAP request (initialize, launch, attach,
 *  setBreakpoints, continue, next, stepIn, stepOut, threads, stackTrace,
 *  scopes, variables, evaluate, disconnect, etc.). Handlers read the DAP
 *  request body, delegate to Session, and write the DAP response to stdout.
 */

#include "Whatdbg.h"

using dap::DynObj;

static constexpr int maxStackFrames { 50 };

void Whatdbg::onInitialize (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    writeMessage (dap::getResponse (seq, "initialize", true, dap::getCapabilities ()));
    writeMessage (dap::getEvent ("initialized"));
}

void Whatdbg::onLaunch (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const juce::String program { dap::getString (args, "program") };

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] launch: " + program);
#endif

    const bool isLaunched { session.launch (program) };

    if (isLaunched)
    {
        addSearchPaths (dap::getString (args, "cwd"));

        state.executionState = debug::ExecutionState::launching;
        writeMessage (dap::getResponse (seq, "launch", true));
    }
    else
    {
        writeMessage (dap::getErrorResponse (seq, "launch", "Failed to launch process"));
    }
}

void Whatdbg::addSearchPaths (const juce::String& cwd)
{
#if JUCE_WINDOWS
    session.appendSymbolPath ("srv*");
#endif

    if (cwd.isNotEmpty ())
    {
#if JUCE_WINDOWS
        session.appendSourcePath (cwd.replace ("/", "\\"));
        session.appendSymbolPath (cwd.replace ("/", "\\"));
#else
        session.appendSourcePath (cwd);
        session.appendSymbolPath (cwd);
#endif
    }
}

void Whatdbg::onAttach (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const std::uint32_t pid { static_cast<std::uint32_t> (static_cast<int> (args["pid"])) };

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] attach: pid=" + juce::String (static_cast<unsigned long> (pid)));
#endif

    const bool isAttached { session.attach (pid) };

    if (isAttached)
    {
        addSearchPaths (dap::getString (args, "cwd"));

        state.targetProcessId = pid;

       #if JUCE_WINDOWS
        state.executionState = debug::ExecutionState::launching;
       #else
        state.executionState    = debug::ExecutionState::stopped;
        state.initialBreakPhase = debug::InitialBreakPhase::pending;
       #endif

        writeMessage (dap::getResponse (seq, "attach", true));
    }
    else
    {
        writeMessage (dap::getErrorResponse (seq, "attach", "Failed to attach"));
    }
}

void Whatdbg::onConfigurationDone (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    state.isConfigurationDone = true;

    if (state.executionState == debug::ExecutionState::stopped)
    {
        resumeAfterInitialBreak ();
    }

    writeMessage (dap::getResponse (seq, "configurationDone", true));
}

void Whatdbg::onDisconnect (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::String command { request["command"].toString () };
    const juce::var& args { request["arguments"] };

    const bool isTerminate { command == "terminate" };
    state.shouldTerminateOnExit = isTerminate or static_cast<bool> (args["terminateDebuggee"]);

    writeMessage (dap::getResponse (seq, command, true));

    const bool isDebuggeeAlive { state.executionState != debug::ExecutionState::idle
                              and state.executionState != debug::ExecutionState::exited };

    if (state.shouldTerminateOnExit and isDebuggeeAlive)
    {
        session.terminateDebuggee (state.targetProcessId);

        // "running" here means the poll gate in run() must keep calling
        // pollEvents() until the exit event arrives — terminateDebuggee kills
        // the debuggee, it does not observe its exit synchronously on either
        // platform. terminateDeadlineMs bounds that wait.
        state.executionState = debug::ExecutionState::running;
        state.terminateDeadlineMs = juce::Time::getMillisecondCounter () + terminateTimeoutMs;
    }
    else
    {
        state.isRunning = false;
    }
}

void Whatdbg::onSetBreakpoints (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const juce::var& source { args["source"] };
    const juce::String sourcePath { dap::getString (source, "path") };
    const juce::var& requestedBps { args["breakpoints"] };

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] setBreakpoints: " + sourcePath + " ("
                             + juce::String (requestedBps.isArray () ? requestedBps.getArray ()->size () : 0)
                             + " breakpoints)");
#endif

    juce::Array<juce::var> resultBps { breakpointManager.onSetBreakpoints (sourcePath, requestedBps) };

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("breakpoints", juce::var (resultBps));
    writeMessage (dap::getResponse (seq, "setBreakpoints", true, juce::var (body)));
}

void Whatdbg::onThreads (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    juce::Array<juce::var> threads { session.getThreads () };

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("threads", juce::var { threads });
    writeMessage (dap::getResponse (seq, "threads", true, juce::var (body)));
}

void Whatdbg::onStackTrace (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const int threadId { static_cast<int> (args["threadId"]) };

    if (threadId > 0)
    {
        session.setCurrentThreadBySystemId (static_cast<std::uint32_t> (threadId));
    }

    juce::Array<juce::var> frames { session.getStackTrace (maxStackFrames) };

    // Assign unique frame IDs and store threadId mapping
    for (auto& frameVar : frames)
    {
        if (auto* frameObj { frameVar.getDynamicObject () })
        {
            const int originalIndex { static_cast<int> (frameObj->getProperty ("id")) };
            const int uniqueId { state.nextFrameId++ };
            frameObj->setProperty ("id", uniqueId);
            state.frameIdMap.insert_or_assign (uniqueId,
                std::pair<std::uint32_t, int> { static_cast<std::uint32_t> (threadId), originalIndex });
        }
    }

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("stackFrames", juce::var { frames });
    body->setProperty ("totalFrames", frames.size ());
    writeMessage (dap::getResponse (seq, "stackTrace", true, juce::var (body)));
}

std::pair<std::uint32_t, int> Whatdbg::selectFrameFromId (int frameId)
{
    std::uint32_t threadSystemId { 0 };
    int frameIndex { frameId };

    const auto frameEntry { state.frameIdMap.find (frameId) };

    if (frameEntry != state.frameIdMap.end ())
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

void Whatdbg::onScopes (const juce::var& request)
{
    const int seq     { static_cast<int> (request["seq"]) };
    const int frameId { static_cast<int> (request["arguments"]["frameId"]) };

    const auto [threadSystemId, frameIndex] { selectFrameFromId (frameId) };

    if (threadSystemId > 0)
    {
        state.lastScopesThreadId = threadSystemId;
    }

    const int localsRef { state.nextVariablesRef++ };
    state.variablesRefMap.insert_or_assign (localsRef, std::pair<int, int> { frameIndex, -1 });

    DynObj localsScope { new juce::DynamicObject () };
    localsScope->setProperty ("name",               "Locals");
    localsScope->setProperty ("variablesReference",  localsRef);
    localsScope->setProperty ("expensive",           false);

    juce::Array<juce::var> scopes;
    scopes.add (juce::var (localsScope));

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("scopes", juce::var { scopes });
    writeMessage (dap::getResponse (seq, "scopes", true, juce::var (body)));
}

juce::Array<juce::var> Whatdbg::getDapVariables (int ref)
{
    juce::Array<juce::var> dapVariables;

    const auto variablesEntry { state.variablesRefMap.find (ref) };

    if (variablesEntry != state.variablesRefMap.end ())
    {
        const auto& [variablesKey, variablesLocation] { *variablesEntry };
        const auto& [frameIndex, symbolIndex] { variablesLocation };

        juce::Array<juce::var> rawVars { symbolIndex < 0
            ? session.getLocals (frameIndex)
            : session.getVariableChildren (frameIndex, symbolIndex) };

        for (const auto& rawVar : rawVars)
            addDapVariable (dapVariables, rawVar, frameIndex);
    }

    return dapVariables;
}

void Whatdbg::onVariables (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const int ref { static_cast<int> (request["arguments"]["variablesReference"]) };

    if (state.lastScopesThreadId > 0)
    {
        session.setCurrentThreadBySystemId (state.lastScopesThreadId);
    }

    juce::Array<juce::var> dapVariables { getDapVariables (ref) };

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("variables", juce::var { dapVariables });
    writeMessage (dap::getResponse (seq, "variables", true, juce::var (body)));
}

void Whatdbg::addDapVariable (juce::Array<juce::var>& dapVariables, const juce::var& rawVar, int frameIndex)
{
    if (auto* obj { rawVar.getDynamicObject () })
    {
        const bool hasChildren { static_cast<bool> (obj->getProperty ("hasChildren")) };
        const int  symIdx      { static_cast<int>  (obj->getProperty ("symbolIndex")) };

        int childRef { 0 };

        if (hasChildren)
        {
            childRef = state.nextVariablesRef++;
            state.variablesRefMap.insert_or_assign (childRef, std::pair<int, int> { frameIndex, symIdx });
        }

        DynObj dapVar { new juce::DynamicObject () };
        dapVar->setProperty ("name",               obj->getProperty ("name"));
        dapVar->setProperty ("value",              obj->getProperty ("value"));
        dapVar->setProperty ("type",               obj->getProperty ("type"));
        dapVar->setProperty ("variablesReference",  childRef);

        dapVariables.add (juce::var (dapVar));
    }
}

void Whatdbg::onContinue (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    resumeExecution ();
    state.isStepPending = false;
    state.isPausePending = false;

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("allThreadsContinued", true);
    writeMessage (dap::getResponse (seq, "continue", true, juce::var (body)));
}

void Whatdbg::onNext (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    session.stepOver ();
    state.executionState = debug::ExecutionState::running;
    state.isStepPending = true;
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] next issued");
#endif

    writeMessage (dap::getResponse (seq, "next", true));
}

void Whatdbg::onStepIn (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    session.stepInto ();
    state.executionState = debug::ExecutionState::running;
    state.isStepPending = true;
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] stepIn issued");
#endif

    writeMessage (dap::getResponse (seq, "stepIn", true));
}

void Whatdbg::onStepOut (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    session.stepOut ();
    state.executionState = debug::ExecutionState::running;
    state.isStepPending = true;
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] stepOut issued");
#endif

    writeMessage (dap::getResponse (seq, "stepOut", true));
}

void Whatdbg::onPause (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    session.interrupt (state.targetProcessId);
    state.executionState = debug::ExecutionState::running;
    state.isPausePending = true;

    writeMessage (dap::getResponse (seq, "pause", true));
}

void Whatdbg::onExceptionInfo (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    const juce::String codeHex    { juce::String::toHexString (static_cast<juce::int64> (state.exceptionCode)) };
    const juce::String addressHex { juce::String::toHexString (static_cast<juce::int64> (state.exceptionAddress)) };
    const juce::String exceptionId { debug::getExceptionName (state.exceptionCode, state.isMachException) };

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("exceptionId", exceptionId);
    body->setProperty ("description", juce::String ("0x") + codeHex + " at 0x" + addressHex);
    body->setProperty ("breakMode",   "unhandled");

    writeMessage (dap::getResponse (seq, "exceptionInfo", true, juce::var (body)));
}

void Whatdbg::onEvaluate (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const juce::String expression { dap::getString (args, "expression") };
    const int frameId { static_cast<int> (args["frameId"]) };

    const auto [threadSystemId, frameIndex] { selectFrameFromId (frameId) };
    juce::ignoreUnused (threadSystemId);

    const juce::String result { session.evaluateExpression (expression, frameIndex) };

    if (result.isNotEmpty ())
    {
        DynObj body { new juce::DynamicObject () };
        body->setProperty ("result",             result);
        body->setProperty ("variablesReference", 0);

        writeMessage (dap::getResponse (seq, "evaluate", true, juce::var (body)));
    }
    else
    {
        writeMessage (dap::getErrorResponse (seq, "evaluate",
            "Could not evaluate: " + expression));
    }
}
