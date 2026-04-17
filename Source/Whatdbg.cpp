#include "Whatdbg.h"
#include "Log.h"

#include <cstdint>
#include <iostream>
#if JUCE_WINDOWS
#include <fcntl.h>
#include <io.h>
#endif

static constexpr std::uint32_t pollTimeoutMs { 50 };

using dap::DynObj;

Whatdbg::Whatdbg ()
    : breakpointManager { session }
{
    commandHandlers = {
        { "initialize",        [this] (const juce::var& m) { handleInitialize (m); } },
        { "launch",            [this] (const juce::var& m) { handleLaunch (m); } },
        { "attach",            [this] (const juce::var& m) { handleAttach (m); } },
        { "configurationDone", [this] (const juce::var& m) { handleConfigurationDone (m); } },
        { "disconnect",        [this] (const juce::var& m) { handleDisconnect (m); } },
        { "terminate",         [this] (const juce::var& m) { handleDisconnect (m); } },
        { "setBreakpoints",    [this] (const juce::var& m) { handleSetBreakpoints (m); } },
        { "threads",           [this] (const juce::var& m) { handleThreads (m); } },
        { "stackTrace",        [this] (const juce::var& m) { handleStackTrace (m); } },
        { "scopes",            [this] (const juce::var& m) { handleScopes (m); } },
        { "variables",         [this] (const juce::var& m) { handleVariables (m); } },
        { "continue",          [this] (const juce::var& m) { handleContinue (m); } },
        { "next",              [this] (const juce::var& m) { handleNext (m); } },
        { "stepIn",            [this] (const juce::var& m) { handleStepIn (m); } },
        { "stepOut",           [this] (const juce::var& m) { handleStepOut (m); } },
        { "pause",             [this] (const juce::var& m) { handlePause (m); } },
        { "evaluate",          [this] (const juce::var& m) { handleEvaluate (m); } },
        { "exceptionInfo",     [this] (const juce::var& m) { handleExceptionInfo (m); } },
    };
}

bool Whatdbg::initialize (const juce::File& sidecarDir) noexcept
{
#if JUCE_WINDOWS
    // Windows stdout defaults to text mode (CRLF translation); DAP protocol is binary.
    _setmode (_fileno (stdout), _O_BINARY);
#endif

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
            bool hadEvent { false };
            const juce::Result pollResult { session.pollEvents (pollTimeoutMs, hadEvent) };

            if (pollResult.wasOk () and hadEvent)
            {
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
                    body->setProperty ("threadId",          static_cast<int> (session.getEventThreadSystemId ()));
                    body->setProperty ("allThreadsStopped", true);

                    sendEvent (dap::makeEvent ("stopped", juce::var (body)));
                    resetVariablesState ();
                    logWrite ("[Whatdbg] pause completed, emitted stopped event\n");
                }
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

    logWrite ("[diag] Whatdbg::run exiting main loop shouldTerminateOnExit=%d executionState=%d\n",
              static_cast<int> (shouldTerminateOnExit),
              static_cast<int> (state.executionState));

    reader.stop ();
    debug::EndMode endMode { debug::EndMode::detach };

    if (state.executionState == debug::ExecutionState::exited)
        endMode = debug::EndMode::passive;
    else if (shouldTerminateOnExit)
        endMode = debug::EndMode::terminate;

    session.shutdown (endMode);
}

void Whatdbg::handleCommand (const juce::var& message)
{
    const juce::String type { message["type"].toString () };

    if (type == "request")
    {
        const juce::String command { message["command"].toString () };
        const int seq { static_cast<int> (message["seq"]) };

        logWrite ("[Whatdbg] command=%s seq=%d\n", command.toRawUTF8 (), seq);

        const auto it { commandHandlers.find (command.toStdString ()) };

        if (it != commandHandlers.end ())
        {
            it->second (message);
        }
        else
        {
            sendResponse (dap::makeErrorResponse (seq, command, "Unknown command"));
        }
    }
}

void Whatdbg::processDeferredEvents ()
{
    // Initial breakpoint: resume if configurationDone already received
    if (state.initialBreakPhase == debug::InitialBreakPhase::pending
        and state.executionState == debug::ExecutionState::stopped
        and isConfigurationDone
        and not state.hasBreakpointHit
        and not state.hasStepCompleted)
    {
        resolveAndResumeAfterInitialBreak ();
    }

    // Breakpoint hit — check if it's a user BP or an internal BP (e.g., stepOut's "gu")
    if (state.hasBreakpointHit)
    {
        state.hasBreakpointHit = false;

        if (isStepPending and not breakpointManager.isUserBreakpoint (state.breakpointEngineId))
        {
            // Internal breakpoint from stepOut ("gu") — treat as step completion
            isStepPending = false;

            DynObj body { new juce::DynamicObject () };
            body->setProperty ("reason",            "step");
            body->setProperty ("threadId",          static_cast<int> (session.getEventThreadSystemId ()));
            body->setProperty ("allThreadsStopped", true);

            sendEvent (dap::makeEvent ("stopped", juce::var (body)));
            resetVariablesState ();
            logWrite ("[Whatdbg] stepOut completed (internal BP), emitted stopped event\n");
        }
        else
        {
            juce::var stoppedBody { breakpointManager.onBreakpointHit (
                state.breakpointEngineId,
                session.getEventThreadSystemId ()) };

            sendEvent (dap::makeEvent ("stopped", stoppedBody));
            resetVariablesState ();
            logWrite ("[Whatdbg] breakpoint hit, emitted stopped event\n");
        }
    }

    // Step completed — emit stopped event
    if (state.hasStepCompleted)
    {
        state.hasStepCompleted = false;

        DynObj body { new juce::DynamicObject () };
        body->setProperty ("reason",            "step");
        body->setProperty ("threadId",          static_cast<int> (session.getEventThreadSystemId ()));
        body->setProperty ("allThreadsStopped", true);

        sendEvent (dap::makeEvent ("stopped", juce::var (body)));
        resetVariablesState ();
        logWrite ("[Whatdbg] step completed, emitted stopped event\n");
    }

    // Module load with pending breakpoints — resolve
    if (state.hasNewModuleLoaded)
    {
        state.hasNewModuleLoaded = false;
        const bool wasStopped { state.executionState == debug::ExecutionState::stopped };

        if (breakpointManager.hasPending ())
        {
            juce::ignoreUnused (session.loadModuleSymbols (state.lastLoadedImageName));
            juce::Array<juce::var> events { breakpointManager.onModuleLoad () };

            if (not events.isEmpty ())
            {
                for (const auto& event : events)
                {
                    sendEvent (event);
                }

                logWrite ("[Whatdbg] resolved %d pending BPs after module load\n", events.size ());
            }
        }

        // Windows: dbgeng's LoadModule callback returns DEBUG_STATUS_BREAK, so state
        // was stopped and we must re-resume. macOS: eBroadcastBitModulesLoaded fires
        // from an already-running target — no pause ever occurred, skip the resume.
        if (wasStopped)
        {
            session.resume ();
            state.executionState = debug::ExecutionState::running;
        }
    }

    // Breakpoint location resolved asynchronously by liblldb (target loaded
    // a new module that resolved a previously-pending BP location).
    if (state.hasBreakpointLocationsResolved)
    {
        state.hasBreakpointLocationsResolved = false;

        const std::uint32_t engineId     { state.resolvedBreakpointEngineId };
        const std::uint32_t resolvedLine { state.resolvedBreakpointLine };

        const int dapId { breakpointManager.onBreakpointLocationsResolved (engineId, resolvedLine) };

        if (dapId > 0)
        {
            DynObj bpObj { new juce::DynamicObject () };
            bpObj->setProperty ("id",       dapId);
            bpObj->setProperty ("verified", true);
            bpObj->setProperty ("line",     static_cast<int> (resolvedLine));

            DynObj body { new juce::DynamicObject () };
            body->setProperty ("reason",     "changed");
            body->setProperty ("breakpoint", juce::var (bpObj));

            sendEvent (dap::makeEvent ("breakpoint", juce::var (body)));

            logWrite ("[Whatdbg] BP resolved async: engineId=%u dapId=%d line=%u\n",
                      engineId, dapId, resolvedLine);
        }
    }

    // Debuggee output (OutputDebugString)
    if (state.hasDebuggeeOutput)
    {
        state.hasDebuggeeOutput = false;
        const juce::String text { state.debuggeeOutputText };
        state.debuggeeOutputText.clear ();

        logWrite ("[diag] processDeferredEvents emitting output event bytes=%d\n",
                  text.length ());

        DynObj body { new juce::DynamicObject () };
        body->setProperty ("category", "console");
        body->setProperty ("output",   text);

        sendEvent (dap::makeEvent ("output", juce::var (body)));
    }

    // Unhandled exception (target crash)
    if (state.hasExceptionStopped)
    {
        state.hasExceptionStopped = false;

        const juce::String addressHex   { juce::String::toHexString (static_cast<juce::int64> (state.exceptionAddress)) };
        const juce::String exceptionName { debug::getExceptionName (state.exceptionCode) };
        const juce::String codeHex       { juce::String::toHexString (static_cast<juce::int64> (state.exceptionCode)) };

        const juce::String description { juce::String ("0x") + codeHex + " at 0x" + addressHex };
        const int threadId { static_cast<int> (session.getEventThreadSystemId ()) };

        logWrite ("[Whatdbg] exception stopped: %s %s (threadId=%d)\n",
                  exceptionName.toRawUTF8 (), description.toRawUTF8 (), threadId);

        DynObj stoppedBody { new juce::DynamicObject () };
        stoppedBody->setProperty ("reason",            "exception");
        stoppedBody->setProperty ("text",              exceptionName);
        stoppedBody->setProperty ("description",       description);
        stoppedBody->setProperty ("threadId",          threadId);
        stoppedBody->setProperty ("allThreadsStopped", true);
        sendEvent (dap::makeEvent ("stopped", juce::var (stoppedBody)));

        DynObj outputBody { new juce::DynamicObject () };
        outputBody->setProperty ("category", "stderr");
        outputBody->setProperty ("output",   "Unhandled exception: " + exceptionName + " " + description + "\n");
        sendEvent (dap::makeEvent ("output", juce::var (outputBody)));

        resetVariablesState ();
    }

    // Process exit
    if (state.hasProcessExited)
    {
        logWrite ("[Whatdbg] emitting exited (code=%d) + terminated events\n", state.processExitCode);
        state.hasProcessExited = false;

        DynObj exitBody { new juce::DynamicObject () };
        exitBody->setProperty ("exitCode", state.processExitCode);
        sendEvent (dap::makeEvent ("exited", juce::var (exitBody)));

        sendEvent (dap::makeEvent ("terminated"));

        state.executionState = debug::ExecutionState::exited;
        isRunning = false;
    }
}

void Whatdbg::resolveAndResumeAfterInitialBreak ()
{
    if (breakpointManager.hasPending ())
    {
        juce::ignoreUnused (session.forceReloadAllSymbols ());
        juce::Array<juce::var> events { breakpointManager.onModuleLoad () };

        for (const auto& event : events)
        {
            sendEvent (event);
        }

        if (not events.isEmpty ())
        {
            logWrite ("[Whatdbg] resolved %d pending BPs at initial break\n", events.size ());
        }
    }

    session.resume ();
    state.executionState = debug::ExecutionState::running;
    state.initialBreakPhase = debug::InitialBreakPhase::resolved;

    DynObj threadBody { new juce::DynamicObject () };
    threadBody->setProperty ("reason",   "started");
    threadBody->setProperty ("threadId", 1);
    sendEvent (dap::makeEvent ("thread", juce::var (threadBody)));

    logWrite ("[Whatdbg] resumed after initial break\n");
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

void Whatdbg::resetVariablesState () noexcept
{
    nextVariablesRef = 1;
    variablesRefMap.clear ();
    nextFrameId = 1;
    frameIdMap.clear ();
    lastScopesThreadId = 0;
    session.resetSymbolGroupCache ();
}
