/** @file Processor.cpp
 *  @brief Engine-context side of the adapter: the command table, the request handlers, the engine event handlers, and the poll loop.
 *
 *  Every function here runs on the serial context of jam::AsyncUpdater. process() drains
 *  the request lane, polls the engine while the target runs or launches or a terminate
 *  deadline is pending, and then asks StopManager for the stop messages.
 */

#include "Processor.h"

Processor::Processor ()
{
    commands.add (Id::initialize, [this] (const juce::String&, const jam::JsonDocument& document) { onInitialize (document); });
    commands.add (Id::launch, [this] (const juce::String&, const jam::JsonDocument& document) { onLaunch (document); });
    commands.add (Id::runInTerminal, [this] (const juce::String&, const jam::JsonDocument& document) { onRunInTerminal (document); });
    commands.add (Id::attach, [this] (const juce::String&, const jam::JsonDocument& document) { onAttach (document); });
    commands.add (Id::configurationDone, [this] (const juce::String&, const jam::JsonDocument& document) { onConfigurationDone (document); });
    commands.add (Id::disconnect, [this] (const juce::String&, const jam::JsonDocument& document) { onDisconnect (document, false); });
    commands.add (Id::terminate, [this] (const juce::String&, const jam::JsonDocument& document) { onDisconnect (document, true); });
    commands.add (Id::setBreakpoints, [this] (const juce::String&, const jam::JsonDocument& document) { onSetBreakpoints (document); });
    commands.add (Id::threads, [this] (const juce::String&, const jam::JsonDocument& document) { onThreads (document); });
    commands.add (Id::stackTrace, [this] (const juce::String&, const jam::JsonDocument& document) { onStackTrace (document); });
    commands.add (Id::scopes, [this] (const juce::String&, const jam::JsonDocument& document) { onScopes (document); });
    commands.add (Id::variables, [this] (const juce::String&, const jam::JsonDocument& document) { onVariables (document); });
    commands.add (Id::continueCommand, [this] (const juce::String&, const jam::JsonDocument& document) { onContinue (document); });
    commands.add (Id::next, [this] (const juce::String&, const jam::JsonDocument& document) { onNext (document); });
    commands.add (Id::stepIn, [this] (const juce::String&, const jam::JsonDocument& document) { onStepIn (document); });
    commands.add (Id::stepOut, [this] (const juce::String&, const jam::JsonDocument& document) { onStepOut (document); });
    commands.add (Id::pause, [this] (const juce::String&, const jam::JsonDocument& document) { onPause (document); });
    commands.add (Id::evaluate, [this] (const juce::String&, const jam::JsonDocument& document) { onEvaluate (document); });
    commands.add (Id::exceptionInfo, [this] (const juce::String&, const jam::JsonDocument& document) { onExceptionInfo (document); });
    commands.add (Id::prepare, [this] (const juce::String&, const jam::JsonDocument& document) { onPrepare (document); });
    commands.add (Id::releaseResources, [this] (const juce::String&, const jam::JsonDocument& document) { onReleaseResources (document); });

    cancellations.add (Id::disconnect, [this] (const juce::String&) { session.cancel (); });
    cancellations.add (Id::terminate, [this] (const juce::String&) { session.cancel (); });

#if JUCE_WINDOWS
    deferrals.add (Id::setBreakpoints);
#endif

    registerEvents ();
    registerLauncherEvents ();
    registerOutputEvents ();
}

Processor::~Processor ()
{
    releaseResources ();
}

void Processor::prepare (const juce::File& sidecarDirectory)
{
    requests.write (dap::getRequest (jam::Strings::getView (Id::prepare.toString ()), [&sidecarDirectory] (std::string& arguments)
    {
        jam::JsonWriter::appendKey (arguments, Id::path);
        jam::JsonWriter::appendString (arguments, jam::Strings::getView (sidecarDirectory.getFullPathName ()));
    }));

    handleUpdateNow ();
}

void Processor::addRequest (jam::JsonDocument request)
{
    const juce::String command { getText (request, *request.getRoot ()->firstChild, Id::command) };

    if (cancellations.contains (command))
        cancellations.get (command);

    requests.write (std::move (request));
    triggerAsyncUpdate ();
    session.wake ();
}

void Processor::releaseResources ()
{
    cancelPendingUpdate ();
    requests.write (dap::getRequest (jam::Strings::getView (Id::releaseResources.toString ())));
    handleUpdateNow ();
}

void Processor::handleAsyncUpdate ()
{
    process ();
}

void Processor::process ()
{
    requests.applyPendingData ([this] (jam::JsonDocument&& request) { onRequest (std::move (request)); });

    if (isPolling ())
    {
        session.pollEvents (pollTimeoutMs);
    }
    else
    {
        session.drainLauncherOutput ();
    }

    if (static_cast<debug::StopReason> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::stopReason)->getValue ()) != debug::StopReason::none)
    {
        applyPendingRequests ();
    }

    addMessages (stopManager.onStop (targetProgram));
    drainTerminateTimeout ();

    if (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunning)->getValue () != 0 and isPolling ())
    {
        triggerAsyncUpdate ();
    }
}

bool Processor::isPolling () const
{
    const auto executionState { static_cast<debug::ExecutionState> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::executionState)->getValue ()) };

    return executionState == debug::ExecutionState::running
           or executionState == debug::ExecutionState::launching
           or model.getParameter<jam::Parameter<int64_t>> (model.sessionTag, Id::terminateDeadlineMs)->getValue () != 0;
}

void Processor::onRequest (jam::JsonDocument&& request)
{
    const juce::String command { getText (request, *request.getRoot ()->firstChild, Id::command) };
    const auto executionState { static_cast<debug::ExecutionState> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::executionState)->getValue ()) };
    const bool isRunning { executionState == debug::ExecutionState::running };
    const bool isDeferred { deferrals.contains (juce::Identifier { command })
                            and (isRunning or executionState == debug::ExecutionState::launching) };

    if (isDeferred)
    {
        if (isRunning and model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isBreakInPending)->getValue () == 0)
        {
            model.storeValue (model.sessionTag, Id::isBreakInPending, 1);
            session.interrupt (static_cast<std::uint32_t> (model.getParameter<jam::Parameter<int64_t>> (model.sessionTag, Id::targetProcessId)->getValue ()));
        }

        pendingRequests.write (std::move (request));
    }
    else
    {
        onCommand (request);
    }
}

void Processor::applyPendingRequests ()
{
    pendingRequests.applyPendingData ([this] (jam::JsonDocument&& request) { onCommand (request); });
}

void Processor::registerEvents ()
{
    session.events.add<int> (Id::targetProcessId, [this] (const juce::Identifier&, int processId)
    {
        model.storeValue (model.sessionTag, Id::targetProcessId, static_cast<int64_t> (static_cast<std::uint32_t> (processId)));
    });

    session.events.add<int> (Id::exited, [this] (const juce::Identifier&, int exitCode)
    {
        session.drainOutput ();
        model.storeValue (model.sessionTag, Id::processExitCode, exitCode);
        drainProcessExited ();
    });

    session.events.add (Id::terminated, [this] (const juce::Identifier&)
    {
        drainProcessExited ();
    });

    session.events.add (Id::running, [this] (const juce::Identifier&)
    {
        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::running));
    });

    session.events.add<int, int> (Id::breakpointLocationFound, [this] (const juce::Identifier&, int engineId, int line)
    {
        addMessages (breakpointManager.onBreakpointLocationFound (engineId, static_cast<std::uint16_t> (line)));
    });
}

void Processor::registerLauncherEvents ()
{
    session.events.add (Id::runInTerminal, [this] (const juce::Identifier&)
    {
        triggerAsyncUpdate ();
    });

    session.events.add<int> (Id::didAttach, [this] (const juce::Identifier&, int processId)
    {
        std::string json;

        setAttachedState (static_cast<std::uint32_t> (processId));
        dap::appendResponse (json, model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::launchSequenceNumber)->getValue (), jam::Strings::getView (Id::launch.toString ()), true);
        addMessage (std::move (json));
    });

    session.events.add<const juce::String&> (Id::error, [this] (const juce::Identifier&, const juce::String& text)
    {
        addLaunchErrorResponse (text);
    });
}

void Processor::registerOutputEvents ()
{
    session.events.add<const juce::String&> (Id::console, [this] (const juce::Identifier& key, const juce::String& text)
    {
        addMessage (dap::getOutputEvent (jam::Strings::getView (key.toString ()), jam::Strings::getView (text)));
    });

    for (const auto& id : { Id::standardOutput, Id::standardError })
        session.events.add<std::string_view> (id, [this] (const juce::Identifier& key, std::string_view text)
        {
            addMessage (dap::getOutputEvent (jam::Strings::getView (key.toString ()), text));
        });
}

int Processor::getRequestSeq (const jam::JsonDocument& request)
{
    return jam::Format::getNumber<int> (request.getView (*request.find (*request.getRoot ()->firstChild, jam::Strings::getView (Id::seq.toString ()))));
}

const jam::Document::Element& Processor::getArguments (const jam::JsonDocument& request)
{
    const auto& message { *request.getRoot ()->firstChild };

    return *request.getChildByToken (message, *request.find (message, jam::Strings::getView (Id::arguments.toString ())));
}

juce::String Processor::getText (const jam::JsonDocument& request, const jam::Document::Element& object, const juce::Identifier& key)
{
    const auto* value { request.find (object, jam::Strings::getView (key.toString ())) };

    return value != nullptr ? jam::Strings::fromView (request.getString (*value)) : juce::String {};
}

void Processor::appendLocalsScope (std::string& json, int localsRef)
{
    jam::JsonWriter::appendObject (json, [localsRef] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::name);
        jam::JsonWriter::appendString (members, "Locals");
        jam::JsonWriter::appendKey (members, Id::variablesReference);
        jam::Format::appendDecimal (members, localsRef);
        jam::JsonWriter::appendKey (members, Id::expensive);
        jam::JsonWriter::appendBoolean (members, false);
    });
}

void Processor::onCommand (const jam::JsonDocument& document)
{
    const juce::String command { getText (document, *document.getRoot ()->firstChild, Id::command) };
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
        addMessage (std::move (json));
    }
}

void Processor::onRunInTerminal (const jam::JsonDocument& document)
{
    const auto& message { *document.getRoot ()->firstChild };
    const auto* isSuccess { document.find (message, jam::Strings::getView (Id::success.toString ())) };

    if (isSuccess != nullptr and not document.getBoolean (*isSuccess))
    {
        const juce::String text { "runInTerminal failed: " + getText (document, message, Id::message) };

#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] " + text);
#endif

        addLaunchErrorResponse (text);
    }
}

void Processor::addLaunchErrorResponse (const juce::String& text)
{
    std::string json;

    dap::appendErrorResponse (json, model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::launchSequenceNumber)->getValue (), jam::Strings::getView (Id::launch.toString ()), jam::Strings::getView (text));
    addMessage (std::move (json));
}

void Processor::addMessage (std::string json)
{
    messages.write (std::move (json));
    model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::numMessages)->setValue (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::numMessages)->getValue () + 1);
}

void Processor::addMessages (const jam::Strings& messages)
{
    for (int index { 0 }; index < messages.size (); ++index)
    {
        addMessage (std::string (messages.getView (index)));
    }
}

void Processor::drainProcessExited ()
{
    applyPendingRequests ();

    if (static_cast<debug::ExecutionState> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::executionState)->getValue ()) != debug::ExecutionState::exited)
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] emitting exited (code=" + juce::String (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::processExitCode)->getValue ())
                                 + ") + terminated events");
#endif

        std::string json;

        dap::appendEvent (json, jam::Strings::getView (Id::exited.toString ()), [this] (std::string& body)
        {
            jam::JsonWriter::appendKey (body, Id::exitCode);
            jam::Format::appendDecimal (body, static_cast<int> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::processExitCode)->getValue ()));
        });

        addMessage (std::move (json));

        json.clear ();
        dap::appendEvent (json, jam::Strings::getView (Id::terminated.toString ()));
        addMessage (std::move (json));

        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::exited));
        model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunning)->setValue (0);
        model.storeValue (model.sessionTag, Id::terminateDeadlineMs, int64_t { 0 });
    }
}

void Processor::drainTerminateTimeout ()
{
    if (model.getParameter<jam::Parameter<int64_t>> (model.sessionTag, Id::terminateDeadlineMs)->getValue () != 0
        and juce::Time::getMillisecondCounter () >= static_cast<std::uint32_t> (model.getParameter<jam::Parameter<int64_t>> (model.sessionTag, Id::terminateDeadlineMs)->getValue ()))
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] terminate timed out after "
                                 + juce::String (terminateTimeoutMs) + "ms, forcing exit");
#endif
        model.storeValue (model.sessionTag, Id::terminateDeadlineMs, int64_t { 0 });
        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::exited));
        model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunning)->setValue (0);
    }
}

debug::EndMode Processor::getEndModeForExit () const noexcept
{
    if (static_cast<debug::ExecutionState> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::executionState)->getValue ()) == debug::ExecutionState::exited) return debug::EndMode::passive;
    if (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::shouldTerminateOnExit)->getValue () != 0) return debug::EndMode::terminate;

    return debug::EndMode::detach;
}

void Processor::onPrepare (const jam::JsonDocument& document)
{
    const bool isInitialized { session.initialize (juce::File { getText (document, getArguments (document), Id::path) }) };

    if (not isInitialized)
    {
        model.storeValue (model.sessionTag, Id::adapterExitCode, 1);
        model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunning)->setValue (0);
    }
}

void Processor::onReleaseResources (const jam::JsonDocument&)
{
    session.shutdown (getEndModeForExit ());
    model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::idle));
    model.storeValue (model.sessionTag, Id::terminateDeadlineMs, int64_t { 0 });
}

void Processor::onInitialize (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto* supportsRunInTerminal { document.find (getArguments (document), jam::Strings::getView (Id::supportsRunInTerminalRequest.toString ())) };
    std::string json;

    model.storeValue (model.sessionTag, Id::isRunInTerminalSupported, supportsRunInTerminal != nullptr and document.getBoolean (*supportsRunInTerminal) ? 1 : 0);
    dap::appendResponse (json, seq, jam::Strings::getView (Id::initialize.toString ()), true, &dap::appendCapabilities);
    addMessage (std::move (json));

    json.clear ();
    dap::appendEvent (json, jam::Strings::getView (Id::initialized.toString ()));
    addMessage (std::move (json));
}

void Processor::onLaunch (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const juce::String program { getText (document, arguments, Id::program) };
    const juce::String console { getText (document, arguments, Id::console) };

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] launch: " + program);
#endif

    if (console.isNotEmpty () and juce::Identifier { console } == Id::internalConsole)
        launchInConsole (seq, program, getText (document, arguments, Id::cwd));
    else
        launchInTerminal (seq, program, console, getText (document, arguments, Id::cwd));
}

void Processor::launchInConsole (int seq, const juce::String& program, const juce::String& cwd)
{
    std::string json;
    const bool isLaunched { session.launch (program) };

    if (isLaunched)
    {
        addSearchPaths (cwd);

        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::launching));
        targetProgram = program;
        dap::appendResponse (json, seq, jam::Strings::getView (Id::launch.toString ()), true);
    }
    else
    {
        dap::appendErrorResponse (json, seq, jam::Strings::getView (Id::launch.toString ()), "Failed to launch process");
    }

    addMessage (std::move (json));
}

void Processor::launchInTerminal (int seq, const juce::String& program, const juce::String& console, const juce::String& cwd)
{
    std::string json;

    if (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunInTerminalSupported)->getValue () != 0)
    {
        addSearchPaths (cwd);
        targetProgram = program;
        model.storeValue (model.sessionTag, Id::launchSequenceNumber, seq);

        const juce::Identifier& type { console.isNotEmpty () and juce::Identifier { console } == Id::externalTerminal ? Id::external : Id::integrated };
        const juce::StringArray args { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName (),
                                       Id::commFileOption.toString (), session.createCommFile (), Id::launchTargetOption.toString (), program };

        dap::appendRequest (json, jam::Strings::getView (Id::runInTerminal.toString ()), [&cwd, &args, &type] (std::string& arguments) { dap::appendRunInTerminalArguments (arguments, type, cwd, args); });
    }
    else
    {
        dap::appendErrorResponse (json, seq, jam::Strings::getView (Id::launch.toString ()), "Client does not support runInTerminal; set console to internalConsole");
    }

    addMessage (std::move (json));
}

void Processor::addSearchPaths (const juce::String& cwd)
{
    if (cwd.isNotEmpty ())
    {
        session.appendSourcePath (cwd);
        session.appendSymbolPath (cwd);
    }
}

void Processor::setAttachedState (std::uint32_t pid)
{
    model.storeValue (model.sessionTag, Id::targetProcessId, static_cast<int64_t> (pid));

#if JUCE_WINDOWS
    model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::launching));
#else
    model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::stopped));
    model.storeValue (model.sessionTag, Id::stopReason, static_cast<int> (debug::StopReason::initialBreak));
#endif
}

void Processor::onAttach (const jam::JsonDocument& document)
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
        addMessage (std::move (json));
        addMessage (dap::getProcessEvent ("pid " + juce::String (pid), pid, jam::Strings::getView (Id::attach.toString ())));
    }
    else
    {
        dap::appendErrorResponse (json, seq, jam::Strings::getView (Id::attach.toString ()), "Failed to attach");
        addMessage (std::move (json));
    }
}

void Processor::onConfigurationDone (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    model.storeValue (model.sessionTag, Id::isConfigurationDone, 1);

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::configurationDone.toString ()), true);
    addMessage (std::move (json));
}

void Processor::onDisconnect (const jam::JsonDocument& document, bool isTerminate)
{
    session.resetCancel ();

    const auto& request { *document.getRoot ()->firstChild };
    const int seq { getRequestSeq (document) };
    const juce::String command { getText (document, request, Id::command) };
    const auto* argumentsValue { document.find (request, jam::Strings::getView (Id::arguments.toString ())) };
    const auto* terminateValue { argumentsValue != nullptr ? document.find (*document.getChildByToken (request, *argumentsValue), jam::Strings::getView (Id::terminateDebuggee.toString ())) : nullptr };

    model.storeValue (model.sessionTag, Id::shouldTerminateOnExit, static_cast<int> (isTerminate or (terminateValue != nullptr and document.getBoolean (*terminateValue))));

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (command), true);
    addMessage (std::move (json));

    const bool isDebuggeeAlive { static_cast<debug::ExecutionState> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::executionState)->getValue ()) != debug::ExecutionState::idle
                              and static_cast<debug::ExecutionState> (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::executionState)->getValue ()) != debug::ExecutionState::exited };

    if (model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::shouldTerminateOnExit)->getValue () != 0 and isDebuggeeAlive)
    {
        session.terminateDebuggee (static_cast<std::uint32_t> (model.getParameter<jam::Parameter<int64_t>> (model.sessionTag, Id::targetProcessId)->getValue ()));

        // terminateDebuggee only sends the kill request. The exit arrives later
        // through pollEvents, so process() keeps polling while terminateDeadlineMs
        // is set. terminateDeadlineMs ends that wait.
        model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::running));
        model.storeValue (model.sessionTag, Id::terminateDeadlineMs, static_cast<int64_t> (juce::Time::getMillisecondCounter () + terminateTimeoutMs));
    }
    else
    {
        model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunning)->setValue (0);
    }
}

void Processor::onSetBreakpoints (const jam::JsonDocument& document)
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

    addMessage (std::move (json));
    addMessages (breakpointManager.onReload ());
}

void Processor::onThreads (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::threads.toString ()), true, [this] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::threads);
        jam::JsonWriter::appendArray (body, [this] (std::string& elements) { session.appendThreads (elements); });
    });

    addMessage (std::move (json));
}

void Processor::onStackTrace (const jam::JsonDocument& document)
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

    dap::appendResponse (json, seq, jam::Strings::getView (Id::stackTrace.toString ()), true, [this, &frameCount, threadId] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::stackFrames);
        jam::JsonWriter::appendArray (body, [this, &frameCount, threadId] (std::string& elements)
        {
            frameCount = variableManager.onStackTrace (elements, static_cast<std::uint32_t> (threadId));
        });
        jam::JsonWriter::appendKey (body, Id::totalFrames);
        jam::Format::appendDecimal (body, frameCount);
    });

    addMessage (std::move (json));
}

void Processor::onScopes (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const int frameId { getNumber<int> (document, arguments, Id::frameId) };

    const int localsRef { variableManager.onScopes (frameId) };

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::scopes.toString ()), true, [localsRef] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::scopes);
        jam::JsonWriter::appendArray (body, [localsRef] (std::string& elements) { appendLocalsScope (elements, localsRef); });
    });

    addMessage (std::move (json));
}

void Processor::onVariables (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const int ref { getNumber<int> (document, arguments, Id::variablesReference) };

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::variables.toString ()), true, [this, ref] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::variables);
        jam::JsonWriter::appendArray (body, [this, ref] (std::string& elements) { variableManager.onVariables (elements, ref); });
    });

    addMessage (std::move (json));
}

void Processor::onContinue (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    session.resume ();
    model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::running));
    model.storeValue (model.sessionTag, Id::isStepPending, 0);
    model.storeValue (model.sessionTag, Id::isPausePending, 0);

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::continueCommand.toString ()), true, [] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::allThreadsContinued);
        jam::JsonWriter::appendBoolean (body, true);
    });

    addMessage (std::move (json));
}

void Processor::onNext (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    session.stepOver ();
    model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::running));
    model.storeValue (model.sessionTag, Id::isStepPending, 1);
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] next issued");
#endif

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::next.toString ()), true);
    addMessage (std::move (json));
}

void Processor::onStepIn (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    session.stepInto ();
    model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::running));
    model.storeValue (model.sessionTag, Id::isStepPending, 1);
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] stepIn issued");
#endif

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::stepIn.toString ()), true);
    addMessage (std::move (json));
}

void Processor::onStepOut (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    session.stepOut ();
    model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::running));
    model.storeValue (model.sessionTag, Id::isStepPending, 1);
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] stepOut issued");
#endif

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::stepOut.toString ()), true);
    addMessage (std::move (json));
}

void Processor::onPause (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    session.interrupt (static_cast<std::uint32_t> (model.getParameter<jam::Parameter<int64_t>> (model.sessionTag, Id::targetProcessId)->getValue ()));
    model.storeValue (model.sessionTag, Id::executionState, static_cast<int> (debug::ExecutionState::running));
    model.storeValue (model.sessionTag, Id::isPausePending, 1);

    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::pause.toString ()), true);
    addMessage (std::move (json));
}

void Processor::onExceptionInfo (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };

    const juce::String exceptionId { debug::getExceptionName (static_cast<std::uint32_t> (model.getParameter<jam::Parameter<int64_t>> (model.exceptionTag, Id::exceptionCode)->getValue ()), model.getParameter<jam::Parameter<int>> (model.exceptionTag, Id::isMachException)->getValue () != 0) };
    std::string json;

    dap::appendResponse (json, seq, jam::Strings::getView (Id::exceptionInfo.toString ()), true, [this, &exceptionId] (std::string& body)
    {
        jam::JsonWriter::appendKey (body, Id::exceptionId);
        jam::JsonWriter::appendString (body, jam::Strings::getView (exceptionId));
        jam::JsonWriter::appendKey (body, Id::description);
        jam::JsonWriter::appendString (body, jam::Strings::getView (dap::getExceptionDescription (static_cast<juce::int64> (model.getParameter<jam::Parameter<int64_t>> (model.exceptionTag, Id::exceptionCode)->getValue ()), static_cast<juce::int64> (model.getParameter<jam::Parameter<int64_t>> (model.exceptionTag, Id::exceptionAddress)->getValue ()))));
        jam::JsonWriter::appendKey (body, Id::breakMode);
        jam::JsonWriter::appendString (body, jam::Strings::getView (Id::unhandled.toString ()));
    });

    addMessage (std::move (json));
}

void Processor::onEvaluate (const jam::JsonDocument& document)
{
    const int seq { getRequestSeq (document) };
    const auto& arguments { getArguments (document) };
    const juce::String expression { getText (document, arguments, Id::expression) };
    const int frameId { getNumber<int> (document, arguments, Id::frameId) };
    const bool isHover { document.contains (arguments, jam::Strings::getView (Id::context.toString ()))
                         and juce::Identifier { getText (document, arguments, Id::context) } == Id::hover };

    const auto [threadSystemId, frameIndex] { variableManager.selectFrameFromId (frameId) };

    const juce::String result { session.evaluateExpression (expression, frameIndex, isHover) };
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

    addMessage (std::move (json));
}
