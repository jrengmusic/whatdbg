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
 *  Threading model: the engine context runs every call except wake and cancel. dbgeng
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

namespace debug
{

Session::~Session ()
{
    shutdown (EndMode::passive);
}

IDebugClient5* Session::getOrCreateDebugClient (const juce::File& sidecarDir) noexcept
{
    IDebugClient5* rawClient { nullptr };

    const HRESULT comResult { CoInitializeEx (nullptr, COINIT_MULTITHREADED) };
    const bool isComOk { comResult == S_OK or comResult == RPC_E_CHANGED_MODE };

    if (isComOk)
    {
        isComOwned = (comResult == S_OK);
        const bool isLoaderOk { loader.load (sidecarDir) };

        if (isLoaderOk)
        {
            rawClient = loader.createDebugClient ();
        }
    }

    return rawClient;
}

bool Session::getOrCreateDebugInterfaces () noexcept
{
    const HRESULT qiControlResult { client->QueryInterface (
        __uuidof (IDebugControl4),
        reinterpret_cast<PVOID*> (control.GetAddressOf ())) };
    juce::ignoreUnused (qiControlResult);

    const HRESULT qiSymbolsResult { client->QueryInterface (
        __uuidof (IDebugSymbols3),
        reinterpret_cast<PVOID*> (symbols.GetAddressOf ())) };
    juce::ignoreUnused (qiSymbolsResult);

    const HRESULT qiDataResult { client->QueryInterface (
        __uuidof (IDebugDataSpaces4),
        reinterpret_cast<PVOID*> (dataSpaces.GetAddressOf ())) };
    juce::ignoreUnused (qiDataResult);

    const HRESULT qiSysObjResult { client->QueryInterface (
        __uuidof (IDebugSystemObjects),
        reinterpret_cast<PVOID*> (systemObjects.GetAddressOf ())) };
    juce::ignoreUnused (qiSysObjResult);

    return control != nullptr
           and symbols != nullptr
           and dataSpaces != nullptr
           and systemObjects != nullptr;
}

void Session::setDebugInterfaces () noexcept
{
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
    control->AddEngineOptions (DEBUG_ENGOPT_INITIAL_BREAK);
    client->SetEventCallbacks (&eventCallbacks);
}

bool Session::initialize (const juce::File& sidecarDir) noexcept
{
    IDebugClient5* rawClient { getOrCreateDebugClient (sidecarDir) };

    if (rawClient != nullptr)
    {
        client.Attach (rawClient);

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

static juce::String normalizeCommandLine (const juce::String& program) noexcept
{
    juce::String normalized { program.replace ("/", "\\") };

    if (normalized.containsChar (' ') and not normalized.startsWithChar ('"'))
        normalized = normalized.quoted ();

    return normalized;
}

bool Session::launch (const juce::String& program) noexcept
{
    jassert (client != nullptr);

    const juce::String normalized { normalizeCommandLine (program) };

#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: CreateProcess2 commandLine: " + normalized);
#endif

    std::string commandLineBuffer { normalized.toStdString () };

    DEBUG_CREATE_PROCESS_OPTIONS options {};
    options.CreateFlags    = DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE;
    options.EngCreateFlags = 0;
    options.VerifierFlags  = 0;
    options.Reserved       = 0;

    const HRESULT result { client->CreateProcess2 (
        0,
        commandLineBuffer.data (),
        &options,
        sizeof (options),
        nullptr,
        nullptr) };

    const bool launched { SUCCEEDED (result) };

    if (launched)
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: launched process: " + program);
#endif
    }
    else
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: CreateProcess2 failed, hr=0x"
                                 + juce::String::toHexString (static_cast<unsigned long> (result)));
#endif
    }

    return launched;
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

void Session::resume () noexcept
{
    control->SetExecutionStatus (DEBUG_STATUS_GO);
}

void Session::pollEvents (std::uint32_t timeoutMs) noexcept
{
    const HRESULT hr { control->WaitForEvent (0, static_cast<ULONG> (timeoutMs)) };

    auto* model { WHATDBGModel::getInstance () };

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
    systemObjects.Reset ();
    dataSpaces.Reset ();
    symbols.Reset ();
    control.Reset ();
    client.Reset ();

    if (isComOwned)
    {
        CoUninitialize ();
        isComOwned = false;
    }
}

void Session::wake () noexcept
{
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
    symbols->AppendSymbolPath (path.toRawUTF8 ());
#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: appended symbol path: " + path);
#endif
}

void Session::appendSourcePath (const juce::String& path) noexcept
{
    symbols->AppendSourcePath (path.toRawUTF8 ());
#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: appended source path: " + path);
#endif
}

void Session::stepOver () noexcept
{
    control->SetExecutionStatus (DEBUG_STATUS_STEP_OVER);
}

void Session::stepInto () noexcept
{
    control->SetExecutionStatus (DEBUG_STATUS_STEP_INTO);
}

void Session::stepOut () noexcept
{
    control->Execute (DEBUG_OUTCTL_IGNORE, "gu", DEBUG_EXECUTE_NOT_LOGGED);
}

void Session::interrupt (std::uint32_t processId) noexcept
{
    if (processId != 0)
    {
        const HANDLE handle { OpenProcess (PROCESS_VM_OPERATION bitor PROCESS_VM_WRITE bitor PROCESS_CREATE_THREAD,
                                           FALSE, static_cast<DWORD> (processId)) };

        if (handle != nullptr)
        {
            const BOOL result { DebugBreakProcess (handle) };
            CloseHandle (handle);

            if (result)
            {
#if JUCE_DEBUG
                jam::debug::Log::write ("WHATDBG: DebugBreakProcess success, PID="
                                         + juce::String (static_cast<unsigned long> (processId)));
#endif
            }
            else
            {
#if JUCE_DEBUG
                jam::debug::Log::write ("WHATDBG: DebugBreakProcess failed, PID="
                                         + juce::String (static_cast<unsigned long> (processId))
                                         + " error=" + juce::String (GetLastError ()));
#endif
            }
        }
        else
        {
#if JUCE_DEBUG
            jam::debug::Log::write ("WHATDBG: OpenProcess failed, PID="
                                     + juce::String (static_cast<unsigned long> (processId))
                                     + " error=" + juce::String (GetLastError ()));
#endif
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

static OffsetStatus getLineStatus (HRESULT lineResult) noexcept
{
    static const jam::HashMap<HRESULT, OffsetStatus> lineStatuses
    {
        { S_OK,         OffsetStatus::found      },
        { E_UNEXPECTED, OffsetStatus::engineBusy }
    };

    const auto lineStatus { lineStatuses.find (lineResult) };

    if (lineStatus != lineStatuses.end ())
    {
        const auto& [statusResult, status] { *lineStatus };
        return status;
    }

    return OffsetStatus::notFound;
}

static std::pair<OffsetStatus, std::uint64_t> getSourceOffset (IDebugSymbols3*     symbols,
                                                                const juce::String& filePath,
                                                                std::uint16_t       line) noexcept
{
    std::uint64_t offset { 0 };
    OffsetStatus  status { getLineStatus (symbols->GetOffsetByLine (static_cast<ULONG> (line),
                                                                     filePath.toRawUTF8 (),
                                                                     &offset)) };

    if (status == OffsetStatus::notFound)
    {
        const juce::String basename { filePath.fromLastOccurrenceOf ("\\", false, false) };
        status = getLineStatus (symbols->GetOffsetByLine (static_cast<ULONG> (line),
                                                           basename.toRawUTF8 (),
                                                           &offset));

        if (status == OffsetStatus::found)
        {
            char pathBuffer[MAX_PATH] {};
            ULONG pathSize { 0 };
            ULONG lineOut { 0 };
            const HRESULT lineResult { symbols->GetLineByOffset (offset, &lineOut,
                                                                  pathBuffer, MAX_PATH,
                                                                  &pathSize, nullptr) };

            const juce::String storedPath    { juce::String (pathBuffer).replace ("\\", "/") };
            const juce::String requestedPath { filePath.replace ("\\", "/") };
            const bool isSourceMatch { storedPath.endsWithIgnoreCase (requestedPath)
                                       or requestedPath.endsWithIgnoreCase (storedPath) };

            if (lineResult != S_OK or not isSourceMatch)
                status = OffsetStatus::notFound;
        }
    }

    return { status, status == OffsetStatus::found ? offset : std::uint64_t { 0 } };
}

OffsetStatus Session::getOffsetStatus (const juce::String& filePath,
                                       std::uint16_t       line) noexcept
{
    const auto [resolvedStatus, resolvedOffset] { getSourceOffset (symbols.Get (), filePath, line) };
    juce::ignoreUnused (resolvedOffset);

    return resolvedStatus;
}

std::uint64_t Session::getOffset (const juce::String& filePath,
                                  std::uint16_t       line) noexcept
{
    const auto [resolvedStatus, resolvedOffset] { getSourceOffset (symbols.Get (), filePath, line) };
    juce::ignoreUnused (resolvedStatus);

    return resolvedOffset;
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

    const OffsetStatus status { getOffsetStatus (filePath, line) };

    if (status == OffsetStatus::found)
    {
        const std::uint64_t               offset   { getOffset (filePath, line) };
        const std::optional<std::int32_t> engineId { addBreakpoint (offset) };

        if (engineId.has_value ())
            location = BreakpointLocation::pack (*engineId, line);
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
    cachedSymbolGroup.Reset ();
    cachedFrameIndex = -1;
}

IDebugSymbolGroup2* Session::getOrCreateSymbolGroup (int frameIndex) noexcept
{
    IDebugSymbolGroup2* result { nullptr };

    if (cachedSymbolGroup != nullptr and cachedFrameIndex == frameIndex)
    {
        result = cachedSymbolGroup.Get ();
    }
    else
    {
        cachedSymbolGroup.Reset ();

        symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));

        IDebugSymbolGroup2* group { nullptr };
        const HRESULT hr { symbols->GetScopeSymbolGroup2 (
            DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

        if (SUCCEEDED (hr) and group != nullptr)
        {
            cachedSymbolGroup.Attach (group);
            cachedFrameIndex = frameIndex;
            result = cachedSymbolGroup.Get ();
        }
    }

    return result;
}

juce::String Session::getThreadName (std::uint32_t systemId) noexcept
{
    juce::String name { "Thread " + juce::String (systemId) };

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
            jam::JsonWriter::appendSeparator (json);
            jam::JsonWriter::appendObject (json, [this, &systemIds, i] (std::string& thread)
            {
                jam::JsonWriter::appendKey (thread, Id::id);
                jam::Format::appendDecimal (thread, static_cast<int> (systemIds.at (i)));
                jam::JsonWriter::appendKey (thread, Id::name);
                jam::JsonWriter::appendString (thread, jam::Strings::getView (getThreadName (systemIds.at (i))));
            });
        }

        systemObjects->SetCurrentThreadId (savedEngineId);
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

    if (SUCCEEDED (hr))
    {
        systemObjects->SetCurrentThreadId (engineId);
    }
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

    const int frameCount { maxFrames };

    std::vector<DEBUG_STACK_FRAME> stackFrames (static_cast<size_t> (frameCount));
    ULONG framesFilled { 0 };
    int appendedFrames { 0 };

    const HRESULT hr { control->GetStackTrace (
        0, 0, 0,
        stackFrames.data (),
        static_cast<ULONG> (frameCount),
        &framesFilled) };

    if (SUCCEEDED (hr))
    {
        for (ULONG i { 0 }; i < framesFilled; ++i)
            appendStackFrame (json, stackFrames.at (static_cast<size_t> (i)),
                              addFrame (static_cast<int> (i)));

        appendedFrames = static_cast<int> (framesFilled);
    }

    return appendedFrames;
}

void Session::applyToLocals (int frameIndex, const std::function<void (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex)>& function) noexcept
{
    IDebugSymbolGroup2* group { getOrCreateSymbolGroup (frameIndex) };

    if (group != nullptr)
    {
        applyToSymbols (group, dataSpaces.Get (), symbols.Get (), DEBUG_ANY_ID, function);
    }
}

void Session::applyToVariableChildren (int frameIndex, int symbolIndex, const std::function<void (const juce::String& name, const juce::String& value, const juce::String& type, bool hasChildren, int symbolIndex)>& function) noexcept
{
    IDebugSymbolGroup2* group { getOrCreateSymbolGroup (frameIndex) };

    if (group != nullptr)
    {
        const ULONG parentIndex { static_cast<ULONG> (symbolIndex) };
        const HRESULT expandResult { group->ExpandSymbol (parentIndex, TRUE) };

        if (SUCCEEDED (expandResult))
        {
            applyToSymbols (group, dataSpaces.Get (), symbols.Get (), parentIndex, function);
        }
    }
}

juce::String Session::evaluateExpression (const juce::String& expression, int frameIndex) noexcept
{
    juce::String result;

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
            result = getSymbolValue (group, dataSpaces.Get (), symbols.Get (), index, getSymbolTypeName (group, index));
        }
    }

    return result;
}

} // namespace debug

#endif // JUCE_WINDOWS
