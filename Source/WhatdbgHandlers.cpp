#include "Whatdbg.h"
#include "Log.h"

using dap::DynObj;

void Whatdbg::handleInitialize (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    sendResponse (dap::makeResponse (seq, "initialize", true, dap::makeCapabilities ()));
    sendEvent (dap::makeEvent ("initialized"));
}

void Whatdbg::handleLaunch (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const juce::String program { dap::getString (args, "program") };

    logWrite ("[Whatdbg] launch: %s\n", program.toRawUTF8 ());

    const bool isLaunched { session.launch (program) };

    if (isLaunched)
    {
        // Configure symbol search — srv* enables Microsoft symbol server
        session.appendSymbolPath ("srv*");

        // Configure source path from DAP arguments if provided
        const juce::String cwd { dap::getString (args, "cwd") };

        if (cwd.isNotEmpty ())
        {
            session.appendSourcePath (cwd.replace ("/", "\\"));
            session.appendSymbolPath (cwd.replace ("/", "\\"));
        }

        state.executionState = debug::ExecutionState::launching;
        sendResponse (dap::makeResponse (seq, "launch", true));
    }
    else
    {
        sendResponse (dap::makeErrorResponse (seq, "launch", "Failed to launch process"));
    }
}

void Whatdbg::handleAttach (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const std::uint32_t pid { static_cast<std::uint32_t> (static_cast<int> (args["pid"])) };

    logWrite ("[Whatdbg] attach: pid=%lu\n", static_cast<unsigned long> (pid));

    const bool isAttached { session.attach (pid) };

    if (isAttached)
    {
        // Configure symbol search — srv* enables Microsoft symbol server
        session.appendSymbolPath ("srv*");

        // Configure source path from DAP arguments if provided
        const juce::String cwd { dap::getString (args, "cwd") };

        if (cwd.isNotEmpty ())
        {
            session.appendSourcePath (cwd.replace ("/", "\\"));
            session.appendSymbolPath (cwd.replace ("/", "\\"));
        }

        state.targetProcessId = pid;
        state.executionState = debug::ExecutionState::launching;
        sendResponse (dap::makeResponse (seq, "attach", true));
    }
    else
    {
        sendResponse (dap::makeErrorResponse (seq, "attach", "Failed to attach"));
    }
}

void Whatdbg::handleConfigurationDone (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    isConfigurationDone = true;

    if (state.executionState == debug::ExecutionState::stopped)
    {
        resolveAndResumeAfterInitialBreak ();
    }

    sendResponse (dap::makeResponse (seq, "configurationDone", true));
}

void Whatdbg::handleDisconnect (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::String command { request["command"].toString () };
    const juce::var& args { request["arguments"] };

    const bool isTerminate { command == "terminate" };
    shouldTerminateOnExit = isTerminate or static_cast<bool> (args["terminateDebuggee"]);

    sendResponse (dap::makeResponse (seq, command, true));
    isRunning = false;
}

void Whatdbg::handleSetBreakpoints (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const juce::var& source { args["source"] };
    const juce::String sourcePath { dap::getString (source, "path") };
    const juce::var& requestedBps { args["breakpoints"] };

    logWrite ("[Whatdbg] setBreakpoints: %s (%d breakpoints)\n",
              sourcePath.toRawUTF8 (),
              requestedBps.isArray () ? requestedBps.getArray ()->size () : 0);

    juce::Array<juce::var> resultBps { breakpointManager.handleSetBreakpoints (sourcePath, requestedBps) };

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("breakpoints", juce::var (resultBps));
    sendResponse (dap::makeResponse (seq, "setBreakpoints", true, juce::var (body)));
}

void Whatdbg::handleThreads (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    juce::Array<juce::var> threads { session.getThreads () };

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("threads", juce::var { threads });
    sendResponse (dap::makeResponse (seq, "threads", true, juce::var (body)));
}

void Whatdbg::handleStackTrace (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const int threadId { static_cast<int> (args["threadId"]) };

    if (threadId > 0)
    {
        session.setCurrentThreadBySystemId (static_cast<std::uint32_t> (threadId));
    }

    juce::Array<juce::var> frames { session.getStackTrace (50) };

    // Assign unique frame IDs and store threadId mapping
    for (auto& frameVar : frames)
    {
        if (auto* frameObj { frameVar.getDynamicObject () })
        {
            const int originalIndex { static_cast<int> (frameObj->getProperty ("id")) };
            const int uniqueId { nextFrameId++ };
            frameObj->setProperty ("id", uniqueId);
            frameIdMap[uniqueId] = { static_cast<std::uint32_t> (threadId), originalIndex };
        }
    }

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("stackFrames", juce::var { frames });
    body->setProperty ("totalFrames", frames.size ());
    sendResponse (dap::makeResponse (seq, "stackTrace", true, juce::var (body)));
}

void Whatdbg::handleScopes (const juce::var& request)
{
    const int seq     { static_cast<int> (request["seq"]) };
    const int frameId { static_cast<int> (request["arguments"]["frameId"]) };

    // Decode frameId to (threadSystemId, frameIndex)
    int frameIndex { frameId };
    std::uint32_t threadSystemId { 0 };

    if (frameIdMap.count (frameId) > 0)
    {
        const auto& entry { frameIdMap.at (frameId) };
        threadSystemId = entry.first;
        frameIndex = entry.second;

        if (threadSystemId > 0)
        {
            session.setCurrentThreadBySystemId (threadSystemId);
            lastScopesThreadId = threadSystemId;
        }
    }

    const int localsRef { nextVariablesRef++ };
    variablesRefMap[localsRef] = { frameIndex, -1 };

    DynObj localsScope { new juce::DynamicObject () };
    localsScope->setProperty ("name",               "Locals");
    localsScope->setProperty ("variablesReference",  localsRef);
    localsScope->setProperty ("expensive",           false);

    juce::Array<juce::var> scopes;
    scopes.add (juce::var (localsScope));

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("scopes", juce::var { scopes });
    sendResponse (dap::makeResponse (seq, "scopes", true, juce::var (body)));
}

void Whatdbg::handleVariables (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const int ref { static_cast<int> (request["arguments"]["variablesReference"]) };

    if (lastScopesThreadId > 0)
    {
        session.setCurrentThreadBySystemId (lastScopesThreadId);
    }

    juce::Array<juce::var> dapVariables;

    if (variablesRefMap.count (ref) > 0)
    {
        const auto& entry       { variablesRefMap.at (ref) };
        const int   frameIndex  { entry.first };
        const int   symbolIndex { entry.second };

        juce::Array<juce::var> rawVars;

        if (symbolIndex < 0)
        {
            rawVars = session.getLocals (frameIndex);
        }
        else
        {
            rawVars = session.getVariableChildren (frameIndex, symbolIndex);
        }

        for (const auto& rawVar : rawVars)
        {
            if (auto* obj { rawVar.getDynamicObject () })
            {
                const bool hasChildren { static_cast<bool> (obj->getProperty ("hasChildren")) };
                const int  symIdx      { static_cast<int>  (obj->getProperty ("symbolIndex")) };

                int childRef { 0 };

                if (hasChildren)
                {
                    childRef = nextVariablesRef++;
                    variablesRefMap[childRef] = { frameIndex, symIdx };
                }

                DynObj dapVar { new juce::DynamicObject () };
                dapVar->setProperty ("name",               obj->getProperty ("name"));
                dapVar->setProperty ("value",              obj->getProperty ("value"));
                dapVar->setProperty ("type",               obj->getProperty ("type"));
                dapVar->setProperty ("variablesReference",  childRef);

                dapVariables.add (juce::var (dapVar));
            }
        }
    }

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("variables", juce::var { dapVariables });
    sendResponse (dap::makeResponse (seq, "variables", true, juce::var (body)));
}

void Whatdbg::handleContinue (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    session.resume ();
    state.executionState = debug::ExecutionState::running;
    isStepPending = false;
    isPausePending = false;

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("allThreadsContinued", true);
    sendResponse (dap::makeResponse (seq, "continue", true, juce::var (body)));
}

void Whatdbg::handleNext (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    session.stepOver ();
    state.executionState = debug::ExecutionState::running;
    isStepPending = true;
    logWrite ("[Whatdbg] next issued\n");

    sendResponse (dap::makeResponse (seq, "next", true));
}

void Whatdbg::handleStepIn (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    session.stepInto ();
    state.executionState = debug::ExecutionState::running;
    isStepPending = true;
    logWrite ("[Whatdbg] stepIn issued\n");

    sendResponse (dap::makeResponse (seq, "stepIn", true));
}

void Whatdbg::handleStepOut (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    session.stepOut ();
    state.executionState = debug::ExecutionState::running;
    isStepPending = true;

    sendResponse (dap::makeResponse (seq, "stepOut", true));
}

void Whatdbg::handlePause (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    session.interrupt (state.targetProcessId);
    isPausePending = true;

    sendResponse (dap::makeResponse (seq, "pause", true));
}

void Whatdbg::handleEvaluate (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::var& args { request["arguments"] };
    const juce::String expression { dap::getString (args, "expression") };
    const int frameId { static_cast<int> (args["frameId"]) };

    const juce::String result { session.evaluateExpression (expression, frameId) };

    if (result.isNotEmpty ())
    {
        DynObj body { new juce::DynamicObject () };
        body->setProperty ("result",             result);
        body->setProperty ("variablesReference", 0);

        sendResponse (dap::makeResponse (seq, "evaluate", true, juce::var (body)));
    }
    else
    {
        sendResponse (dap::makeErrorResponse (seq, "evaluate",
            "Could not evaluate: " + expression));
    }
}
