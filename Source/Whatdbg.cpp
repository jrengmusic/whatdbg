/** @file Whatdbg.cpp
 *  @brief DAP request handlers, deferred-event drains, and the main loop of the adapter.
 *
 *  Holds the constructor that fills the command and stop dispatch tables, run(), the
 *  onXxx handlers for each DAP request, the drains that processDeferredEvents runs
 *  for each stop reason, the DAP event builders, and writeMessage. All stdout writes
 *  happen on the main thread.
 */

#include "Whatdbg.h"

#if JUCE_WINDOWS
#include <fcntl.h>
#include <io.h>
#endif

static constexpr std::uint32_t pollTimeoutMs { 50 };
static constexpr int idleSleepMs { 10 };
static constexpr int maxStackFrames { 50 };

using dap::DynObj;

Whatdbg::Whatdbg ()
    : breakpointManager { session }
{
    commands.add ("initialize",        [this] (const juce::String&, const juce::var& message) { onInitialize (message); });
    commands.add ("launch",            [this] (const juce::String&, const juce::var& message) { onLaunch (message); });
    commands.add ("attach",            [this] (const juce::String&, const juce::var& message) { onAttach (message); });
    commands.add ("configurationDone", [this] (const juce::String&, const juce::var& message) { onConfigurationDone (message); });
    commands.add ("disconnect",        [this] (const juce::String&, const juce::var& message) { onDisconnect (message, false); });
    commands.add ("terminate",         [this] (const juce::String&, const juce::var& message) { onDisconnect (message, true); });
    commands.add ("setBreakpoints",    [this] (const juce::String&, const juce::var& message) { onSetBreakpoints (message); });
    commands.add ("threads",           [this] (const juce::String&, const juce::var& message) { onThreads (message); });
    commands.add ("stackTrace",        [this] (const juce::String&, const juce::var& message) { onStackTrace (message); });
    commands.add ("scopes",            [this] (const juce::String&, const juce::var& message) { onScopes (message); });
    commands.add ("variables",         [this] (const juce::String&, const juce::var& message) { onVariables (message); });
    commands.add ("continue",          [this] (const juce::String&, const juce::var& message) { onContinue (message); });
    commands.add ("next",              [this] (const juce::String&, const juce::var& message) { onNext (message); });
    commands.add ("stepIn",            [this] (const juce::String&, const juce::var& message) { onStepIn (message); });
    commands.add ("stepOut",           [this] (const juce::String&, const juce::var& message) { onStepOut (message); });
    commands.add ("pause",             [this] (const juce::String&, const juce::var& message) { onPause (message); });
    commands.add ("evaluate",          [this] (const juce::String&, const juce::var& message) { onEvaluate (message); });
    commands.add ("exceptionInfo",     [this] (const juce::String&, const juce::var& message) { onExceptionInfo (message); });

    stops.add (debug::StopReason::initialBreak, [this] (const debug::StopReason&) { drainInitialBreak (); });
    stops.add (debug::StopReason::moduleLoad,   [this] (const debug::StopReason&) { drainModuleLoaded (); });
    stops.add (debug::StopReason::breakpoint,   [this] (const debug::StopReason&) { drainBreakpointHit (); });
    stops.add (debug::StopReason::step,         [this] (const debug::StopReason&) { drainStepCompleted (); });
    stops.add (debug::StopReason::pause,        [this] (const debug::StopReason&) { drainPauseCompleted (); });
    stops.add (debug::StopReason::exception,    [this] (const debug::StopReason&) { drainExceptionStopped (); });
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

        if (state.executionState == debug::ExecutionState::running
            or state.executionState == debug::ExecutionState::launching)
        {
            session.pollEvents (pollTimeoutMs);
        }
        else
        {
            juce::Thread::sleep (idleSleepMs);
        }

        // 2. Process deferred events
        processDeferredEvents ();
    }

    session.shutdown (getEndModeForExit ());
}

void Whatdbg::onCommand (const juce::var& message)
{
    const juce::String type { message["type"].toString () };

    if (type.compare ("request") == 0)
    {
        const juce::String command { message["command"].toString () };
        const int seq { static_cast<int> (message["seq"]) };

#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] command=" + command + " seq=" + juce::String (seq));
#endif

        const auto commandEntry { commands.find (command) };

        if (commandEntry != commands.end ())
        {
            const auto& [commandName, commandCallback] { *commandEntry };
            commandCallback (commandName, message);
        }
        else
        {
            writeMessage (dap::getErrorResponse (seq, command, "Unknown command"));
        }
    }
}

void Whatdbg::drainInitialBreak ()
{
    if (state.isConfigurationDone)
    {
        state.stopReason = debug::StopReason::none;
        resumeAfterInitialBreak ();
    }
}

void Whatdbg::drainBreakpointHit ()
{
    state.stopReason = debug::StopReason::none;

    writeMessage (breakpointManager.getBreakpointStoppedEvent (state.breakpointEngineId, session.getEventThreadSystemId ()));
    resetVariablesState ();
}

void Whatdbg::drainStepCompleted ()
{
    state.stopReason = debug::StopReason::none;

    writeMessage (getStoppedEvent ("step"));
    resetVariablesState ();
    writeMessages (breakpointManager.onReload ());
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] step completed, emitted stopped event");
#endif
}

void Whatdbg::drainPauseCompleted ()
{
    state.stopReason = debug::StopReason::none;

    writeMessage (getStoppedEvent ("pause"));
    resetVariablesState ();
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] pause completed, emitted stopped event");
#endif
}

void Whatdbg::resumeExecution ()
{
    session.resume ();
    state.executionState = debug::ExecutionState::running;
}

void Whatdbg::writeMessages (const juce::Array<juce::var>& messages)
{
    for (const auto& message : messages)
    {
        writeMessage (message);
    }
}

void Whatdbg::drainModuleLoaded ()
{
    state.stopReason = debug::StopReason::none;

    writeMessages (breakpointManager.onModuleLoad (state.lastLoadedImageName));
    session.resume ();
}

void Whatdbg::drainBreakpointLocationResolved ()
{
    // Breakpoint location resolved asynchronously by liblldb (target loaded
    // a new module that resolved a previously-pending BP location).
    if (state.hasBreakpointLocationsResolved)
    {
        state.hasBreakpointLocationsResolved = false;

        writeMessages (breakpointManager.onBreakpointLocationFound (state.resolvedBreakpointEngineId,
                                                                    static_cast<std::uint16_t> (state.resolvedBreakpointLine)));
    }
}

void Whatdbg::drainDebuggeeOutput ()
{
    if (state.hasDebuggeeOutput)
    {
        state.hasDebuggeeOutput = false;

        DynObj body { new juce::DynamicObject () };
        body->setProperty ("category", state.debuggeeOutputCategory);
        body->setProperty ("output",   state.debuggeeOutputText);

        writeMessage (dap::getEvent ("output", juce::var (body)));
        state.debuggeeOutputText.clear ();
    }
}

void Whatdbg::drainExceptionStopped ()
{
    state.stopReason = debug::StopReason::none;

    const juce::String exceptionName { debug::getExceptionName (state.exceptionCode, state.isMachException) };
    const juce::String description   { getExceptionDescription () };
    const int threadId { static_cast<int> (session.getEventThreadSystemId ()) };

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] exception stopped: " + exceptionName + " " + description
                             + " (threadId=" + juce::String (threadId) + ")");
#endif

    writeMessage (getExceptionStoppedEvent (exceptionName, description, threadId));
    writeMessage (getExceptionOutputEvent (exceptionName, description));
    resetVariablesState ();
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
    const auto stop { stops.find (state.stopReason) };

    if (stop != stops.end ())
    {
        const auto& [stopReason, drain] { *stop };
        drain (stopReason);
    }

    drainBreakpointLocationResolved ();
    drainDebuggeeOutput ();
    drainProcessExited ();
    drainTerminateTimeout ();
}

void Whatdbg::resumeAfterInitialBreak ()
{
    writeMessages (breakpointManager.onReload ());

    const bool isLaunchSession { state.targetProgram.isNotEmpty () };

    if (isLaunchSession)
        writeMessage (getProcessEvent (state.targetProgram, state.targetProcessId, "launch"));

    resumeExecution ();

    DynObj threadBody { new juce::DynamicObject () };
    threadBody->setProperty ("reason",   "started");
    threadBody->setProperty ("threadId", static_cast<int> (session.getEventThreadSystemId ()));
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
        state.targetProgram = program;
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
    const std::uint32_t pid { static_cast<std::uint32_t> (static_cast<juce::int64> (args["pid"])) };

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] attach: pid=" + juce::String (pid));
#endif

    const bool isAttached { session.attach (pid) };

    if (isAttached)
    {
        addSearchPaths (dap::getString (args, "cwd"));

        state.targetProcessId = pid;

#if JUCE_WINDOWS
        state.executionState = debug::ExecutionState::launching;
#else
        state.executionState = debug::ExecutionState::stopped;
        state.stopReason     = debug::StopReason::initialBreak;
#endif

        writeMessage (dap::getResponse (seq, "attach", true));
        writeMessage (getProcessEvent ("pid " + juce::String (pid), pid, "attach"));
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

    writeMessage (dap::getResponse (seq, "configurationDone", true));
}

void Whatdbg::onDisconnect (const juce::var& request, bool isTerminate)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::String command { request["command"].toString () };
    const juce::var& args { request["arguments"] };

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

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("breakpoints", juce::var (breakpointManager.onSetBreakpoints (sourcePath, requestedBps)));
    writeMessage (dap::getResponse (seq, "setBreakpoints", true, juce::var (body)));
    writeMessages (breakpointManager.onReload ());
}

void Whatdbg::onThreads (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("threads", juce::var (session.getThreads ()));
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

    juce::Array<juce::var> frames (session.getStackTrace (maxStackFrames, state.nextFrameId));

    for (int frameIndex { 0 }; frameIndex < frames.size (); ++frameIndex)
    {
        state.frameIdMap.insert_or_assign (state.nextFrameId + frameIndex,
            std::pair<std::uint32_t, int> { static_cast<std::uint32_t> (threadId), frameIndex });
    }

    state.nextFrameId += frames.size ();

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("totalFrames", frames.size ());
    body->setProperty ("stackFrames", juce::var (std::move (frames)));
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
    body->setProperty ("scopes", juce::var (std::move (scopes)));
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

        juce::Array<juce::var> rawVars (symbolIndex < 0
            ? session.getLocals (frameIndex)
            : session.getVariableChildren (frameIndex, symbolIndex));

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

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("variables", juce::var (getDapVariables (ref)));
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

    const juce::String exceptionId { debug::getExceptionName (state.exceptionCode, state.isMachException) };

    DynObj body { new juce::DynamicObject () };
    body->setProperty ("exceptionId", exceptionId);
    body->setProperty ("description", getExceptionDescription ());
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

juce::var Whatdbg::getStoppedEvent (const juce::String& reason)
{
    DynObj body { new juce::DynamicObject () };
    body->setProperty ("reason",            reason);
    body->setProperty ("threadId",          static_cast<int> (session.getEventThreadSystemId ()));
    body->setProperty ("allThreadsStopped", true);

    return dap::getEvent ("stopped", juce::var (body));
}

juce::String Whatdbg::getExceptionDescription () const
{
    const juce::String codeHex    { juce::String::toHexString (static_cast<juce::int64> (state.exceptionCode)) };
    const juce::String addressHex { juce::String::toHexString (static_cast<juce::int64> (state.exceptionAddress)) };

    return juce::String ("0x") + codeHex + " at 0x" + addressHex;
}

juce::var Whatdbg::getExceptionStoppedEvent (const juce::String& exceptionName,
                                             const juce::String& description,
                                             int threadId) const
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
                                            const juce::String& description) const
{
    DynObj outputBody { new juce::DynamicObject () };
    outputBody->setProperty ("category", "stderr");
    outputBody->setProperty ("output",   "Unhandled exception: " + exceptionName + " " + description + "\n");

    return dap::getEvent ("output", juce::var (outputBody));
}

juce::var Whatdbg::getProcessEvent (const juce::String& name,
                                    std::uint32_t processId,
                                    const juce::String& startMethod) const
{
    DynObj body { new juce::DynamicObject () };
    body->setProperty ("name",            name);
    body->setProperty ("systemProcessId", static_cast<int> (processId));
    body->setProperty ("isLocalProcess",  true);
    body->setProperty ("startMethod",     startMethod);

    return dap::getEvent ("process", juce::var (body));
}
