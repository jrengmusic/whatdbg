/** @file Whatdbg.cpp
 *  @brief Core DAP adapter — main loop, event dispatch, and state management.
 *
 *  Owns the stdin Reader thread, the debug Session, and the DAP dispatch table.
 *  The main loop (run()) polls for DAP requests from stdin and debug events from
 *  the Session, dispatching each to the appropriate handler. All stdout writes
 *  (DAP responses and events) happen exclusively on this thread.
 */

#include "Whatdbg.h"

#if JUCE_WINDOWS
#include <fcntl.h>
#include <io.h>
#endif

static constexpr std::uint32_t pollTimeoutMs { 50 };
static constexpr int idleSleepMs { 10 };

using dap::DynObj;

Whatdbg::Whatdbg ()
    : breakpointManager { session }
{
    commands = {
        { "initialize",        [this] (const juce::var& m) { onInitialize (m); } },
        { "launch",            [this] (const juce::var& m) { onLaunch (m); } },
        { "attach",            [this] (const juce::var& m) { onAttach (m); } },
        { "configurationDone", [this] (const juce::var& m) { onConfigurationDone (m); } },
        { "disconnect",        [this] (const juce::var& m) { onDisconnect (m); } },
        { "terminate",         [this] (const juce::var& m) { onDisconnect (m); } },
        { "setBreakpoints",    [this] (const juce::var& m) { onSetBreakpoints (m); } },
        { "threads",           [this] (const juce::var& m) { onThreads (m); } },
        { "stackTrace",        [this] (const juce::var& m) { onStackTrace (m); } },
        { "scopes",            [this] (const juce::var& m) { onScopes (m); } },
        { "variables",         [this] (const juce::var& m) { onVariables (m); } },
        { "continue",          [this] (const juce::var& m) { onContinue (m); } },
        { "next",              [this] (const juce::var& m) { onNext (m); } },
        { "stepIn",            [this] (const juce::var& m) { onStepIn (m); } },
        { "stepOut",           [this] (const juce::var& m) { onStepOut (m); } },
        { "pause",             [this] (const juce::var& m) { onPause (m); } },
        { "evaluate",          [this] (const juce::var& m) { onEvaluate (m); } },
        { "exceptionInfo",     [this] (const juce::var& m) { onExceptionInfo (m); } },
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
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] main loop started");
#endif

    while (state.isRunning)
    {
        // 1. Drain FIFO — process all pending DAP commands
        for (juce::var message { reader.tryPop () }; not message.isVoid (); message = reader.tryPop ())
            onCommand (message);

        // 2. Poll dbgeng events (skip when target stopped or idle)
        if (state.executionState == debug::ExecutionState::running
            or state.executionState == debug::ExecutionState::launching)
        {
            juce::ignoreUnused (session.pollEvents (pollTimeoutMs));
        }

        // 3. Process deferred events
        processDeferredEvents ();

        // 4. Yield when idle (no target)
        if (state.executionState == debug::ExecutionState::idle)
        {
            juce::Thread::sleep (idleSleepMs);
        }
    }

    session.shutdown (getEndModeForExit ());
}

void Whatdbg::onCommand (const juce::var& message)
{
    const juce::String type { message["type"].toString () };

    if (type == "request")
    {
        const juce::String command { message["command"].toString () };
        const int seq { static_cast<int> (message["seq"]) };

#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] command=" + command + " seq=" + juce::String (seq));
#endif

        const auto commandEntry { commands.find (command.toStdString ()) };

        if (commandEntry != commands.end ())
        {
            commandEntry->second (message);
        }
        else
        {
            writeMessage (dap::getErrorResponse (seq, command, "Unknown command"));
        }
    }
}

void Whatdbg::drainInitialBreak ()
{
    // Initial breakpoint: resume if configurationDone already received
    if (state.initialBreakPhase == debug::InitialBreakPhase::pending
        and state.executionState == debug::ExecutionState::stopped
        and state.isConfigurationDone
        and not state.hasBreakpointHit
        and not state.hasStepCompleted)
    {
        resumeAfterInitialBreak ();
    }
}

void Whatdbg::emitBreakpointStoppedEvent ()
{
    juce::var stoppedBody { breakpointManager.onBreakpointHit (
        state.breakpointEngineId,
        session.getEventThreadSystemId ()) };

    writeMessage (dap::getEvent ("stopped", stoppedBody));
    resetVariablesState ();
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] breakpoint hit, emitted stopped event");
#endif
}

bool Whatdbg::drainBreakpointHit ()
{
    bool emittedStoppedEvent { false };

    if (state.hasBreakpointHit)
    {
        state.hasBreakpointHit = false;

        if (state.isStepPending and not breakpointManager.isUserBreakpoint (state.breakpointEngineId))
        {
            // Internal breakpoint from stepOut ("gu") — treat as step completion
            state.isStepPending = false;
            state.hasStepCompleted = true;
#if JUCE_DEBUG
            jam::debug::Log::write ("[Whatdbg] stepOut completed (internal BP)");
#endif
        }
        else
        {
            emitBreakpointStoppedEvent ();
            emittedStoppedEvent = true;
        }
    }

    return emittedStoppedEvent;
}

bool Whatdbg::drainStepCompleted ()
{
    bool emittedStoppedEvent { false };

    if (state.hasStepCompleted)
    {
        state.hasStepCompleted = false;

        DynObj body { new juce::DynamicObject () };
        body->setProperty ("reason",            "step");
        body->setProperty ("threadId",          static_cast<int> (session.getEventThreadSystemId ()));
        body->setProperty ("allThreadsStopped", true);

        writeMessage (dap::getEvent ("stopped", juce::var (body)));
        resetVariablesState ();
        emittedStoppedEvent = true;
#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] step completed, emitted stopped event");
#endif
    }

    return emittedStoppedEvent;
}

void Whatdbg::drainPauseCompleted ()
{
    if (state.hasPauseCompleted)
    {
        state.hasPauseCompleted = false;

        DynObj body { new juce::DynamicObject () };
        body->setProperty ("reason",            "pause");
        body->setProperty ("threadId",          static_cast<int> (session.getEventThreadSystemId ()));
        body->setProperty ("allThreadsStopped", true);

        writeMessage (dap::getEvent ("stopped", juce::var (body)));
        resetVariablesState ();
#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] pause completed, emitted stopped event");
#endif
    }
}

void Whatdbg::resumeExecution ()
{
    session.resume ();
    state.executionState = debug::ExecutionState::running;
}

void Whatdbg::emitResolvedBreakpointEvents ()
{
    juce::Array<juce::var> events { breakpointManager.onModuleLoad () };

    for (const auto& event : events)
    {
        writeMessage (event);
    }

#if JUCE_DEBUG
    if (not events.isEmpty ())
        jam::debug::Log::write ("[Whatdbg] resolved", events.size (), "pending breakpoints");
#endif
}

void Whatdbg::drainModuleLoaded ()
{
    if (state.hasNewModuleLoaded)
    {
        state.hasNewModuleLoaded = false;
        const bool wasStopped { state.executionState == debug::ExecutionState::stopped };

        if (state.hasPendingBreakpoints)
        {
            juce::ignoreUnused (session.loadModuleSymbols (state.lastLoadedImageName));
            emitResolvedBreakpointEvents ();
        }

        // Windows: dbgeng's LoadModule callback returns DEBUG_STATUS_BREAK, so state
        // was stopped and we must re-resume. macOS: eBroadcastBitModulesLoaded fires
        // from an already-running target — no pause ever occurred, skip the resume.
        if (wasStopped)
        {
            resumeExecution ();
        }
    }
}

juce::var Whatdbg::getBreakpointChangedEvent (int dapId, std::uint32_t resolvedLine)
{
    DynObj bpObj { new juce::DynamicObject () };
    bpObj->setProperty ("id",       dapId);
    bpObj->setProperty ("verified", true);
    bpObj->setProperty ("line",     static_cast<int> (resolvedLine));

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("reason",     "changed");
    body->setProperty ("breakpoint", juce::var (bpObj));

    return dap::getEvent ("breakpoint", juce::var (body));
}

void Whatdbg::drainBreakpointLocationResolved ()
{
    // Breakpoint location resolved asynchronously by liblldb (target loaded
    // a new module that resolved a previously-pending BP location).
    if (state.hasBreakpointLocationsResolved)
    {
        state.hasBreakpointLocationsResolved = false;

        const std::int32_t  engineId     { state.resolvedBreakpointEngineId };
        const std::uint32_t resolvedLine { state.resolvedBreakpointLine };

        const int dapId { breakpointManager.onBreakpointLocationFound (engineId,
                                                                        static_cast<std::uint16_t> (resolvedLine)) };

        if (dapId > 0)
        {
            writeMessage (getBreakpointChangedEvent (dapId, resolvedLine));

#if JUCE_DEBUG
            jam::debug::Log::write ("[Whatdbg] BP resolved async: engineId=" + juce::String (engineId)
                                     + " dapId=" + juce::String (dapId) + " line=" + juce::String (resolvedLine));
#endif
        }
    }
}

void Whatdbg::drainDebuggeeOutput ()
{
    if (state.hasDebuggeeOutput)
    {
        state.hasDebuggeeOutput = false;
        const juce::String text { state.debuggeeOutputText };
        const juce::String category { state.debuggeeOutputCategory };
        state.debuggeeOutputText.clear ();

        DynObj body { new juce::DynamicObject () };
        body->setProperty ("category", category);
        body->setProperty ("output",   text);

        writeMessage (dap::getEvent ("output", juce::var (body)));
    }
}

juce::var Whatdbg::getExceptionStoppedEvent (const juce::String& exceptionName,
                                              const juce::String& description,
                                              int threadId)
{
    DynObj stoppedBody { new juce::DynamicObject () };
    stoppedBody->setProperty ("reason",            "exception");
    stoppedBody->setProperty ("text",              exceptionName);
    stoppedBody->setProperty ("description",       description);
    stoppedBody->setProperty ("threadId",          threadId);
    stoppedBody->setProperty ("allThreadsStopped", true);

    return dap::getEvent ("stopped", juce::var (stoppedBody));
}

juce::var Whatdbg::getExceptionOutputEvent (const juce::String& exceptionName,
                                             const juce::String& description)
{
    DynObj outputBody { new juce::DynamicObject () };
    outputBody->setProperty ("category", "stderr");
    outputBody->setProperty ("output",   "Unhandled exception: " + exceptionName + " " + description + "\n");

    return dap::getEvent ("output", juce::var (outputBody));
}

void Whatdbg::drainExceptionStopped ()
{
    if (state.hasExceptionStopped)
    {
        state.hasExceptionStopped = false;

        const juce::String addressHex    { juce::String::toHexString (static_cast<juce::int64> (state.exceptionAddress)) };
        const juce::String exceptionName { debug::getExceptionName (state.exceptionCode, state.isMachException) };
        const juce::String codeHex       { juce::String::toHexString (static_cast<juce::int64> (state.exceptionCode)) };
        const juce::String description   { juce::String ("0x") + codeHex + " at 0x" + addressHex };
        const int threadId { static_cast<int> (session.getEventThreadSystemId ()) };

#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] exception stopped: " + exceptionName + " " + description
                                 + " (threadId=" + juce::String (threadId) + ")");
#endif

        writeMessage (getExceptionStoppedEvent (exceptionName, description, threadId));
        writeMessage (getExceptionOutputEvent (exceptionName, description));
        resetVariablesState ();
    }
}

void Whatdbg::drainProcessExited ()
{
    // Process exit
    if (state.hasProcessExited)
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] emitting exited (code=" + juce::String (state.processExitCode)
                                 + ") + terminated events");
#endif
        state.hasProcessExited = false;

        DynObj exitBody { new juce::DynamicObject () };
        exitBody->setProperty ("exitCode", state.processExitCode);
        writeMessage (dap::getEvent ("exited", juce::var (exitBody)));

        writeMessage (dap::getEvent ("terminated"));

        state.executionState = debug::ExecutionState::exited;
        state.isRunning = false;
        state.terminateDeadlineMs = 0;
    }
}

void Whatdbg::drainTerminateTimeout ()
{
    if (state.terminateDeadlineMs != 0
        and juce::Time::getMillisecondCounter () >= state.terminateDeadlineMs)
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] terminate timed out after "
                                 + juce::String (terminateTimeoutMs) + "ms, forcing exit");
#endif
        state.terminateDeadlineMs = 0;
        state.executionState = debug::ExecutionState::exited;
        state.isRunning = false;
    }
}

void Whatdbg::processDeferredEvents ()
{
    drainInitialBreak ();

    // A real breakpoint hit and a step completion are mutually exclusive for a
    // single stop — drainBreakpointHit converts an internal stepOut breakpoint into
    // a step completion internally, so drainStepCompleted must still run in that
    // case, but must not run again once a real breakpoint stop was emitted.
    if (not drainBreakpointHit ())
        drainStepCompleted ();

    drainPauseCompleted ();
    drainModuleLoaded ();
    drainBreakpointLocationResolved ();
    drainDebuggeeOutput ();
    drainExceptionStopped ();
    drainProcessExited ();
    drainTerminateTimeout ();
}

void Whatdbg::resumeAfterInitialBreak ()
{
    if (state.hasPendingBreakpoints)
    {
        juce::ignoreUnused (session.forceReloadAllSymbols ());
        emitResolvedBreakpointEvents ();
    }

    resumeExecution ();
    state.initialBreakPhase = debug::InitialBreakPhase::resolved;

    DynObj threadBody { new juce::DynamicObject () };
    threadBody->setProperty ("reason",   "started");
    threadBody->setProperty ("threadId", 1);
    writeMessage (dap::getEvent ("thread", juce::var (threadBody)));

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] resumed after initial break");
#endif
}

debug::EndMode Whatdbg::getEndModeForExit () const noexcept
{
    if (state.executionState == debug::ExecutionState::exited) return debug::EndMode::passive;
    if (state.shouldTerminateOnExit) return debug::EndMode::terminate;

    return debug::EndMode::detach;
}

void Whatdbg::writeMessage (const juce::var& message) noexcept
{
    const juce::String jsonBody { juce::JSON::toString (message, true) };
    const juce::CharPointer_UTF8 utf8 { jsonBody.toUTF8 () };
    const size_t byteCount { jsonBody.getNumBytesAsUTF8 () };

    const std::string header { "Content-Length: " + std::to_string (byteCount) + "\r\n\r\n" };

    std::cout.write (header.data (), static_cast<std::streamsize> (header.size ()));
    std::cout.write (utf8.getAddress (), static_cast<std::streamsize> (byteCount));
    std::cout.flush ();
}

void Whatdbg::resetVariablesState () noexcept
{
    state.nextVariablesRef = 1;
    state.variablesRefMap.clear ();
    state.nextFrameId = 1;
    state.frameIdMap.clear ();
    state.lastScopesThreadId = 0;
    session.resetSymbolGroupCache ();
}
