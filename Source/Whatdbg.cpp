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

Whatdbg::Whatdbg ()
    : breakpointManager { session }
{
    commands.add (Id::initialize.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onInitialize (document); });
    commands.add (Id::launch.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onLaunch (document); });
    commands.add (Id::attach.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onAttach (document); });
    commands.add (Id::configurationDone.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onConfigurationDone (document); });
    commands.add (Id::disconnect.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onDisconnect (document, false); });
    commands.add (Id::terminate.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onDisconnect (document, true); });
    commands.add (Id::setBreakpoints.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onSetBreakpoints (document); });
    commands.add (Id::threads.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onThreads (document); });
    commands.add (Id::stackTrace.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onStackTrace (document); });
    commands.add (Id::scopes.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onScopes (document); });
    commands.add (Id::variables.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onVariables (document); });
    commands.add (Id::tokenContinue.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onContinue (document); });
    commands.add (Id::next.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onNext (document); });
    commands.add (Id::stepIn.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onStepIn (document); });
    commands.add (Id::stepOut.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onStepOut (document); });
    commands.add (Id::pause.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onPause (document); });
    commands.add (Id::evaluate.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onEvaluate (document); });
    commands.add (Id::exceptionInfo.toString (), [this] (const juce::String&, const jam::JsonDocument& document) { onExceptionInfo (document); });

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
        for (auto message { reader.tryPop () }; message.has_value (); message = reader.tryPop ())
            onCommand (*message);

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

int Whatdbg::getRequestSeq (const jam::JsonDocument& request)
{
    return jam::Format::getNumber<int> (request.getView (*request.find (*request.getRoot ()->firstChild, jam::Strings::getView (Id::seq.toString ()))));
}

const jam::Document::Element& Whatdbg::getArguments (const jam::JsonDocument& request)
{
    const auto& message { *request.getRoot ()->firstChild };

    return *request.getChildByToken (message, *request.find (message, jam::Strings::getView (Id::arguments.toString ())));
}

juce::String Whatdbg::getText (const jam::JsonDocument& request, const jam::Document::Element& object, const juce::Identifier& key)
{
    const auto* value { request.find (object, jam::Strings::getView (key.toString ())) };

    return value != nullptr ? jam::Strings::fromView (request.getString (*value)) : juce::String {};
}

void Whatdbg::appendLocalsScope (std::string& json, int localsRef)
{
    jam::JsonWriter::appendObject (json, [localsRef] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::name);
        jam::JsonWriter::appendString (members, jam::Strings::getView (Id::locals.toString ()));
        jam::JsonWriter::appendKey (members, Id::variablesReference);
        jam::Format::appendDecimal (members, localsRef);
        jam::JsonWriter::appendKey (members, Id::expensive);
        jam::JsonWriter::appendBoolean (members, false);
    });
}

void Whatdbg::onCommand (const jam::JsonDocument& document)
{
    const auto& message { *document.getRoot ()->firstChild };
    const juce::String type { getText (document, message, Id::type) };

    if (type.compare (Id::request.toString ()) == 0)
    {
        const juce::String command { getText (document, message, Id::command) };
        const int seq { getRequestSeq (document) };

#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] command=" + command + " seq=" + juce::String (seq));
#endif

        const auto commandEntry { commands.find (command) };

        if (commandEntry != commands.end ())
        {
            const auto& [commandName, commandCallback] { *commandEntry };
            commandCallback (commandName, document);
        }
        else
        {
            std::string json;

            dap::appendErrorResponse (json, seq, jam::Strings::getView (command), "Unknown command");
            writeMessage (json);
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

    std::string json;

    breakpointManager.appendBreakpointStoppedEvent (json, state.breakpointEngineId, session.getEventThreadSystemId ());
    writeMessage (json);
    resetVariablesState ();
}

void Whatdbg::drainStepCompleted ()
{
    state.stopReason = debug::StopReason::none;

    writeMessage (getStoppedEvent (jam::Strings::getView (Id::step.toString ())));
    resetVariablesState ();
    writeMessages (breakpointManager.onReload ());
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] step completed, emitted stopped event");
#endif
}

void Whatdbg::drainPauseCompleted ()
{
    state.stopReason = debug::StopReason::none;

    writeMessage (getStoppedEvent (jam::Strings::getView (Id::pause.toString ())));
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

void Whatdbg::writeMessages (const jam::Strings& messages)
{
    for (int index { 0 }; index < messages.size (); ++index)
    {
        writeMessage (messages.getView (index));
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

        std::string json;

        dap::appendEvent (json, jam::Strings::getView (Id::output.toString ()), [this] (std::string& body)
        {
            jam::JsonWriter::appendKey (body, Id::category);
            jam::JsonWriter::appendString (body, jam::Strings::getView (state.debuggeeOutputCategory));
            jam::JsonWriter::appendKey (body, Id::output);
            jam::JsonWriter::appendString (body, jam::Strings::getView (state.debuggeeOutputText));
        });

        writeMessage (json);
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

        std::string json;

        dap::appendEvent (json, jam::Strings::getView (Id::exited.toString ()), [this] (std::string& body)
        {
            jam::JsonWriter::appendKey (body, Id::exitCode);
            jam::Format::appendDecimal (body, static_cast<int> (state.processExitCode));
        });

        writeMessage (json);

        json.clear ();
        dap::appendEvent (json, jam::Strings::getView (Id::terminated.toString ()));
        writeMessage (json);

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
        writeMessage (getProcessEvent (state.targetProgram, state.targetProcessId, jam::Strings::getView (Id::launch.toString ())));

    resumeExecution ();

    std::string json;

    dap::appendEvent (json, jam::Strings::getView (Id::thread.toString ()), [this] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::reason);
        jam::JsonWriter::appendString (body, jam::Strings::getView (Id::started.toString ()));
        jam::JsonWriter::appendKey (body, Id::threadId);
        jam::Format::appendDecimal (body, static_cast<int> (session.getEventThreadSystemId ()));
    });

    writeMessage (json);

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

void Whatdbg::writeMessage (std::string_view json) noexcept
{
    const std::string header { "Content-Length: " + std::to_string (json.size ()) + "\r\n\r\n" };

    std::cout.write (header.data (), static_cast<std::streamsize> (header.size ()));
    std::cout.write (json.data (), static_cast<std::streamsize> (json.size ()));
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

void Whatdbg::onInitialize (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::initialize.toString ()), true, &dap::appendCapabilities);
    writeMessage (json);

    json.clear ();
    dap::appendEvent (json, jam::Strings::getView (Id::initialized.toString ()));
    writeMessage (json);
}

void Whatdbg::onLaunch (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const juce::String program { getText (document, arguments, Id::program) };
    std::string json;

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] launch: " + program);
#endif

    const bool isLaunched { session.launch (program) };

    if (isLaunched)
    {
        addSearchPaths (getText (document, arguments, Id::cwd));

        state.executionState = debug::ExecutionState::launching;
        state.targetProgram = program;
        dap::appendResponse (json, seq, jam::Strings::getView (Id::launch.toString ()), true);
    }
    else
    {
        dap::appendErrorResponse (json, seq, jam::Strings::getView (Id::launch.toString ()), "Failed to launch process");
    }

    writeMessage (json);
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

void Whatdbg::setAttachedState (std::uint32_t pid)
{
    state.targetProcessId = pid;

#if JUCE_WINDOWS
    state.executionState = debug::ExecutionState::launching;
#else
    state.executionState = debug::ExecutionState::stopped;
    state.stopReason     = debug::StopReason::initialBreak;
#endif
}

void Whatdbg::onAttach (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const std::uint32_t pid { getNumber<std::uint32_t> (document, arguments, Id::pid) };
    std::string json;

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] attach: pid=" + juce::String (pid));
#endif

    const bool isAttached { session.attach (pid) };

    if (isAttached)
    {
        addSearchPaths (getText (document, arguments, Id::cwd));

        setAttachedState (pid);

        dap::appendResponse (json, seq, jam::Strings::getView (Id::attach.toString ()), true);
        writeMessage (json);
        writeMessage (getProcessEvent ("pid " + juce::String (pid), pid, jam::Strings::getView (Id::attach.toString ())));
    }
    else
    {
        dap::appendErrorResponse (json, seq, jam::Strings::getView (Id::attach.toString ()), "Failed to attach");
        writeMessage (json);
    }
}

void Whatdbg::onConfigurationDone (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    state.isConfigurationDone = true;

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::configurationDone.toString ()), true);
    writeMessage (json);
}

void Whatdbg::onDisconnect (const jam::JsonDocument& document, bool isTerminate)
{
    const auto& request { *document.getRoot ()->firstChild };
    const int seq { getRequestSeq (document) };
    const juce::String command { getText (document, request, Id::command) };
    const auto* argumentsValue { document.find (request, jam::Strings::getView (Id::arguments.toString ())) };
    const auto* terminateValue { argumentsValue != nullptr ? document.find (*document.getChildByToken (request, *argumentsValue), jam::Strings::getView (Id::terminateDebuggee.toString ())) : nullptr };

    state.shouldTerminateOnExit = isTerminate or (terminateValue != nullptr and document.getBoolean (*terminateValue));

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (command), true);
    writeMessage (json);

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

void Whatdbg::onSetBreakpoints (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const auto& source { *document.getChildByToken (arguments, *document.find (arguments, jam::Strings::getView (Id::source.toString ()))) };
    const juce::String sourcePath { getText (document, source, Id::path) };
    const auto* breakpointsValue { document.find (arguments, jam::Strings::getView (Id::breakpoints.toString ())) };
    const auto* requestedBreakpoints { breakpointsValue != nullptr ? document.getChildByToken (arguments, *breakpointsValue) : nullptr };

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] setBreakpoints: " + sourcePath + " ("
                             + juce::String (requestedBreakpoints != nullptr ? requestedBreakpoints->get<jam::Document::Tokens> (Id::tokens)->size () : 0)
                             + " breakpoints)");
#endif

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::setBreakpoints.toString ()), true, [this, &sourcePath, &document, requestedBreakpoints] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::breakpoints);
        jam::JsonWriter::appendArray (body, [this, &sourcePath, &document, requestedBreakpoints] (std::string& elements)
        {
            breakpointManager.onSetBreakpoints (elements, sourcePath, document, requestedBreakpoints);
        });
    });

    writeMessage (json);
    writeMessages (breakpointManager.onReload ());
}

void Whatdbg::onThreads (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::threads.toString ()), true, [this] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::threads);
        jam::JsonWriter::appendArray (body, [this] (std::string& elements) { session.appendThreads (elements); });
    });

    writeMessage (json);
}

void Whatdbg::addFrameIds (std::uint32_t threadId, int frameCount)
{
    for (int frameIndex { 0 }; frameIndex < frameCount; ++frameIndex)
    {
        state.frameIdMap.insert_or_assign (state.nextFrameId + frameIndex,
            std::pair<std::uint32_t, int> { threadId, frameIndex });
    }

    state.nextFrameId += frameCount;
}

void Whatdbg::onStackTrace (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const int threadId { getNumber<int> (document, arguments, Id::threadId) };

    if (threadId > 0)
    {
        session.setCurrentThreadBySystemId (static_cast<std::uint32_t> (threadId));
    }

    int frameCount { 0 };
    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::stackTrace.toString ()), true, [this, &frameCount] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::stackFrames);
        jam::JsonWriter::appendArray (body, [this, &frameCount] (std::string& elements)
        {
            frameCount = session.appendStackFrames (elements, maxStackFrames, state.nextFrameId);
        });
        jam::JsonWriter::appendKey (body, Id::totalFrames);
        jam::Format::appendDecimal (body, frameCount);
    });

    addFrameIds (static_cast<std::uint32_t> (threadId), frameCount);
    writeMessage (json);
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

void Whatdbg::onScopes (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const int frameId { getNumber<int> (document, arguments, Id::frameId) };

    const auto [threadSystemId, frameIndex] { selectFrameFromId (frameId) };

    if (threadSystemId > 0)
    {
        state.lastScopesThreadId = threadSystemId;
    }

    const int localsRef { state.nextVariablesRef++ };
    state.variablesRefMap.insert_or_assign (localsRef, std::pair<int, int> { frameIndex, -1 });

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::scopes.toString ()), true, [localsRef] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::scopes);
        jam::JsonWriter::appendArray (body, [localsRef] (std::string& elements) { appendLocalsScope (elements, localsRef); });
    });

    writeMessage (json);
}

void Whatdbg::appendVariables (std::string& json, int ref)
{
    const auto variablesEntry { state.variablesRefMap.find (ref) };

    if (variablesEntry != state.variablesRefMap.end ())
    {
        const auto& [variablesKey, variablesLocation] { *variablesEntry };
        const auto& [frameIndex, symbolIndex] { variablesLocation };

        const auto appendLocal = [this, &json, variableFrameIndex = frameIndex] (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int childSymbolIndex)
        {
            appendVariable (json, name, value, type, hasChildren, childSymbolIndex, variableFrameIndex);
        };

        if (symbolIndex < 0)
            session.applyToLocals (frameIndex, appendLocal);
        else
            session.applyToVariableChildren (frameIndex, symbolIndex, appendLocal);
    }
}

void Whatdbg::onVariables (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const int ref { getNumber<int> (document, arguments, Id::variablesReference) };

    if (state.lastScopesThreadId > 0)
    {
        session.setCurrentThreadBySystemId (state.lastScopesThreadId);
    }

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::variables.toString ()), true, [this, ref] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::variables);
        jam::JsonWriter::appendArray (body, [this, ref] (std::string& elements) { appendVariables (elements, ref); });
    });

    writeMessage (json);
}

void Whatdbg::appendVariable (std::string& json, const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex, int frameIndex)
{
    int childRef { 0 };

    if (hasChildren)
    {
        childRef = state.nextVariablesRef++;
        state.variablesRefMap.insert_or_assign (childRef, std::pair<int, int> { frameIndex, symbolIndex });
    }

    jam::JsonWriter::appendSeparator (json);
    jam::JsonWriter::appendObject (json, [&name, &value, &type, childRef] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::name);
        jam::JsonWriter::appendString (members, jam::Strings::getView (name));
        jam::JsonWriter::appendKey (members, Id::value);
        jam::JsonWriter::appendString (members, jam::Strings::getView (value));
        jam::JsonWriter::appendKey (members, Id::type);
        jam::JsonWriter::appendString (members, jam::Strings::getView (type));
        jam::JsonWriter::appendKey (members, Id::variablesReference);
        jam::Format::appendDecimal (members, childRef);
    });
}

void Whatdbg::onContinue (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    resumeExecution ();
    state.isStepPending = false;
    state.isPausePending = false;

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::tokenContinue.toString ()), true, [] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::allThreadsContinued);
        jam::JsonWriter::appendBoolean (body, true);
    });

    writeMessage (json);
}

void Whatdbg::onNext (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    session.stepOver ();
    state.executionState = debug::ExecutionState::running;
    state.isStepPending = true;
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] next issued");
#endif

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::next.toString ()), true);
    writeMessage (json);
}

void Whatdbg::onStepIn (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    session.stepInto ();
    state.executionState = debug::ExecutionState::running;
    state.isStepPending = true;
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] stepIn issued");
#endif

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::stepIn.toString ()), true);
    writeMessage (json);
}

void Whatdbg::onStepOut (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    session.stepOut ();
    state.executionState = debug::ExecutionState::running;
    state.isStepPending = true;
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] stepOut issued");
#endif

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::stepOut.toString ()), true);
    writeMessage (json);
}

void Whatdbg::onPause (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    session.interrupt (state.targetProcessId);
    state.executionState = debug::ExecutionState::running;
    state.isPausePending = true;

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::pause.toString ()), true);
    writeMessage (json);
}

void Whatdbg::onExceptionInfo (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    const juce::String exceptionId { debug::getExceptionName (state.exceptionCode, state.isMachException) };
    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::exceptionInfo.toString ()), true, [this, &exceptionId] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::exceptionId);
        jam::JsonWriter::appendString (body, jam::Strings::getView (exceptionId));
        jam::JsonWriter::appendKey (body, Id::description);
        jam::JsonWriter::appendString (body, jam::Strings::getView (getExceptionDescription ()));
        jam::JsonWriter::appendKey (body, Id::breakMode);
        jam::JsonWriter::appendString (body, jam::Strings::getView (Id::unhandled.toString ()));
    });

    writeMessage (json);
}

void Whatdbg::onEvaluate (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const juce::String expression { getText (document, arguments, Id::expression) };
    const int frameId { document.contains (arguments, jam::Strings::getView (Id::frameId.toString ())) ? getNumber<int> (document, arguments, Id::frameId) : 0 };

    const auto [threadSystemId, frameIndex] { selectFrameFromId (frameId) };

    const juce::String result { session.evaluateExpression (expression, frameIndex) };
    std::string json;

    if (result.isNotEmpty ())
    {
        dap::appendResponse (json, seq, jam::Strings::getView (Id::evaluate.toString ()), true, [&result] (std::string& body)
        {
            jam::JsonWriter::appendKey (body, Id::result);
            jam::JsonWriter::appendString (body, jam::Strings::getView (result));
            jam::JsonWriter::appendKey (body, Id::variablesReference);
            jam::Format::appendDecimal (body, 0);
        });
    }
    else
    {
        dap::appendErrorResponse (json, seq, jam::Strings::getView (Id::evaluate.toString ()),
            jam::Strings::getView ("Could not evaluate: " + expression));
    }

    writeMessage (json);
}

std::string Whatdbg::getStoppedEvent (std::string_view reason)
{
    std::string json;

    dap::appendEvent (json, jam::Strings::getView (Id::stopped.toString ()), [this, reason] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::reason);
        jam::JsonWriter::appendString (body, reason);
        jam::JsonWriter::appendKey (body, Id::threadId);
        jam::Format::appendDecimal (body, static_cast<int> (session.getEventThreadSystemId ()));
        jam::JsonWriter::appendKey (body, Id::allThreadsStopped);
        jam::JsonWriter::appendBoolean (body, true);
    });

    return json;
}

juce::String Whatdbg::getExceptionDescription () const
{
    const juce::String codeHex    { juce::String::toHexString (static_cast<juce::int64> (state.exceptionCode)) };
    const juce::String addressHex { juce::String::toHexString (static_cast<juce::int64> (state.exceptionAddress)) };

    return juce::String ("0x") + codeHex + " at 0x" + addressHex;
}

std::string Whatdbg::getExceptionStoppedEvent (const juce::String& exceptionName,
                                               const juce::String& description,
                                               int threadId) const
{
    std::string json;

    dap::appendEvent (json, jam::Strings::getView (Id::stopped.toString ()), [&exceptionName, &description, threadId] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::reason);
        jam::JsonWriter::appendString (body, jam::Strings::getView (Id::exception.toString ()));
        jam::JsonWriter::appendKey (body, Id::text);
        jam::JsonWriter::appendString (body, jam::Strings::getView (exceptionName));
        jam::JsonWriter::appendKey (body, Id::description);
        jam::JsonWriter::appendString (body, jam::Strings::getView (description));
        jam::JsonWriter::appendKey (body, Id::threadId);
        jam::Format::appendDecimal (body, threadId);
        jam::JsonWriter::appendKey (body, Id::allThreadsStopped);
        jam::JsonWriter::appendBoolean (body, true);
    });

    return json;
}

std::string Whatdbg::getExceptionOutputEvent (const juce::String& exceptionName,
                                              const juce::String& description) const
{
    std::string json;

    dap::appendEvent (json, jam::Strings::getView (Id::output.toString ()), [&exceptionName, &description] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::category);
        jam::JsonWriter::appendString (body, jam::Strings::getView (Id::tokenStderr.toString ()));
        jam::JsonWriter::appendKey (body, Id::output);
        jam::JsonWriter::appendString (body, jam::Strings::getView ("Unhandled exception: " + exceptionName + " " + description + "\n"));
    });

    return json;
}

std::string Whatdbg::getProcessEvent (const juce::String& name,
                                      std::uint32_t processId,
                                      std::string_view startMethod) const
{
    std::string json;

    dap::appendEvent (json, jam::Strings::getView (Id::process.toString ()), [&name, processId, startMethod] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::name);
        jam::JsonWriter::appendString (body, jam::Strings::getView (name));
        jam::JsonWriter::appendKey (body, Id::systemProcessId);
        jam::Format::appendDecimal (body, static_cast<int> (processId));
        jam::JsonWriter::appendKey (body, Id::isLocalProcess);
        jam::JsonWriter::appendBoolean (body, true);
        jam::JsonWriter::appendKey (body, Id::startMethod);
        jam::JsonWriter::appendString (body, startMethod);
    });

    return json;
}
