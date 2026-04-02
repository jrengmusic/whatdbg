#include <JuceHeader.h>
#include "Session.h"
#include "../dap/Types.h"
#include "../Log.h"
#include <dbghelp.h>

namespace debug
{

using dap::DynObj;

// ---------------------------------------------------------------------------
// Session::~Session
// ---------------------------------------------------------------------------

Session::~Session ()
{
    shutdown ();
}

// ---------------------------------------------------------------------------
// Session::initialize
// ---------------------------------------------------------------------------

bool Session::initialize (const juce::File& sidecarDir) noexcept
{
    const HRESULT comResult { CoInitializeEx (nullptr, COINIT_MULTITHREADED) };
    const bool isComOk { comResult == S_OK or comResult == RPC_E_CHANGED_MODE };

    if (isComOk)
    {
        isComOwned = (comResult == S_OK);
        const bool isLoaderOk { loader.load (sidecarDir) };

        if (isLoaderOk)
        {
            IDebugClient5* rawClient { nullptr };
            const HRESULT createResult { loader.createDebugClient (&rawClient) };

            if (SUCCEEDED (createResult) and rawClient != nullptr)
            {
                client.Attach (rawClient);

                const HRESULT qiControlResult { client->QueryInterface (
                    __uuidof (IDebugControl4),
                    reinterpret_cast<PVOID*> (control.GetAddressOf ())) };

                if (SUCCEEDED (qiControlResult) and control != nullptr)
                {
                    const HRESULT cbResult { client->SetOutputCallbacks (
                        reinterpret_cast<IDebugOutputCallbacks*> (&outputCallbacks)) };
                    juce::ignoreUnused (cbResult);

                    client->SetOutputMask (
                        DEBUG_OUTPUT_NORMAL
                        | DEBUG_OUTPUT_WARNING
                        | DEBUG_OUTPUT_ERROR
                        | DEBUG_OUTPUT_DEBUGGEE);

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

                    if (symbols != nullptr)
                    {
                        symbols->AddSymbolOptions (SYMOPT_LOAD_LINES);
                        control->SetCodeLevel (DEBUG_LEVEL_SOURCE);
                    }

                    control->AddEngineOptions (DEBUG_ENGOPT_INITIAL_BREAK);
                    client->SetEventCallbacks (&eventCallbacks);
                }
            }
        }
    }

    const bool isInitialized { client != nullptr
                                and control != nullptr
                                and symbols != nullptr
                                and dataSpaces != nullptr
                                and systemObjects != nullptr };

    if (not isInitialized)
    {
        shutdown ();
        logWrite ("WHATDBG: initialization failed\n");
    }

    return isInitialized;
}

// ---------------------------------------------------------------------------
// Session::launch
// ---------------------------------------------------------------------------

bool Session::launch (const juce::String& program) noexcept
{
    jassert (client != nullptr);

    juce::String normalized { program.replace ("/", "\\") };

    if (normalized.containsChar (' ') and not normalized.startsWithChar ('"'))
        normalized = "\"" + normalized + "\"";

    logWrite ("WHATDBG: CreateProcess2 commandLine: %s\n", normalized.toRawUTF8 ());

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
        logWrite ("WHATDBG: launched process: %s\n", program.toRawUTF8 ());
    }
    else
    {
        logWrite ("WHATDBG: CreateProcess2 failed, hr=0x%08lX\n", static_cast<unsigned long> (result));
    }

    return launched;
}

// ---------------------------------------------------------------------------
// Session::attach
// ---------------------------------------------------------------------------

bool Session::attach (ULONG processId) noexcept
{
    jassert (client != nullptr);

    const HRESULT attachResult { client->AttachProcess (0, processId, 0) };
    const bool attached { SUCCEEDED (attachResult) };

    if (attached)
    {
        logWrite ("WHATDBG: attached to process %lu\n", static_cast<unsigned long> (processId));
    }
    else
    {
        logWrite ("WHATDBG: AttachProcess failed, hr=0x%08lX\n", static_cast<unsigned long> (attachResult));
    }

    return attached;
}

// ---------------------------------------------------------------------------
// Session::resume
// ---------------------------------------------------------------------------

void Session::resume () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_GO);
    }
}

// ---------------------------------------------------------------------------
// Session::pollEvents
// ---------------------------------------------------------------------------

HRESULT Session::pollEvents (ULONG timeoutMs) noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        result = control->WaitForEvent (0, timeoutMs);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::shutdown
// ---------------------------------------------------------------------------

void Session::shutdown (bool shouldTerminate) noexcept
{
    if (client != nullptr)
    {
        const ULONG endFlag { static_cast<ULONG> (shouldTerminate
            ? DEBUG_END_ACTIVE_TERMINATE
            : DEBUG_END_ACTIVE_DETACH) };
        client->EndSession (endFlag);
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

// ---------------------------------------------------------------------------
// Session::appendSymbolPath / appendSourcePath
// ---------------------------------------------------------------------------

void Session::appendSymbolPath (const juce::String& path) noexcept
{
    if (symbols != nullptr)
    {
        symbols->AppendSymbolPath (path.toRawUTF8 ());
        logWrite ("WHATDBG: appended symbol path: %s\n", path.toRawUTF8 ());
    }
}

void Session::appendSourcePath (const juce::String& path) noexcept
{
    if (symbols != nullptr)
    {
        symbols->AppendSourcePath (path.toRawUTF8 ());
        logWrite ("WHATDBG: appended source path: %s\n", path.toRawUTF8 ());
    }
}

// ---------------------------------------------------------------------------
// Session::stepOver / stepInto / stepOut / interrupt
// ---------------------------------------------------------------------------

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

void Session::interrupt (ULONG processId) noexcept
{
    if (processId != 0)
    {
        const HANDLE handle { OpenProcess (PROCESS_ALL_ACCESS, FALSE, processId) };

        if (handle != nullptr)
        {
            const BOOL result { DebugBreakProcess (handle) };
            CloseHandle (handle);

            if (result)
            {
                logWrite ("WHATDBG: DebugBreakProcess success, PID=%lu\n",
                          static_cast<unsigned long> (processId));
            }
            else
            {
                logWrite ("WHATDBG: DebugBreakProcess failed, PID=%lu error=%lu\n",
                          static_cast<unsigned long> (processId), GetLastError ());
            }
        }
        else
        {
            logWrite ("WHATDBG: OpenProcess failed, PID=%lu error=%lu\n",
                      static_cast<unsigned long> (processId), GetLastError ());
        }
    }
}

// ---------------------------------------------------------------------------
// Session::loadModuleSymbols / forceReloadAllSymbols
// ---------------------------------------------------------------------------

HRESULT Session::loadModuleSymbols (const juce::String& imageName) noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        const juce::String basename { juce::File (imageName).getFileName () };
        const juce::String command { ".reload /f " + basename.quoted () };
        result = control->Execute (DEBUG_OUTCTL_IGNORE,
                                   command.toRawUTF8 (),
                                   DEBUG_EXECUTE_NOT_LOGGED);
        logWrite ("WHATDBG: .reload /f %s hr=0x%08lX\n",
                  basename.toRawUTF8 (),
                  static_cast<unsigned long> (result));
    }

    return result;
}

HRESULT Session::forceReloadAllSymbols () noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        result = control->Execute (DEBUG_OUTCTL_IGNORE,
                                   ".reload /f",
                                   DEBUG_EXECUTE_NOT_LOGGED);
        logWrite ("WHATDBG: .reload /f (all) hr=0x%08lX\n", static_cast<unsigned long> (result));
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::getOffsetByLine / getLineByOffset
// ---------------------------------------------------------------------------

HRESULT Session::getOffsetByLine (const juce::String& filePath, ULONG line, ULONG64* outOffset) noexcept
{
    HRESULT result { E_FAIL };

    if (symbols != nullptr)
    {
        result = symbols->GetOffsetByLine (line, filePath.toRawUTF8 (), outOffset);
    }

    return result;
}

HRESULT Session::getLineByOffset (ULONG64 offset, juce::String& outFilePath, ULONG* outLine) noexcept
{
    HRESULT result { E_FAIL };

    if (symbols != nullptr)
    {
        char pathBuffer[MAX_PATH] {};
        ULONG pathSize { 0 };

        result = symbols->GetLineByOffset (offset, outLine, pathBuffer, MAX_PATH, &pathSize, nullptr);

        if (SUCCEEDED (result))
        {
            outFilePath = juce::String (pathBuffer);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::addBreakpoint / removeBreakpoint
// ---------------------------------------------------------------------------

HRESULT Session::addBreakpoint (ULONG64 offset, ULONG* outEngineId) noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        IDebugBreakpoint2* bp { nullptr };
        result = control->AddBreakpoint2 (DEBUG_BREAKPOINT_CODE, DEBUG_ANY_ID, &bp);

        if (SUCCEEDED (result) and bp != nullptr)
        {
            bp->SetOffset (offset);
            bp->AddFlags (DEBUG_BREAKPOINT_ENABLED);
            bp->GetId (outEngineId);
        }
    }

    return result;
}

HRESULT Session::removeBreakpoint (ULONG engineId) noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        IDebugBreakpoint2* bp { nullptr };
        result = control->GetBreakpointById2 (engineId, &bp);

        if (SUCCEEDED (result) and bp != nullptr)
        {
            result = control->RemoveBreakpoint2 (bp);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::resetSymbolGroupCache / getOrCreateSymbolGroup
// ---------------------------------------------------------------------------

void Session::resetSymbolGroupCache () noexcept
{
    if (cachedSymbolGroup != nullptr)
    {
        cachedSymbolGroup->Release ();
        cachedSymbolGroup = nullptr;
    }

    cachedFrameIndex = -1;
}

IDebugSymbolGroup2* Session::getOrCreateSymbolGroup (int frameIndex) noexcept
{
    IDebugSymbolGroup2* result { nullptr };

    if (symbols != nullptr)
    {
        if (cachedSymbolGroup != nullptr and cachedFrameIndex == frameIndex)
        {
            result = cachedSymbolGroup;
        }
        else
        {
            if (cachedSymbolGroup != nullptr)
            {
                cachedSymbolGroup->Release ();
                cachedSymbolGroup = nullptr;
            }

            symbols->SetScopeFrameByIndex (static_cast<ULONG> (frameIndex));

            IDebugSymbolGroup2* group { nullptr };
            const HRESULT hr { symbols->GetScopeSymbolGroup2 (
                DEBUG_SCOPE_GROUP_ALL, nullptr, &group) };

            if (SUCCEEDED (hr) and group != nullptr)
            {
                cachedSymbolGroup = group;
                cachedFrameIndex  = frameIndex;
                result = cachedSymbolGroup;
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Session::getThreads
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Session::getEventThreadSystemId
// ---------------------------------------------------------------------------

ULONG Session::getEventThreadSystemId () noexcept
{
    ULONG systemId { 0 };

    if (systemObjects != nullptr)
    {
        ULONG eventEngineId { 0 };
        const HRESULT hr { systemObjects->GetEventThread (&eventEngineId) };

        if (SUCCEEDED (hr))
        {
            ULONG savedId { 0 };
            systemObjects->GetCurrentThreadId (&savedId);
            systemObjects->SetCurrentThreadId (eventEngineId);
            systemObjects->GetCurrentThreadSystemId (&systemId);
            systemObjects->SetCurrentThreadId (savedId);
        }
    }

    return systemId;
}

// ---------------------------------------------------------------------------
// Session::setCurrentThreadBySystemId
// ---------------------------------------------------------------------------

void Session::setCurrentThreadBySystemId (ULONG systemId) noexcept
{
    if (systemObjects != nullptr and systemId != 0)
    {
        ULONG engineId { 0 };
        const HRESULT hr { systemObjects->GetThreadIdBySystemId (systemId, &engineId) };

        if (SUCCEEDED (hr))
        {
            systemObjects->SetCurrentThreadId (engineId);
        }
    }
}

} // namespace debug
