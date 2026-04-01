#include <JuceHeader.h>
#include "Callbacks.h"
#include "State.h"
#include "../Log.h"

static constexpr DWORD MS_VC_EXCEPTION { 0x406D1388 };

namespace debug
{

//==============================================================================
// OutputCallbacks — IUnknown
//==============================================================================

ULONG OutputCallbacks::AddRef ()
{
    return ++refCount;
}

ULONG OutputCallbacks::Release ()
{
    const ULONG remaining { --refCount };
    return remaining;
}

HRESULT OutputCallbacks::QueryInterface (REFIID interfaceId, PVOID* outInterface)
{
    HRESULT result { E_NOINTERFACE };
    *outInterface = nullptr;

    if (interfaceId == IID_IUnknown)
    {
        *outInterface = static_cast<IUnknown*> (this);
        AddRef ();
        result = S_OK;
    }
    else if (interfaceId == IID_IDebugOutputCallbacks)
    {
        *outInterface = reinterpret_cast<IDebugOutputCallbacks*> (this);
        AddRef ();
        result = S_OK;
    }
    else if (interfaceId == IID_IDebugOutputCallbacks2)
    {
        *outInterface = static_cast<IDebugOutputCallbacks2*> (this);
        AddRef ();
        result = S_OK;
    }

    return result;
}

//==============================================================================
// OutputCallbacks — IDebugOutputCallbacks
//==============================================================================

HRESULT OutputCallbacks::Output (ULONG /*mask*/, PCSTR text)
{
    logWrite ("%s", text);
    return S_OK;
}

//==============================================================================
// OutputCallbacks — IDebugOutputCallbacks2
//==============================================================================

HRESULT OutputCallbacks::GetInterestMask (PULONG mask)
{
    *mask = DEBUG_OUTCBI_ANY_FORMAT;
    return S_OK;
}

HRESULT OutputCallbacks::Output2 (ULONG which, ULONG flags, ULONG64 /*arg*/, PCWSTR text)
{
    const bool isTextOrDml { which == DEBUG_OUTCB_TEXT or which == DEBUG_OUTCB_DML };

    juce::ignoreUnused (flags, text, isTextOrDml);
    return S_OK;
}

//==============================================================================
// EventCallbacks — IUnknown
//==============================================================================

ULONG EventCallbacks::AddRef ()
{
    return ++refCount;
}

ULONG EventCallbacks::Release ()
{
    const ULONG remaining { --refCount };
    return remaining;
}

HRESULT EventCallbacks::QueryInterface (REFIID interfaceId, PVOID* outInterface)
{
    HRESULT result { E_NOINTERFACE };
    *outInterface = nullptr;

    if (interfaceId == IID_IUnknown)
    {
        *outInterface = static_cast<IUnknown*> (this);
        AddRef ();
        result = S_OK;
    }
    else if (interfaceId == IID_IDebugEventCallbacks)
    {
        *outInterface = static_cast<IDebugEventCallbacks*> (this);
        AddRef ();
        result = S_OK;
    }

    return result;
}

//==============================================================================
// EventCallbacks — IDebugEventCallbacks
//==============================================================================

HRESULT EventCallbacks::GetInterestMask (PULONG mask)
{
    *mask = DEBUG_EVENT_BREAKPOINT
          | DEBUG_EVENT_EXCEPTION
          | DEBUG_EVENT_CREATE_PROCESS
          | DEBUG_EVENT_EXIT_PROCESS
          | DEBUG_EVENT_LOAD_MODULE;
    return S_OK;
}

HRESULT EventCallbacks::Breakpoint (PDEBUG_BREAKPOINT bp)
{
    auto* state { State::getContext () };

    if (bp != nullptr)
    {
        ULONG engineId { 0 };
        bp->GetId (&engineId);

        state->hasBreakpointHit = true;
        state->breakpointEngineId = engineId;
        state->breakpointThreadId = 1;  // hardcoded: single-thread model, matches handleThreads
        state->executionState = ExecutionState::stopped;

        logWrite ("WHATDBG: Breakpoint hit, engineId=%lu\n", engineId);
    }

    return DEBUG_STATUS_BREAK;
}

HRESULT EventCallbacks::Exception (PEXCEPTION_RECORD64 exception, ULONG firstChance)
{
    logWrite ("WHATDBG: Exception code=0x%08lX firstChance=%lu\n",
              static_cast<unsigned long> (exception->ExceptionCode),
              static_cast<unsigned long> (firstChance));

    HRESULT result { DEBUG_STATUS_NO_CHANGE };

    if (not State::getContext ()->isInitialBreakHandled
        and exception->ExceptionCode == EXCEPTION_BREAKPOINT)
    {
        State::getContext ()->isInitialBreakHandled = true;
        State::getContext ()->isInitialBreakSeen = true;
        State::getContext ()->executionState = ExecutionState::stopped;
        result = DEBUG_STATUS_BREAK;
    }
    else if (exception->ExceptionCode == EXCEPTION_BREAKPOINT)
    {
        State::getContext ()->executionState = ExecutionState::stopped;
        result = DEBUG_STATUS_BREAK;
    }
    else if (exception->ExceptionCode == MS_VC_EXCEPTION)
    {
        result = DEBUG_STATUS_GO_NOT_HANDLED;
    }
    else if (exception->ExceptionCode == EXCEPTION_SINGLE_STEP)
    {
        State::getContext ()->hasStepCompleted = true;
        State::getContext ()->executionState = ExecutionState::stopped;
        result = DEBUG_STATUS_BREAK;
    }
    else if (firstChance != 0)
    {
        result = DEBUG_STATUS_GO_NOT_HANDLED;
    }
    else
    {
        result = DEBUG_STATUS_BREAK;
    }

    return result;
}

HRESULT EventCallbacks::CreateThread (ULONG64 /*handle*/, ULONG64 /*dataOffset*/, ULONG64 /*startOffset*/)
{
    logWrite ("WHATDBG: CreateThread\n");
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::ExitThread (ULONG /*exitCode*/)
{
    logWrite ("WHATDBG: ExitThread\n");
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::CreateProcess (ULONG64 /*imageFileHandle*/, ULONG64 handle,
                                       ULONG64 /*baseOffset*/, ULONG /*moduleSize*/,
                                       PCSTR moduleName, PCSTR /*imageName*/,
                                       ULONG /*checkSum*/, ULONG /*timeDateStamp*/,
                                       ULONG64 /*initialThreadHandle*/, ULONG64 /*threadDataOffset*/,
                                       ULONG64 /*startOffset*/)
{
    const HANDLE processHandle { reinterpret_cast<HANDLE> (static_cast<ULONG_PTR> (handle)) };
    const DWORD pid { GetProcessId (processHandle) };

    State::getContext ()->targetProcessId = static_cast<ULONG> (pid);

    logWrite ("WHATDBG: CreateProcess: %s (PID=%lu)\n",
              moduleName != nullptr ? moduleName : "(null)",
              static_cast<unsigned long> (pid));

    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::ExitProcess (ULONG exitCode)
{
    State::getContext ()->executionState = ExecutionState::exited;
    State::getContext ()->processExitCode = static_cast<int> (exitCode);
    State::getContext ()->hasProcessExited = true;
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::LoadModule (ULONG64 /*imageFileHandle*/, ULONG64 /*baseOffset*/,
                                    ULONG /*moduleSize*/, PCSTR moduleName, PCSTR imageName,
                                    ULONG /*checkSum*/, ULONG /*timeDateStamp*/)
{
    auto* state { State::getContext () };
    state->hasNewModuleLoaded = true;
    state->lastLoadedModuleName = moduleName != nullptr ? juce::String (moduleName) : juce::String ();
    state->lastLoadedImageName = imageName != nullptr ? juce::String (imageName) : juce::String ();
    logWrite ("WHATDBG: LoadModule: %s (%s)\n",
              moduleName != nullptr ? moduleName : "(null)",
              imageName != nullptr ? imageName : "(null)");

    // Stop the target when pending BPs exist so main loop can safely resolve
    HRESULT result { DEBUG_STATUS_NO_CHANGE };

    if (state->hasPendingBreakpoints)
    {
        result = DEBUG_STATUS_BREAK;
    }

    return result;
}

HRESULT EventCallbacks::UnloadModule (PCSTR /*imageBaseName*/, ULONG64 /*baseOffset*/)
{
    logWrite ("WHATDBG: UnloadModule\n");
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::SystemError (ULONG /*error*/, ULONG /*level*/)
{
    logWrite ("WHATDBG: SystemError\n");
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::SessionStatus (ULONG /*status*/)
{
    logWrite ("WHATDBG: SessionStatus\n");
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::ChangeDebuggeeState (ULONG /*flags*/, ULONG64 /*argument*/)
{
    logWrite ("WHATDBG: ChangeDebuggeeState\n");
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::ChangeEngineState (ULONG /*flags*/, ULONG64 /*argument*/)
{
    logWrite ("WHATDBG: ChangeEngineState\n");
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::ChangeSymbolState (ULONG /*flags*/, ULONG64 /*argument*/)
{
    logWrite ("WHATDBG: ChangeSymbolState\n");
    return DEBUG_STATUS_NO_CHANGE;
}

} // namespace debug
