#include <JuceHeader.h>
#include "Session.h"
#include "../Log.h"
#include <dbghelp.h>

namespace debug
{

using DynObj = juce::ReferenceCountedObjectPtr<juce::DynamicObject>;

Session::~Session ()
{
    shutdown ();
}

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

    const bool isInitialized { client != nullptr and control != nullptr and symbols != nullptr };

    if (not isInitialized)
    {
        shutdown ();
        logWrite ("WHATDBG: initialization failed\n");
    }

    return isInitialized;
}

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

void Session::resume () noexcept
{
    if (control != nullptr)
    {
        control->SetExecutionStatus (DEBUG_STATUS_GO);
    }
}

HRESULT Session::pollEvents (ULONG timeoutMs) noexcept
{
    HRESULT result { E_FAIL };

    if (control != nullptr)
    {
        result = control->WaitForEvent (0, timeoutMs);
    }

    return result;
}

void Session::shutdown () noexcept
{
    if (client != nullptr)
    {
        client->EndSession (DEBUG_END_ACTIVE_DETACH);
    }

    symbols.Reset ();
    control.Reset ();
    client.Reset ();

    if (isComOwned)
    {
        CoUninitialize ();
        isComOwned = false;
    }
}

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

juce::Array<juce::var> Session::getStackTrace (int maxFrames) noexcept
{
    juce::Array<juce::var> frames;

    if (control != nullptr and symbols != nullptr)
    {
        static constexpr int kMaxStackFrames { 128 };
        static constexpr int kNameBufferSize { 512 };
        static constexpr int kFileBufferSize { 1024 };
        const int frameCount { juce::jmin (maxFrames, kMaxStackFrames) };

        std::vector<DEBUG_STACK_FRAME> stackFrames (static_cast<size_t> (frameCount));
        ULONG framesFilled { 0 };

        const HRESULT hr { control->GetStackTrace (
            0, 0, 0,
            stackFrames.data (),
            static_cast<ULONG> (frameCount),
            &framesFilled) };

        if (SUCCEEDED (hr))
        {
            for (ULONG i { 0 }; i < framesFilled; ++i)
            {
                DynObj frame { new juce::DynamicObject () };
                frame->setProperty ("id", static_cast<int> (i));
                frame->setProperty ("name", "frame");

                // Resolve function name
                char nameBuffer[kNameBufferSize] {};
                ULONG nameSize { 0 };
                ULONG64 displacement { 0 };

                const HRESULT nameResult { symbols->GetNameByOffset (
                    stackFrames.at (static_cast<size_t> (i)).InstructionOffset,
                    nameBuffer,
                    kNameBufferSize,
                    &nameSize,
                    &displacement) };

                if (SUCCEEDED (nameResult))
                {
                    frame->setProperty ("name", juce::String (nameBuffer));
                }

                // Resolve source location
                char fileBuffer[kFileBufferSize] {};
                ULONG fileSize { 0 };
                ULONG line { 0 };

                const HRESULT lineResult { symbols->GetLineByOffset (
                    stackFrames.at (static_cast<size_t> (i)).InstructionOffset,
                    &line,
                    fileBuffer,
                    kFileBufferSize,
                    &fileSize,
                    nullptr) };

                if (SUCCEEDED (lineResult))
                {
                    DynObj source { new juce::DynamicObject () };
                    source->setProperty ("name", juce::File (juce::String (fileBuffer)).getFileName ());
                    source->setProperty ("path", juce::String (fileBuffer).replace ("\\", "/"));
                    frame->setProperty ("source", juce::var (source));
                    frame->setProperty ("line", static_cast<int> (line));
                    frame->setProperty ("column", 1);
                }

                frames.add (juce::var (frame));
            }
        }
    }

    return frames;
}

} // namespace debug
