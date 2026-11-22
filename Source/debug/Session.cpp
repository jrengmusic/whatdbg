/** @file Session.cpp
 *  @brief Windows debug session implementation using Microsoft's dbgeng COM API.
 *
 *  Platform counterpart to Session_mac.cpp (macOS liblldb). Implements the whole
 *  Session interface from Session.h using IDebugClient5, IDebugControl4,
 *  IDebugSymbols3, IDebugDataSpaces4, and IDebugSystemObjects. The file holds the
 *  lifecycle (initialize, launch, attach, shutdown), event polling, stepping and
 *  interrupt, breakpoint resolution, thread queries, stack trace, locals, child
 *  variables, and expression evaluation, with file-local helpers for symbol formatting.
 *
 *  Threading model: the engine context runs every call except wake. The stdin callback of
 *  Processor and the output streams call wake. dbgeng
 *  invokes the callbacks in Callbacks.cpp during WaitForEvent, on the engine context.
 *  The callbacks fire events on Session::events.
 *
 *  Lifecycle: initialize (Loader → COM) → (launch | attach) → pollEvents loop → shutdown.
 */

#include <JuceHeader.h>
#include "Session.h"
#include "PrettyPrint.h"
#include "../dap/Types.h"

#if JUCE_WINDOWS
#include <dbghelp.h>
#include <tlhelp32.h>

namespace debug
{

static constexpr int outputPipeSize { 4096 };

Session::Session ()
{
    launcherBytes.prepare (outputPipeSize);
    standardOutputBytes.prepare (outputPipeSize);
    standardErrorBytes.prepare (outputPipeSize);
}

Session::~Session ()
{
    shutdown (EndMode::passive);
}

IDebugClient5* Session::getOrCreateDebugClient (const juce::File& sidecarDir) noexcept
{
    IDebugClient5* rawClient { nullptr };

    const bool isLoaderOk { loader.load (sidecarDir) };

    if (isLoaderOk)
    {
        rawClient = loader.createDebugClient ();
    }

    return rawClient;
}

bool Session::getOrCreateDebugInterfaces () noexcept
{
    const HRESULT qiControlResult { client.QueryInterface (control) };
    juce::ignoreUnused (qiControlResult);

    const HRESULT qiSymbolsResult { client.QueryInterface (symbols) };
    juce::ignoreUnused (qiSymbolsResult);

    const HRESULT qiDataResult { client.QueryInterface (dataSpaces) };
    juce::ignoreUnused (qiDataResult);

    const HRESULT qiSysObjResult { client.QueryInterface (systemObjects) };
    juce::ignoreUnused (qiSysObjResult);

    return control != nullptr
           and symbols != nullptr
           and dataSpaces != nullptr
           and systemObjects != nullptr;
}

void Session::setDebugInterfaces () noexcept
{
    static constexpr const char* symbolServerPath { "srv*" };

    const HRESULT cbResult { client->SetOutputCallbacks (
        static_cast<IDebugOutputCallbacks*> (&outputCallbacks)) };
    juce::ignoreUnused (cbResult);

    client->SetOutputMask (
        DEBUG_OUTPUT_NORMAL
        | DEBUG_OUTPUT_WARNING
        | DEBUG_OUTPUT_ERROR
        | DEBUG_OUTPUT_DEBUGGEE);

    symbols->AddSymbolOptions (SYMOPT_LOAD_LINES);
    control->SetCodeLevel (DEBUG_LEVEL_SOURCE);
    symbols->AppendSymbolPath (symbolServerPath);
    control->AddEngineOptions (DEBUG_ENGOPT_INITIAL_BREAK);
    client->SetEventCallbacks (&eventCallbacks);
}

void Session::drainOutput () noexcept
{
    while (standardOutputBytes.writeFromTerminalTTY (standardOutputPipe.get ()) > 0)
        standardOutputBytes.applyPendingData ([this] (const char* data, int size)
        {
            events.get (Id::standardOutput, std::string_view { data, static_cast<size_t> (size) });
            return size;
        });

    while (standardErrorBytes.writeFromTerminalTTY (standardErrorPipe.get ()) > 0)
        standardErrorBytes.applyPendingData ([this] (const char* data, int size)
        {
            events.get (Id::standardError, std::string_view { data, static_cast<size_t> (size) });
            return size;
        });
}

bool Session::initialize (const juce::File& sidecarDir) noexcept
{
    comInitialiser.emplace ();

    IDebugClient5* rawClient { getOrCreateDebugClient (sidecarDir) };

    if (rawClient != nullptr)
    {
        client = juce::ComSmartPtr<IDebugClient5> (rawClient, juce::IncrementRef::no);

        if (getOrCreateDebugInterfaces ())
            setDebugInterfaces ();
    }

    const bool isInitialized { client != nullptr
                                and control != nullptr
                                and symbols != nullptr
                                and dataSpaces != nullptr
                                and systemObjects != nullptr };

    if (not isInitialized)
    {
        shutdown (EndMode::passive);
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: initialization failed");
#endif
    }

    return isInitialized;
}

static juce::String getNativePath (const juce::String& path) noexcept
{
    return juce::File::getCurrentWorkingDirectory ().getChildFile (path).getFullPathName ();
}

static juce::String normalizeCommandLine (const juce::String& program) noexcept
{
    juce::String normalized { getNativePath (program) };

    if (normalized.containsChar (' ') and not normalized.startsWithChar ('"'))
        normalized = normalized.quoted ();

    return normalized;
}

static CONTEXT getThreadContext (std::uint32_t systemId) noexcept
{
    CONTEXT threadContext {};
    std::unique_ptr<void, decltype (&CloseHandle)> threadHandle {
        OpenThread (THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, static_cast<DWORD> (systemId)), &CloseHandle };

    if (threadHandle != nullptr)
    {
        threadContext.ContextFlags = CONTEXT_FULL;
        GetThreadContext (threadHandle.get (), &threadContext);
    }

    return threadContext;
}

static std::pair<std::vector<DEBUG_STACK_FRAME>, std::vector<CONTEXT>> getContextStackTrace (IDebugControl4& control, std::uint32_t systemId, int frameCount) noexcept
{
    CONTEXT threadContext { getThreadContext (systemId) };
    std::vector<DEBUG_STACK_FRAME> stackFrames (static_cast<size_t> (frameCount));
    std::vector<CONTEXT> frameContexts (static_cast<size_t> (frameCount));
    ULONG framesFilled { 0 };

    control.GetContextStackTrace (&threadContext, sizeof (CONTEXT),
                                  stackFrames.data (),
                                  static_cast<ULONG> (frameCount),
                                  frameContexts.data (),
                                  static_cast<ULONG> (frameCount * sizeof (CONTEXT)),
                                  sizeof (CONTEXT),
                                  &framesFilled);
    stackFrames.resize (framesFilled);
    frameContexts.resize (framesFilled);

    return { std::move (stackFrames), std::move (frameContexts) };
}

static juce::String getOutputPipeName (const jam::AsyncOutputStream& stream)
{
    return "\\\\.\\pipe\\whatdbg-" + juce::String (static_cast<juce::int64> (GetCurrentProcessId ()))
           + "-" + juce::String::toHexString (static_cast<juce::int64> (reinterpret_cast<std::uintptr_t> (&stream)));
}

static constexpr DWORD maxPipeInstances { 1 };
static constexpr DWORD defaultPipeTimeoutMs { 0 };

static HANDLE createPipe (const juce::String& name, DWORD openMode)
{
    return CreateNamedPipeW (name.toWideCharPointer (),
                             openMode,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                             maxPipeInstances,
                             static_cast<DWORD> (outputPipeSize),
                             static_cast<DWORD> (outputPipeSize),
                             defaultPipeTimeoutMs,
                             nullptr);
}

static juce::String getCommPipeName ()
{
    return "\\\\.\\pipe\\whatdbg-run-in-terminal-" + juce::String (static_cast<juce::int64> (GetCurrentProcessId ()));
}

static std::unique_ptr<void, decltype (&CloseHandle)> createPipeClient (const juce::String& name) noexcept
{
    SECURITY_ATTRIBUTES inheritableSecurity { sizeof (SECURITY_ATTRIBUTES), nullptr, TRUE };
    const HANDLE client { CreateFileW (name.toWideCharPointer (), GENERIC_WRITE, 0, &inheritableSecurity, OPEN_EXISTING, 0, nullptr) };
    jassert (client != INVALID_HANDLE_VALUE);

    return std::unique_ptr<void, decltype (&CloseHandle)> { client, &CloseHandle };
}

static auto getHandleAttributeList (std::array<HANDLE, 2>& inheritedHandles) noexcept
{
    SIZE_T attributeListSize { 0 };
    InitializeProcThreadAttributeList (nullptr, 1, 0, &attributeListSize);
    std::vector<char> attributeListBuffer (attributeListSize);
    auto* attributeList { reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST> (attributeListBuffer.data ()) };

    const BOOL isListInitialised { InitializeProcThreadAttributeList (attributeList, 1, 0, &attributeListSize) };
    const BOOL isListUpdated { UpdateProcThreadAttribute (attributeList,
                                                          0,
                                                          PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                          inheritedHandles.data (),
                                                          sizeof (inheritedHandles),
                                                          nullptr,
                                                          nullptr) };
    jassert (isListInitialised and isListUpdated);
    juce::ignoreUnused (isListInitialised, isListUpdated);

    auto deleteList = [ownedBuffer = std::move (attributeListBuffer)] (LPPROC_THREAD_ATTRIBUTE_LIST list) { DeleteProcThreadAttributeList (list); };

    return std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>, decltype (deleteList)> { attributeList, std::move (deleteList) };
}

static std::unique_ptr<void, decltype (&CloseHandle)> createSuspendedProcess (const juce::String& commandLine, HANDLE outputClient, HANDLE errorClient) noexcept
{
    std::array<HANDLE, 2> inheritedHandles { outputClient, errorClient };
    std::wstring commandLineBuffer { commandLine.toWideCharPointer () };
    const auto attributeList (getHandleAttributeList (inheritedHandles));

    STARTUPINFOEXW startupInfo {};
    startupInfo.StartupInfo.cb = sizeof (STARTUPINFOEXW);
    startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.StartupInfo.hStdOutput = inheritedHandles.at (0);
    startupInfo.StartupInfo.hStdError = inheritedHandles.at (1);
    startupInfo.lpAttributeList = attributeList.get ();

    PROCESS_INFORMATION processInformation {};
    const BOOL isCreated { CreateProcessW (nullptr, commandLineBuffer.data (), nullptr, nullptr, TRUE,
                                           CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                                           nullptr, nullptr, &startupInfo.StartupInfo, &processInformation) };

#if JUCE_DEBUG
    jam::debug::Log::write (isCreated ? "WHATDBG: CreateProcessW pid=" + juce::String (static_cast<unsigned long> (processInformation.dwProcessId))
                                      : "WHATDBG: CreateProcessW failed, error=" + juce::String (static_cast<unsigned long> (GetLastError ())));
#endif

    const std::unique_ptr<void, decltype (&CloseHandle)> threadHandle { isCreated ? processInformation.hThread : nullptr, &CloseHandle };
    return std::unique_ptr<void, decltype (&CloseHandle)> { isCreated ? processInformation.hProcess : nullptr, &CloseHandle };
}

HRESULT Session::launchCaptured (const juce::String& commandLine) noexcept
{
    static constexpr UINT failedAttachExitCode { 1 };

    const juce::String outputPipeName { getOutputPipeName (standardOutput) };
    const juce::String errorPipeName { getOutputPipeName (standardError) };

    standardOutput.onBytesReady = [this] (int) { wake (); };
    standardOutputPipe.reset (createPipe (outputPipeName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE));
    jassert (standardOutputPipe.get () != INVALID_HANDLE_VALUE);
    const auto outputClient (createPipeClient (outputPipeName));
    const bool isOutputOpen { standardOutput.open (standardOutputPipe.get (), nullptr, outputPipeSize, maximumPeriodMilliseconds) };
    jassert (isOutputOpen);
    juce::ignoreUnused (isOutputOpen);

    standardError.onBytesReady = [this] (int) { wake (); };
    standardErrorPipe.reset (createPipe (errorPipeName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE));
    jassert (standardErrorPipe.get () != INVALID_HANDLE_VALUE);
    const auto errorClient (createPipeClient (errorPipeName));
    const bool isErrorOpen { standardError.open (standardErrorPipe.get (), nullptr, outputPipeSize, maximumPeriodMilliseconds) };
    jassert (isErrorOpen);
    juce::ignoreUnused (isErrorOpen);

    const auto process (createSuspendedProcess (commandLine, outputClient.get (), errorClient.get ()));
    HRESULT result { E_FAIL };

    if (process != nullptr)
    {
        result = attachSuspended (GetProcessId (process.get ()));

        if (FAILED (result))
        {
            TerminateProcess (process.get (), failedAttachExitCode);
#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: AttachProcess failed, hr=0x" + juce::String::toHexString (static_cast<unsigned long> (result)));
#endif
        }
    }

    return result;
}

HRESULT Session::attachSuspended (std::uint32_t processId) noexcept
{
    return client->AttachProcess (0, processId, DEBUG_ATTACH_INVASIVE_RESUME_PROCESS | DEBUG_ATTACH_INVASIVE_NO_INITIAL_BREAK);
}

bool Session::launch (const juce::String& program) noexcept
{
    jassert (client != nullptr);

    const HRESULT result { launchCaptured (normalizeCommandLine (program)) };

#if JUCE_DEBUG
    if (SUCCEEDED (result))
        jam::debug::Log::write ("WHATDBG: launched process: " + program);
#endif

    return SUCCEEDED (result);
}

juce::String Session::createCommFile () noexcept
{
    const juce::String name { getCommPipeName () };
    const HANDLE pipe { createPipe (name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED) };
    jassert (pipe != INVALID_HANDLE_VALUE);
    HANDLE duplicate { nullptr };

    const BOOL isDuplicated { DuplicateHandle (GetCurrentProcess (), pipe, GetCurrentProcess (), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS) };
    jassert (isDuplicated);
    juce::ignoreUnused (isDuplicated);
    launcherInput.reset (duplicate);
    launcherPipe.reset (pipe);
    launcherOutput.onBytesReady = [this] (int) { events.get (Id::runInTerminal); };

    connectEvent.reset (CreateEventW (nullptr, TRUE, FALSE, nullptr));
    connectOverlapped = {};
    connectOverlapped.hEvent = connectEvent.get ();

    connectSource.onReady = [this] (int)
    {
        const bool isOpen { launcherOutput.open (launcherPipe.get (), nullptr, outputPipeSize, maximumPeriodMilliseconds) };
        jassert (isOpen);
        juce::ignoreUnused (isOpen);
    };

    const bool isSourceOpen { connectEvent != nullptr and connectSource.open (nullptr, connectEvent.get ()) };
    jassert (isSourceOpen);
    juce::ignoreUnused (isSourceOpen);

    const BOOL isConnected { ConnectNamedPipe (launcherPipe.get (), &connectOverlapped) };
    jassert (isConnected == FALSE and GetLastError () == ERROR_IO_PENDING);
    juce::ignoreUnused (isConnected);

    connectSource.arm ();

    return name;
}

void Session::sendDidAttach () noexcept
{
    std::string line;

    jam::JsonWriter::appendObject (line, [] (std::string& members)
    {
        jam::JsonWriter::appendKey (members, Id::kind);
        jam::JsonWriter::appendString (members, jam::Strings::getView (Id::didAttach.toString ()));
    });
    line += '\n';

    OVERLAPPED overlapped {};
    DWORD numBytes { 0 };
    BOOL isWritten { WriteFile (launcherInput.get (), line.data (), static_cast<DWORD> (line.size ()), nullptr, &overlapped) };

    if (not isWritten and GetLastError () == ERROR_IO_PENDING)
        isWritten = GetOverlappedResult (launcherInput.get (), &overlapped, &numBytes, TRUE);

    jassert (isWritten);
    juce::ignoreUnused (isWritten);
}

void Session::attachLaunchTarget (std::uint32_t processId) noexcept
{
    if (SUCCEEDED (attachSuspended (processId)))
    {
        sendDidAttach ();
        events.get (Id::didAttach, static_cast<int> (processId));
    }
    else
    {
        events.get (Id::error, juce::String { "Failed to attach to the target process" });
    }
}

void Session::drainLauncherOutput () noexcept
{
    if (launcherPipe != nullptr)
    {
        launcherBytes.writeFromTerminalTTY (launcherPipe.get ());

        launcherBytes.applyPendingData ([this] (const char* data, int size)
        {
            const std::string_view view { data, static_cast<size_t> (size) };

            if (view.find ('\n') == std::string_view::npos) return 0;

            const auto document { jam::JsonDocument::read (view.substr (0, view.find ('\n'))) };
            jassert (document != nullptr);

            const auto& message { *document->getRoot ()->firstChild };
            const juce::Identifier type { jam::Strings::fromView (document->getString (document->get (message, jam::Strings::getView (Id::kind.toString ())))) };

            if (type == Id::pid)
            {
                attachLaunchTarget (jam::Format::getNumber<std::uint32_t> (document->getView (document->get (message, jam::Strings::getView (Id::pid.toString ())))));
            }
            else if (type == Id::error)
            {
                events.get (Id::error, jam::Strings::fromView (document->getString (document->get (message, jam::Strings::getView (Id::error.toString ())))));
            }

            connectSource.close ();
            launcherOutput.close ();
            launcherInput.reset ();
            launcherPipe.reset ();
            connectEvent.reset ();

            return size;
        });
    }
}

bool Session::attach (std::uint32_t processId) noexcept
{
    jassert (client != nullptr);

    const HRESULT attachResult { client->AttachProcess (0, static_cast<ULONG> (processId), 0) };
    const bool attached { SUCCEEDED (attachResult) };

    if (attached)
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: attached to process " + juce::String (static_cast<unsigned long> (processId)));
#endif
    }
    else
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: AttachProcess failed, hr=0x"
                                 + juce::String::toHexString (static_cast<unsigned long> (attachResult)));
#endif
    }

    return attached;
}

std::optional<std::int32_t> Session::getBreakpointAtInstruction () noexcept
{
    DEBUG_STACK_FRAME frame {};
    ULONG framesFilled { 0 };
    ULONG breakpointCount { 0 };

    control->GetStackTrace (0, 0, 0, &frame, 1, &framesFilled);
    control->GetNumberBreakpoints (&breakpointCount);

    if (framesFilled == 1)
    {
        for (ULONG i { 0 }; i < breakpointCount; ++i)
        {
            IDebugBreakpoint2* breakpoint { nullptr };
            control->GetBreakpointByIndex2 (i, &breakpoint);

            jassert (breakpoint != nullptr);

            if (breakpoint != nullptr)
            {
                ULONG64 offset { 0 };
                ULONG flags { 0 };
                ULONG engineId { 0 };

                breakpoint->GetOffset (&offset);
                breakpoint->GetFlags (&flags);
                breakpoint->GetId (&engineId);

                if (offset == frame.InstructionOffset and (flags bitand DEBUG_BREAKPOINT_ENABLED) != 0)
                    return static_cast<std::int32_t> (engineId);
            }
        }
    }

    return std::nullopt;
}

void Session::applySteppedBreakpoint () noexcept
{
    IDebugBreakpoint2* breakpoint { nullptr };
    control->GetBreakpointById2 (static_cast<ULONG> (*steppedBreakpointEngineId), &breakpoint);

    jassert (breakpoint != nullptr);

    if (breakpoint != nullptr)
        breakpoint->AddFlags (DEBUG_BREAKPOINT_ENABLED);

    steppedBreakpointEngineId = std::nullopt;

    auto* model { WHATDBGModel::getInstance () };

    if (model->getParameter<jam::Parameter<int>> (model->sessionTag, Id::stopReason)->getValue () == static_cast<int> (debug::StopReason::none)
        and model->getParameter<jam::Parameter<int>> (model->sessionTag, Id::isStepPending)->getValue () == 0)
        control->SetExecutionStatus (DEBUG_STATUS_GO);
}

void Session::disableBreakpointAtInstruction () noexcept
{
    ULONG eventEngineId { 0 };
    systemObjects->GetEventThread (&eventEngineId);
    systemObjects->SetCurrentThreadId (eventEngineId);
    symbols->ResetScope ();

    const auto engineId { getBreakpointAtInstruction () };

    if (engineId.has_value ())
    {
        IDebugBreakpoint2* breakpoint { nullptr };
        control->GetBreakpointById2 (static_cast<ULONG> (*engineId), &breakpoint);

        jassert (breakpoint != nullptr);

        if (breakpoint != nullptr)
        {
            breakpoint->RemoveFlags (DEBUG_BREAKPOINT_ENABLED);
            steppedBreakpointEngineId = engineId;
        }
    }
}

void Session::resume () noexcept
{
    disableBreakpointAtInstruction ();
    control->SetExecutionStatus (steppedBreakpointEngineId.has_value () ? DEBUG_STATUS_STEP_INTO : DEBUG_STATUS_GO);
}

void Session::pollEvents (std::uint32_t timeoutMs) noexcept
{
    auto* model { WHATDBGModel::getInstance () };
    const bool isTerminating { model->getParameter<jam::Parameter<int64_t>> (model->sessionTag, Id::terminateDeadlineMs)->getValue () != 0 };

    drainOutput ();

    HRESULT hr { E_PENDING };

    isWaiting.store (true);

    if (not isWakePending.exchange (false))
        hr = control->WaitForEvent (0, isTerminating ? static_cast<ULONG> (timeoutMs) : INFINITE);

    isWaiting.store (false);

    control->GetInterrupt ();

    if (hr == S_OK and steppedBreakpointEngineId.has_value ())
        applySteppedBreakpoint ();

    drainOutput ();

    if (hr == E_UNEXPECTED)
    {
        events.get (Id::terminated);
    }
    else if (hr == S_OK and model->getParameter<jam::Parameter<int>> (model->sessionTag, Id::isStepPending)->getValue () != 0)
    {
        events.get (Id::step);

#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: step completed");
#endif
    }
}

void Session::shutdown (EndMode mode) noexcept
{
    standardOutput.close ();
    standardError.close ();
    connectSource.close ();
    launcherOutput.close ();

    if (launcherPipe != nullptr and not HasOverlappedIoCompleted (&connectOverlapped))
    {
        DWORD numBytes { 0 };
        CancelIoEx (launcherPipe.get (), &connectOverlapped);
        GetOverlappedResult (launcherPipe.get (), &connectOverlapped, &numBytes, TRUE);
    }

    standardOutputPipe.reset ();
    standardErrorPipe.reset ();
    launcherInput.reset ();
    launcherPipe.reset ();
    connectEvent.reset ();

    if (client != nullptr)
    {
        ULONG endFlag { DEBUG_END_PASSIVE };

        if (mode == EndMode::terminate)
            endFlag = DEBUG_END_ACTIVE_TERMINATE;
        else if (mode == EndMode::detach)
            endFlag = DEBUG_END_ACTIVE_DETACH;

        client->EndSession (endFlag);
        client->SetEventCallbacks (nullptr);
        client->SetOutputCallbacks (nullptr);
    }

    resetSymbolGroupCache ();
    systemObjects = nullptr;
    dataSpaces = nullptr;
    symbols = nullptr;
    control = nullptr;
    client = nullptr;
    comInitialiser.reset ();
}

void Session::wake () noexcept
{
    isWakePending.store (true);

    if (isWaiting.load ())
        control->SetInterrupt (DEBUG_INTERRUPT_EXIT);
}

void Session::cancel () noexcept
{
    control->SetInterrupt (DEBUG_INTERRUPT_EXIT);
}

void Session::resetCancel () noexcept
{
    control->GetInterrupt ();
}

void Session::appendSymbolPath (const juce::String& path) noexcept
{
    const juce::String nativePath { getNativePath (path) };

    symbols->AppendSymbolPath (nativePath.toRawUTF8 ());
#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: appended symbol path: " + nativePath);
#endif
}

void Session::appendSourcePath (const juce::String& path) noexcept
{
    const juce::String nativePath { getNativePath (path) };

    symbols->AppendSourcePath (nativePath.toRawUTF8 ());
#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: appended source path: " + nativePath);
#endif
}

void Session::stepOver () noexcept
{
    disableBreakpointAtInstruction ();

    control->SetExecutionStatus (DEBUG_STATUS_STEP_OVER);
}

void Session::stepInto () noexcept
{
    disableBreakpointAtInstruction ();

    control->SetExecutionStatus (DEBUG_STATUS_STEP_INTO);
}

void Session::stepOut () noexcept
{
    disableBreakpointAtInstruction ();

    control->Execute (DEBUG_OUTCTL_IGNORE, "gu", DEBUG_EXECUTE_NOT_LOGGED);
}

void Session::interrupt (std::uint32_t processId) noexcept
{
    if (processId != 0)
    {
        const std::unique_ptr<void, decltype (&CloseHandle)> process { OpenProcess (PROCESS_VM_OPERATION bitor PROCESS_VM_WRITE bitor PROCESS_CREATE_THREAD,
                                                                                    FALSE, static_cast<DWORD> (processId)), &CloseHandle };
        jassert (process != nullptr);

        if (process != nullptr)
        {
            const BOOL isInterrupted { DebugBreakProcess (process.get ()) };
            jassert (isInterrupted);
#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: DebugBreakProcess PID=" + juce::String (static_cast<unsigned long> (processId)) + " result=" + juce::String (static_cast<int> (isInterrupted)) + " error=" + juce::String (GetLastError ()));
#endif
            juce::ignoreUnused (isInterrupted);
        }
    }
}

void Session::terminateDebuggee (std::uint32_t processId) noexcept
{
    juce::ignoreUnused (processId);

    if (client != nullptr)
    {
        const HRESULT result { client->TerminateProcesses () };

        if (SUCCEEDED (result))
        {
#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: TerminateProcesses success");
#endif
        }
        else
        {
#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: TerminateProcesses failed, hr=0x"
                                     + juce::String::toHexString (static_cast<unsigned long> (result)));
#endif
        }
    }
}

juce::Result Session::loadModuleSymbols (const juce::String& imageName) noexcept
{
    const juce::String basename { juce::File (imageName).getFileName () };
    const juce::String command { ".reload /f " + basename.quoted () };
    const HRESULT hr { control->Execute (DEBUG_OUTCTL_IGNORE,
                                         command.toRawUTF8 (),
                                         DEBUG_EXECUTE_NOT_LOGGED) };
#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: .reload /f " + basename + " hr=0x"
                             + juce::String::toHexString (static_cast<unsigned long> (hr)));
#endif
    return hr == S_OK ? juce::Result::ok ()
                      : juce::Result::fail ("loadModuleSymbols: HRESULT 0x"
                          + juce::String::toHexString (static_cast<int> (hr)));
}

juce::Result Session::forceReloadAllSymbols () noexcept
{
    const HRESULT hr { control->Execute (DEBUG_OUTCTL_IGNORE,
                                         ".reload /f",
                                         DEBUG_EXECUTE_NOT_LOGGED) };
#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: .reload /f (all) hr=0x"
                             + juce::String::toHexString (static_cast<unsigned long> (hr)));
#endif
    return hr == S_OK ? juce::Result::ok ()
                      : juce::Result::fail ("forceReloadAllSymbols: HRESULT 0x"
                          + juce::String::toHexString (static_cast<int> (hr)));
}

std::optional<std::int32_t> Session::addBreakpoint (std::uint64_t offset) noexcept
{
    std::optional<std::int32_t> engineId;

    IDebugBreakpoint2* bp { nullptr };
    const HRESULT hr { control->AddBreakpoint2 (DEBUG_BREAKPOINT_CODE, DEBUG_ANY_ID, &bp) };

    if (hr == S_OK and bp != nullptr)
    {
        bp->SetOffset (offset);
        bp->AddFlags (DEBUG_BREAKPOINT_ENABLED);
        ULONG rawEngineId { 0 };
        bp->GetId (&rawEngineId);
        engineId = static_cast<std::int32_t> (rawEngineId);
    }
    else
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: addBreakpoint: HRESULT 0x"
            + juce::String::toHexString (static_cast<int> (hr)));
#endif
    }

    return engineId;
}

std::optional<BreakpointLocation> Session::addBreakpointByLocation (const juce::String& filePath,
                                                                    std::uint16_t       line) noexcept
{
    std::optional<BreakpointLocation> location;
    DEBUG_SYMBOL_SOURCE_ENTRY entry {};
    ULONG entriesAvailable { 0 };
    const juce::String nativePath { getNativePath (filePath) };

    const HRESULT hr { symbols->GetSourceEntriesByLine (static_cast<ULONG> (line),
                                                        nativePath.toRawUTF8 (),
                                                        DEBUG_GSEL_ALLOW_HIGHER | DEBUG_GSEL_NEAREST_ONLY,
                                                        &entry, 1, &entriesAvailable) };

    if (SUCCEEDED (hr) and entriesAvailable > 0)
    {
        const std::optional<std::int32_t> engineId { addBreakpoint (entry.Offset) };

        if (engineId.has_value ())
            location = BreakpointLocation::pack (*engineId, static_cast<std::uint16_t> (entry.StartLine));
    }

    return location;
}

juce::Result Session::removeBreakpoint (std::int32_t engineId) noexcept
{
    IDebugBreakpoint2* bp { nullptr };
    const HRESULT hr { control->GetBreakpointById2 (static_cast<ULONG> (engineId), &bp) };

    if (hr == S_OK and bp != nullptr)
    {
        const HRESULT hrRemove { control->RemoveBreakpoint2 (bp) };
        return hrRemove == S_OK ? juce::Result::ok ()
                                : juce::Result::fail ("removeBreakpoint: HRESULT 0x"
                                    + juce::String::toHexString (static_cast<int> (hrRemove)));
    }

    return juce::Result::fail ("removeBreakpoint: GetBreakpointById2 HRESULT 0x"
        + juce::String::toHexString (static_cast<int> (hr)));
}

void Session::resetSymbolGroupCache () noexcept
{
    cachedSymbolGroup = nullptr;
    cachedFrameIndex = -1;
}

void Session::setScope (int frameIndex) noexcept
{
    if (unreportedThreadId != 0)
    {
        auto [traceFrames, traceContexts] { getContextStackTrace (*control.get (), unreportedThreadId, frameIndex + 1) };

        if (static_cast<size_t> (frameIndex) < traceContexts.size ())
            symbols->SetScope (0, nullptr, &traceContexts.at (static_cast<size_t> (frameIndex)), sizeof (CONTEXT));
    }
    else
    {
        symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));
    }
}

IDebugSymbolGroup2* Session::getOrCreateSymbolGroup (int frameIndex) noexcept
{
    IDebugSymbolGroup2* result { nullptr };

    if (cachedSymbolGroup != nullptr and cachedFrameIndex == frameIndex)
    {
        result = cachedSymbolGroup.get ();
    }
    else
    {
        cachedSymbolGroup = nullptr;

        IDebugSymbolGroup2* group { nullptr };
        const HRESULT hr { symbols->GetScopeSymbolGroup2 (
            DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

        if (SUCCEEDED (hr) and group != nullptr)
        {
            cachedSymbolGroup = juce::ComSmartPtr<IDebugSymbolGroup2> (group, juce::IncrementRef::no);
            cachedFrameIndex = frameIndex;
            result = cachedSymbolGroup.get ();
        }
    }

    return result;
}

static constexpr const char* defaultThreadNamePrefix { "Thread " };

static void appendThread (std::string& json, std::uint32_t systemId, const juce::String& name)
{
    jam::JsonWriter::appendSeparator (json);
    jam::JsonWriter::appendObject (json, [systemId, &name] (std::string& thread)
    {
        jam::JsonWriter::appendKey (thread, Id::id);
        jam::Format::appendDecimal (thread, static_cast<int> (systemId));
        jam::JsonWriter::appendKey (thread, Id::name);
        jam::JsonWriter::appendString (thread, jam::Strings::getView (name));
    });
}

juce::String Session::getThreadName (std::uint32_t systemId) noexcept
{
    juce::String name { defaultThreadNamePrefix + juce::String (systemId) };

    ULONG64 handle { 0 };
    systemObjects->GetCurrentThreadHandle (&handle);

    if (handle != 0)
    {
        PWSTR desc { nullptr };
        const HRESULT descResult { GetThreadDescription (
            reinterpret_cast<HANDLE> (static_cast<ULONG_PTR> (handle)), &desc) };

        if (SUCCEEDED (descResult) and desc != nullptr)
        {
            const juce::String threadName { desc };
            LocalFree (desc);

            if (threadName.isNotEmpty ())
            {
                name = threadName;
            }
        }
    }

    return name;
}

void Session::appendThreads (std::string& json) noexcept
{
    ULONG threadCount { 0 };
    systemObjects->GetNumberThreads (&threadCount);

    if (threadCount > 0)
    {
        std::vector<ULONG> engineIds (threadCount);
        std::vector<ULONG> systemIds (threadCount);
        systemObjects->GetThreadIdsByIndex (0, threadCount, engineIds.data (), systemIds.data ());

        ULONG savedEngineId { 0 };
        systemObjects->GetCurrentThreadId (&savedEngineId);

        for (ULONG i { 0 }; i < threadCount; ++i)
        {
            systemObjects->SetCurrentThreadId (engineIds.at (i));
            appendThread (json, systemIds.at (i), getThreadName (systemIds.at (i)));
        }

        systemObjects->SetCurrentThreadId (savedEngineId);
    }

    appendUnreportedThreads (json);
}

void Session::appendUnreportedThreads (std::string& json) noexcept
{
    auto* model { WHATDBGModel::getInstance () };
    const auto targetProcessId { static_cast<DWORD> (model->getParameter<jam::Parameter<int64_t>> (model->sessionTag, Id::targetProcessId)->getValue ()) };
    std::unique_ptr<void, decltype (&CloseHandle)> snapshot { CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0), &CloseHandle };
    jassert (snapshot.get () != INVALID_HANDLE_VALUE);
    THREADENTRY32 entry { sizeof (THREADENTRY32) };

    for (BOOL hasEntry { Thread32First (snapshot.get (), &entry) }; hasEntry; hasEntry = Thread32Next (snapshot.get (), &entry))
    {
        ULONG engineId { 0 };

        if (entry.th32OwnerProcessID == targetProcessId
            and FAILED (systemObjects->GetThreadIdBySystemId (static_cast<ULONG> (entry.th32ThreadID), &engineId)))
            appendThread (json, entry.th32ThreadID, defaultThreadNamePrefix + juce::String (static_cast<unsigned long> (entry.th32ThreadID)));
    }
}

std::uint32_t Session::getEventThreadSystemId () noexcept
{
    std::uint32_t systemId { 0 };

    ULONG eventEngineId { 0 };
    const HRESULT hr { systemObjects->GetEventThread (&eventEngineId) };

    if (SUCCEEDED (hr))
    {
        ULONG savedId { 0 };
        systemObjects->GetCurrentThreadId (&savedId);
        systemObjects->SetCurrentThreadId (eventEngineId);
        ULONG rawSystemId { 0 };
        systemObjects->GetCurrentThreadSystemId (&rawSystemId);
        systemObjects->SetCurrentThreadId (savedId);
        systemId = static_cast<std::uint32_t> (rawSystemId);
    }

    return systemId;
}

void Session::setCurrentThreadBySystemId (std::uint32_t systemId) noexcept
{
    jassert (systemId != 0);

    ULONG engineId { 0 };
    const HRESULT hr { systemObjects->GetThreadIdBySystemId (static_cast<ULONG> (systemId), &engineId) };

    const std::uint32_t previousUnreportedThreadId { unreportedThreadId };
    ULONG previousEngineId { 0 };
    systemObjects->GetCurrentThreadId (&previousEngineId);

    if (SUCCEEDED (hr))
    {
        systemObjects->SetCurrentThreadId (engineId);
        unreportedThreadId = 0;
    }
    else
    {
        unreportedThreadId = systemId;
    }

    ULONG currentEngineId { 0 };
    systemObjects->GetCurrentThreadId (&currentEngineId);

    if (currentEngineId != previousEngineId or unreportedThreadId != previousUnreportedThreadId)
        resetSymbolGroupCache ();
}

static juce::String getSymbolTypeName (IDebugSymbolGroup2* group, ULONG index) noexcept
{
    static constexpr int symbolTypeSize { 256 };

    char typeBuffer[symbolTypeSize] {};
    group->GetSymbolTypeName (index, typeBuffer, symbolTypeSize, nullptr);

    return juce::String { typeBuffer };
}

static juce::String getSymbolValue (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                                    IDebugSymbols3* symbols, ULONG index,
                                    const juce::String& typeName) noexcept
{
    static constexpr int symbolValueSize { 512 };

    char valueBuffer[symbolValueSize] {};
    const HRESULT valueResult { group->GetSymbolValueText (index, valueBuffer, symbolValueSize, nullptr) };

    juce::String displayValue { SUCCEEDED (valueResult)
        ? formatSymbolValue (juce::String (valueBuffer))
        : juce::String ("<unavailable>") };

    const juce::String prettyValue { prettyPrint (group, dataSpaces, symbols, static_cast<int> (index), typeName) };

    if (prettyValue.isNotEmpty ())
        displayValue = prettyValue;

    return displayValue;
}

template <typename Function>
static void applyToSymbols (IDebugSymbolGroup2* group, IDebugDataSpaces4* dataSpaces,
                            IDebugSymbols3* symbols, ULONG parentFilter,
                            const Function& function) noexcept
{
    static constexpr int symbolNameSize { 256 };

    ULONG count { 0 };
    group->GetNumberSymbols (&count);

    for (ULONG i { 0 }; i < count; ++i)
    {
        DEBUG_SYMBOL_PARAMETERS params {};
        const HRESULT paramResult { group->GetSymbolParameters (i, 1, &params) };

        if (SUCCEEDED (paramResult) and params.ParentSymbol == parentFilter)
        {
            char nameBuffer[symbolNameSize] {};
            group->GetSymbolName (i, nameBuffer, symbolNameSize, nullptr);
            const juce::String symbolName { nameBuffer };

            if (not shouldSkipSymbol (symbolName))
            {
                const juce::String typeName { getSymbolTypeName (group, i) };
                const juce::String displayValue { getSymbolValue (group, dataSpaces, symbols, i, typeName) };

                function (symbolName, displayValue, typeName, params.SubElements > 0, static_cast<int> (i));
            }
        }

        group->GetNumberSymbols (&count);
    }
}

juce::String Session::getFrameName (std::uint64_t instructionOffset) noexcept
{
    static constexpr int nameBufferSize { 512 };

    char nameBuffer[nameBufferSize] {};
    ULONG nameSize { 0 };
    ULONG64 displacement { 0 };

    const HRESULT nameResult { symbols->GetNameByOffset (
        instructionOffset, nameBuffer, nameBufferSize, &nameSize, &displacement) };

    return SUCCEEDED (nameResult) ? juce::String (nameBuffer) : juce::String ("frame");
}

void Session::appendStackFrame (std::string& json, const DEBUG_STACK_FRAME& stackFrame, int frameId) noexcept
{
    static constexpr int fileBufferSize { 1024 };
    static constexpr int firstColumn { 1 };
    char fileBuffer[fileBufferSize] {};
    ULONG line { 0 };

    const HRESULT lineResult { symbols->GetLineByOffset (
        stackFrame.InstructionOffset, &line, fileBuffer, fileBufferSize, nullptr, nullptr) };
    const juce::String filePath { fileBuffer };

    jam::JsonWriter::appendSeparator (json);
    jam::JsonWriter::appendObject (json, [this, frameId, line, lineResult, &stackFrame, &filePath] (std::string& frame)
    {
        jam::JsonWriter::appendKey (frame, Id::id);
        jam::Format::appendDecimal (frame, frameId);
        jam::JsonWriter::appendKey (frame, Id::name);
        jam::JsonWriter::appendString (frame, jam::Strings::getView (getFrameName (stackFrame.InstructionOffset)));

        if (SUCCEEDED (lineResult))
        {
            jam::JsonWriter::appendKey (frame, Id::source);
            dap::appendSource (frame, jam::Strings::getView (juce::File (filePath).getFileName ()), jam::Strings::getView (filePath.replace ("\\", "/")));
            jam::JsonWriter::appendKey (frame, Id::line);
            jam::Format::appendDecimal (frame, static_cast<int> (line));
            jam::JsonWriter::appendKey (frame, Id::column);
            jam::Format::appendDecimal (frame, firstColumn);
        }
    });
}

int Session::appendStackFrames (std::string& json, int maxFrames, const std::function<int (int frameIndex)>& addFrame) noexcept
{
    jassert (maxFrames > 0);

    std::vector<DEBUG_STACK_FRAME> stackFrames (static_cast<size_t> (maxFrames));
    ULONG framesFilled { 0 };

    if (unreportedThreadId != 0)
    {
        const auto [traceFrames, traceContexts] { getContextStackTrace (*control.get (), unreportedThreadId, maxFrames) };
        framesFilled = static_cast<ULONG> (traceFrames.size ());
        std::copy (traceFrames.begin (), traceFrames.end (), stackFrames.begin ());
    }
    else
    {
        control->GetStackTrace (0, 0, 0, stackFrames.data (), static_cast<ULONG> (maxFrames), &framesFilled);
    }

    for (ULONG i { 0 }; i < framesFilled; ++i)
        appendStackFrame (json, stackFrames.at (static_cast<size_t> (i)), addFrame (static_cast<int> (i)));

    return static_cast<int> (framesFilled);
}

void Session::applyToLocals (int frameIndex, const std::function<void (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex)>& function) noexcept
{
    setScope (frameIndex);
    IDebugSymbolGroup2* group { getOrCreateSymbolGroup (frameIndex) };

    if (group != nullptr)
    {
        applyToSymbols (group, dataSpaces.get (), symbols.get (), DEBUG_ANY_ID, function);
    }
}

void Session::applyToVariableChildren (int frameIndex, int symbolIndex, const std::function<void (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex)>& function) noexcept
{
    setScope (frameIndex);
    IDebugSymbolGroup2* group { getOrCreateSymbolGroup (frameIndex) };

    if (group != nullptr)
    {
        const ULONG parentIndex { static_cast<ULONG> (symbolIndex) };
        const HRESULT expandResult { group->ExpandSymbol (parentIndex, TRUE) };

        if (SUCCEEDED (expandResult))
        {
            applyToSymbols (group, dataSpaces.get (), symbols.get (), parentIndex, function);
        }
    }
}

juce::String Session::evaluateExpression (const juce::String& expression, int frameIndex, bool isHover) noexcept
{
    juce::ignoreUnused (isHover);

    juce::String result;

    setScope (frameIndex);
    IDebugSymbolGroup2* group { getOrCreateSymbolGroup (frameIndex) };

    if (group != nullptr)
    {
        ULONG previousOptions { 0 };
        symbols->GetSymbolOptions (&previousOptions);
        symbols->AddSymbolOptions (SYMOPT_NO_PUBLICS);
        const juce::ErasedScopeGuard optionsRestore { [this, previousOptions] { symbols->SetSymbolOptions (previousOptions); } };

        ULONG index { DEBUG_ANY_ID };
        const HRESULT addResult { group->AddSymbol (expression.toRawUTF8 (), &index) };

        if (addResult == S_OK)
        {
            const juce::ErasedScopeGuard symbolRemove { [group, index] { group->RemoveSymbolByIndex (index); } };
            result = getSymbolValue (group, dataSpaces.get (), symbols.get (), index, getSymbolTypeName (group, index));
        }
    }

    return result;
}

} // namespace debug

#endif // JUCE_WINDOWS
