#include "Whatdbg.h"
#include "Log.h"

#include <iostream>
#include <fcntl.h>
#include <io.h>

static constexpr ULONG kPollTimeoutMs { 50 };

using DynObj = juce::ReferenceCountedObjectPtr<juce::DynamicObject>;

Whatdbg::Whatdbg ()
    : breakpointManager { session }
{
}

bool Whatdbg::initialize (const juce::File& sidecarDir) noexcept
{
    // Set stdout to binary mode for DAP framing
    _setmode (_fileno (stdout), _O_BINARY);

    return session.initialize (sidecarDir);
}

void Whatdbg::run ()
{
    reader.start ();
    logWrite ("[Whatdbg] main loop started\n");

    while (isRunning)
    {
        // 1. Drain FIFO — process all pending DAP commands
        juce::var message;

        while (reader.tryPop (message))
        {
            handleCommand (message);
        }

        // 2. Poll dbgeng events (skip when target stopped or idle)
        if (state.executionState == debug::ExecutionState::running
            or state.executionState == debug::ExecutionState::launching)
        {
            HRESULT pollResult { session.pollEvents (kPollTimeoutMs) };
            if (pollResult == S_OK)
            {
                logWrite ("[Whatdbg] WaitForEvent returned S_OK\n");

                if (isStepPending
                    and not state.hasBreakpointHit
                    and not state.hasStepCompleted
                    and not state.hasNewModuleLoaded
                    and not state.hasProcessExited)
                {
                    state.hasStepCompleted = true;
                    state.executionState = debug::ExecutionState::stopped;
                    isStepPending = false;
                    logWrite ("[Whatdbg] step completed (detected from WaitForEvent)\n");
                }

                if (isPausePending
                    and not state.hasBreakpointHit
                    and not state.hasStepCompleted
                    and not state.hasNewModuleLoaded
                    and not state.hasProcessExited)
                {
                    state.executionState = debug::ExecutionState::stopped;
                    isPausePending = false;

                    DynObj body { new juce::DynamicObject () };
                    body->setProperty ("reason",            "pause");
                    body->setProperty ("threadId",          1);
                    body->setProperty ("allThreadsStopped", true);

                    sendEvent (dap::makeEvent ("stopped", juce::var (body)));
                    resetVariablesState ();
                    logWrite ("[Whatdbg] pause completed, emitted stopped event\n");
                }
            }
            else if (pollResult == S_FALSE and isPausePending)
            {
                logWrite ("[Whatdbg] WaitForEvent timeout (pause pending)\n");
            }
            else if (pollResult != S_FALSE)
            {
                logWrite ("[Whatdbg] WaitForEvent returned hr=0x%08lX\n",
                          static_cast<unsigned long> (pollResult));
            }
        }

        // 3. Process deferred events
        processDeferredEvents ();

        // 4. Yield when idle (no target)
        if (state.executionState == debug::ExecutionState::idle)
        {
            juce::Thread::sleep (10);
        }
    }

    reader.stop ();
    session.shutdown ();
    logWrite ("[Whatdbg] main loop ended\n");
}

void Whatdbg::handleCommand (const juce::var& message)
{
    const juce::String type { message["type"].toString () };

    if (type == "request")
    {
        const juce::String command { message["command"].toString () };
        const int seq { static_cast<int> (message["seq"]) };
        logWrite ("[Whatdbg] command=%s seq=%d\n", command.toRawUTF8 (), seq);

        if      (command == "initialize")        handleInitialize (message);
        else if (command == "launch")            handleLaunch (message);
        else if (command == "attach")            handleAttach (message);
        else if (command == "configurationDone") handleConfigurationDone (message);
        else if (command == "disconnect")        handleDisconnect (message);
        else if (command == "terminate")         handleDisconnect (message);
        else if (command == "setBreakpoints")    handleSetBreakpoints (message);
        else if (command == "threads")           handleThreads (message);
        else if (command == "stackTrace")        handleStackTrace (message);
        else if (command == "scopes")            handleScopes (message);
        else if (command == "variables")         handleVariables (message);
        else if (command == "continue")          handleContinue (message);
        else if (command == "next")          handleNext (message);
        else if (command == "stepIn")        handleStepIn (message);
        else if (command == "stepOut")       handleStepOut (message);
        else if (command == "pause")         handlePause (message);
        else
        {
            sendResponse (dap::makeErrorResponse (seq, command, "Unknown command"));
        }
    }
}

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
    const ULONG pid { static_cast<ULONG> (static_cast<int> (args["pid"])) };

    logWrite ("[Whatdbg] attach: pid=%lu\n", pid);

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
        session.resume ();
        state.executionState = debug::ExecutionState::running;
        state.isInitialBreakSeen = false;

        DynObj threadBody { new juce::DynamicObject () };
        threadBody->setProperty ("reason",   "started");
        threadBody->setProperty ("threadId", 1);
        sendEvent (dap::makeEvent ("thread", juce::var (threadBody)));

        logWrite ("[Whatdbg] resumed after configurationDone\n");
    }

    sendResponse (dap::makeResponse (seq, "configurationDone", true));
}

void Whatdbg::handleDisconnect (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    sendResponse (dap::makeResponse (seq, request["command"].toString (), true));
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

    DynObj thread { new juce::DynamicObject () };
    thread->setProperty ("id", 1);
    thread->setProperty ("name", "Main Thread");

    juce::Array<juce::var> threads;
    threads.add (juce::var (thread));

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("threads", juce::var { threads });
    sendResponse (dap::makeResponse (seq, "threads", true, juce::var (body)));
}

void Whatdbg::handleStackTrace (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    juce::Array<juce::var> frames { session.getStackTrace (50) };

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("stackFrames", juce::var { frames });
    body->setProperty ("totalFrames", frames.size ());
    sendResponse (dap::makeResponse (seq, "stackTrace", true, juce::var (body)));
}

void Whatdbg::resetVariablesState () noexcept
{
    nextVariablesRef = 1;
    variablesRefMap.clear ();
}

void Whatdbg::handleScopes (const juce::var& request)
{
    const int seq     { static_cast<int> (request["seq"]) };
    const int frameId { static_cast<int> (request["arguments"]["frameId"]) };

    const int localsRef { nextVariablesRef++ };
    variablesRefMap[localsRef] = { frameId, -1 };

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

void Whatdbg::processDeferredEvents ()
{
    // Initial breakpoint: resume if configurationDone already received
    if (state.isInitialBreakSeen
        and state.executionState == debug::ExecutionState::stopped
        and isConfigurationDone
        and not state.hasBreakpointHit
        and not state.hasStepCompleted)
    {
        session.resume ();
        state.executionState = debug::ExecutionState::running;
        state.isInitialBreakSeen = false;

        DynObj threadBody { new juce::DynamicObject () };
        threadBody->setProperty ("reason",   "started");
        threadBody->setProperty ("threadId", 1);
        sendEvent (dap::makeEvent ("thread", juce::var (threadBody)));

        logWrite ("[Whatdbg] resumed after initial breakpoint\n");
    }

    // Breakpoint hit — emit stopped event
    if (state.hasBreakpointHit)
    {
        state.hasBreakpointHit = false;

        juce::var stoppedBody { breakpointManager.onBreakpointHit (
            state.breakpointEngineId, state.breakpointThreadId) };

        sendEvent (dap::makeEvent ("stopped", stoppedBody));
        resetVariablesState ();
        logWrite ("[Whatdbg] breakpoint hit, emitted stopped event\n");
    }

    // Step completed — emit stopped event
    if (state.hasStepCompleted)
    {
        state.hasStepCompleted = false;

        DynObj body { new juce::DynamicObject () };
        body->setProperty ("reason",            "step");
        body->setProperty ("threadId",          1);
        body->setProperty ("allThreadsStopped", true);

        sendEvent (dap::makeEvent ("stopped", juce::var (body)));
        resetVariablesState ();
        logWrite ("[Whatdbg] step completed, emitted stopped event\n");
    }

    // Module load with pending breakpoints — resolve
    if (state.hasNewModuleLoaded)
    {
        state.hasNewModuleLoaded = false;

        if (breakpointManager.hasPending ())
        {
            session.loadModuleSymbols (state.lastLoadedImageName);
            juce::Array<juce::var> events { breakpointManager.onModuleLoad () };

            if (not events.isEmpty ())
            {
                for (const auto& event : events)
                {
                    sendEvent (event);
                }

                logWrite ("[Whatdbg] resolved %d pending BPs after module load\n", events.size ());
            }

            // Resume target (LoadModule returned DEBUG_STATUS_BREAK)
            session.resume ();
            state.executionState = debug::ExecutionState::running;
        }
    }

    // Debuggee output (OutputDebugString)
    if (state.hasDebuggeeOutput)
    {
        state.hasDebuggeeOutput = false;
        const juce::String text { state.debuggeeOutputText };
        state.debuggeeOutputText.clear ();

        DynObj body { new juce::DynamicObject () };
        body->setProperty ("category", "console");
        body->setProperty ("output",   text);

        sendEvent (dap::makeEvent ("output", juce::var (body)));
    }

    // Process exit
    if (state.hasProcessExited)
    {
        state.hasProcessExited = false;

        DynObj exitBody { new juce::DynamicObject () };
        exitBody->setProperty ("exitCode", state.processExitCode);
        sendEvent (dap::makeEvent ("exited", juce::var (exitBody)));
        sendEvent (dap::makeEvent ("terminated"));

        state.executionState = debug::ExecutionState::exited;
    }
}

void Whatdbg::writeMessage (const juce::var& message) noexcept
{
    const juce::String jsonBody { juce::JSON::toString (message, true) };
    const juce::CharPointer_UTF8 utf8 { jsonBody.toUTF8 () };
    const size_t byteCount { strlen (utf8.getAddress ()) };

    const std::string header { "Content-Length: " + std::to_string (byteCount) + "\r\n\r\n" };

    std::cout.write (header.data (), static_cast<std::streamsize> (header.size ()));
    std::cout.write (utf8.getAddress (), static_cast<std::streamsize> (byteCount));
    std::cout.flush ();
}

void Whatdbg::sendResponse (const juce::var& response) noexcept
{
    writeMessage (response);
}

void Whatdbg::sendEvent (const juce::var& event) noexcept
{
    writeMessage (event);
}
