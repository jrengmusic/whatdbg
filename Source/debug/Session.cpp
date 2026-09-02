/** @file Session.cpp
 *  @brief Windows debug session implementation using Microsoft's dbgeng COM API.
 *
 *  Platform counterpart to Session_mac.cpp (macOS liblldb). Implements the Session
 *  interface declared in Session.h using IDebugClient5, IDebugControl4,
 *  IDebugSymbols3, IDebugDataSpaces4, and IDebugSystemObjects.
 *
 *  Threading model: single-threaded main loop. COM callbacks (Callbacks.cpp) fire
 *  on the dbgeng thread and store deferred flags on State. Main thread polls and
 *  processes those flags.
 *
 *  Lifecycle: initialize (Loader → COM) → (launch | attach) → pollEvents loop → shutdown.
 */

#include <JuceHeader.h>
#include "Session.h"
#include "../dap/Types.h"

#if JUCE_WINDOWS
#include <dbghelp.h>

namespace debug
{

using dap::DynObj;

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
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_GO);
    }
}

bool Session::pollEvents (std::uint32_t timeoutMs) noexcept
{
    bool hadEvent { false };

    if (control != nullptr)
    {
        const HRESULT hr { control->WaitForEvent (0, static_cast<ULONG> (timeoutMs)) };
        hadEvent = hr == S_OK;
    }

    return hadEvent;
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

void Session::appendSymbolPath (const juce::String& path) noexcept
{
    if (symbols != nullptr)
    {
        symbols->AppendSymbolPath (path.toRawUTF8 ());
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: appended symbol path: " + path);
#endif
    }
}

void Session::appendSourcePath (const juce::String& path) noexcept
{
    if (symbols != nullptr)
    {
        symbols->AppendSourcePath (path.toRawUTF8 ());
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: appended source path: " + path);
#endif
    }
}

void Session::stepOver () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_STEP_OVER);
    }
}

void Session::stepInto () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_STEP_INTO);
    }
}

void Session::stepOut () noexcept
{
    if (control != nullptr)
    {
        control->Execute (DEBUG_OUTCTL_IGNORE, "gu", DEBUG_EXECUTE_NOT_LOGGED);
    }
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
    juce::Result result { juce::Result::fail ("loadModuleSymbols: control is null") };

    if (control != nullptr)
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
        result = hr == S_OK ? juce::Result::ok ()
                            : juce::Result::fail ("loadModuleSymbols: HRESULT 0x"
                                + juce::String::toHexString (static_cast<int> (hr)));
    }

    return result;
}

juce::Result Session::forceReloadAllSymbols () noexcept
{
    juce::Result result { juce::Result::fail ("forceReloadAllSymbols: control is null") };

    if (control != nullptr)
    {
        const HRESULT hr { control->Execute (DEBUG_OUTCTL_IGNORE,
                                             ".reload /f",
                                             DEBUG_EXECUTE_NOT_LOGGED) };
#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: .reload /f (all) hr=0x"
                                 + juce::String::toHexString (static_cast<unsigned long> (hr)));
#endif
        result = hr == S_OK ? juce::Result::ok ()
                            : juce::Result::fail ("forceReloadAllSymbols: HRESULT 0x"
                                + juce::String::toHexString (static_cast<int> (hr)));
    }

    return result;
}

static std::pair<OffsetStatus, std::uint64_t> getSourceOffset (IDebugSymbols3*     symbols,
                                                                const juce::String& filePath,
                                                                std::uint16_t       line) noexcept
{
    jassert (symbols != nullptr);

    OffsetStatus  status { OffsetStatus::notFound };
    std::uint64_t offset { 0 };

    std::uint64_t fullPathOffset { 0 };
    const HRESULT fullPathResult { symbols->GetOffsetByLine (static_cast<ULONG> (line),
                                                              filePath.toRawUTF8 (),
                                                              &fullPathOffset) };

    if (fullPathResult == S_OK)
    {
        status = OffsetStatus::found;
        offset = fullPathOffset;
    }
    else if (fullPathResult == E_UNEXPECTED)
    {
        status = OffsetStatus::engineBusy;
    }
    else
    {
        juce::String basename { filePath };
        const int lastSeparator { filePath.lastIndexOfChar ('\\') };

        if (lastSeparator >= 0)
            basename = filePath.substring (lastSeparator + 1);

        std::uint64_t basenameOffset { 0 };
        const HRESULT basenameResult { symbols->GetOffsetByLine (static_cast<ULONG> (line),
                                                                  basename.toRawUTF8 (),
                                                                  &basenameOffset) };

        if (basenameResult == S_OK)
        {
            char pathBuffer[MAX_PATH] {};
            ULONG pathSize { 0 };
            ULONG lineOut { 0 };
            const HRESULT lineResult { symbols->GetLineByOffset (basenameOffset, &lineOut,
                                                                  pathBuffer, MAX_PATH,
                                                                  &pathSize, nullptr) };

            if (lineResult == S_OK)
            {
                const juce::String storedPath    { juce::String (pathBuffer).replace ("\\", "/") };
                const juce::String requestedPath { filePath.replace ("\\", "/") };
                const bool isSourceMatch { storedPath.endsWithIgnoreCase (requestedPath)
                                           or requestedPath.endsWithIgnoreCase (storedPath) };

                if (isSourceMatch)
                {
                    status = OffsetStatus::found;
                    offset = basenameOffset;
                }
            }
        }
        else if (basenameResult == E_UNEXPECTED)
        {
            status = OffsetStatus::engineBusy;
        }
    }

    return { status, offset };
}

OffsetStatus Session::getOffsetStatus (const juce::String& filePath,
                                       std::uint16_t       line) noexcept
{
    OffsetStatus status { OffsetStatus::notFound };

    if (symbols != nullptr)
    {
        const auto [resolvedStatus, resolvedOffset] { getSourceOffset (symbols.Get (), filePath, line) };
        juce::ignoreUnused (resolvedOffset);
        status = resolvedStatus;
    }

    return status;
}

std::uint64_t Session::getOffset (const juce::String& filePath,
                                  std::uint16_t       line) noexcept
{
    std::uint64_t offset { 0 };

    if (symbols != nullptr)
    {
        const auto [resolvedStatus, resolvedOffset] { getSourceOffset (symbols.Get (), filePath, line) };
        juce::ignoreUnused (resolvedStatus);
        offset = resolvedOffset;
    }

    return offset;
}

std::int32_t Session::addBreakpoint (std::uint64_t offset) noexcept
{
    std::int32_t engineId { 0 };

    if (control != nullptr)
    {
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
    }

    return engineId;
}

BreakpointLocation Session::addBreakpointByLocation (const juce::String& filePath,
                                                     std::uint16_t       line) noexcept
{
    BreakpointLocation location { BreakpointLocation::pack (0, 0) };

    const OffsetStatus status { getOffsetStatus (filePath, line) };

    if (status == OffsetStatus::found)
    {
        const std::uint64_t offset   { getOffset (filePath, line) };
        const std::int32_t  engineId { addBreakpoint (offset) };

        if (engineId != 0)
            location = BreakpointLocation::pack (engineId, line);
    }

    return location;
}

juce::Result Session::removeBreakpoint (std::int32_t engineId) noexcept
{
    juce::Result result { juce::Result::fail ("removeBreakpoint: control is null") };

    if (control != nullptr)
    {
        IDebugBreakpoint2* bp { nullptr };
        const HRESULT hr { control->GetBreakpointById2 (static_cast<ULONG> (engineId), &bp) };

        if (hr == S_OK and bp != nullptr)
        {
            const HRESULT hrRemove { control->RemoveBreakpoint2 (bp) };
            result = hrRemove == S_OK ? juce::Result::ok ()
                                      : juce::Result::fail ("removeBreakpoint: HRESULT 0x"
                                          + juce::String::toHexString (static_cast<int> (hrRemove)));
        }
        else
        {
            result = juce::Result::fail ("removeBreakpoint: GetBreakpointById2 HRESULT 0x"
                + juce::String::toHexString (static_cast<int> (hr)));
        }
    }

    return result;
}

void Session::resetSymbolGroupCache () noexcept
{
    cachedSymbolGroup.Reset ();
    cachedFrameIndex = -1;
}

IDebugSymbolGroup2* Session::getOrCreateSymbolGroup (int frameIndex) noexcept
{
    IDebugSymbolGroup2* result { nullptr };

    if (symbols != nullptr)
    {
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
    }

    return result;
}

juce::Array<juce::var> Session::getThreads () noexcept
{
    juce::Array<juce::var> threads;

    if (systemObjects != nullptr)
    {
        ULONG threadCount { 0 };
        systemObjects->GetNumberThreads (&threadCount);

        if (threadCount > 0)
        {
            std::vector<ULONG> engineIds (threadCount);
            std::vector<ULONG> systemIds (threadCount);
            systemObjects->GetThreadIdsByIndex (0, threadCount,
                                                engineIds.data (), systemIds.data ());

            ULONG savedEngineId { 0 };
            systemObjects->GetCurrentThreadId (&savedEngineId);

            for (ULONG i { 0 }; i < threadCount; ++i)
            {
                systemObjects->SetCurrentThreadId (engineIds.at (i));

                juce::String name { "Thread " + juce::String (systemIds.at (i)) };

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

                DynObj thread { new juce::DynamicObject () };
                thread->setProperty ("id",   static_cast<int> (systemIds.at (i)));
                thread->setProperty ("name", name);
                threads.add (juce::var (thread));
            }

            systemObjects->SetCurrentThreadId (savedEngineId);
        }
    }

    return threads;
}

std::uint32_t Session::getEventThreadSystemId () noexcept
{
    std::uint32_t systemId { 0 };

    if (systemObjects != nullptr)
    {
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
    }

    return systemId;
}

void Session::setCurrentThreadBySystemId (std::uint32_t systemId) noexcept
{
    if (systemObjects != nullptr and systemId != 0)
    {
        ULONG engineId { 0 };
        const HRESULT hr { systemObjects->GetThreadIdBySystemId (static_cast<ULONG> (systemId), &engineId) };

        if (SUCCEEDED (hr))
        {
            systemObjects->SetCurrentThreadId (engineId);
        }
    }
}

} // namespace debug

#endif // JUCE_WINDOWS
