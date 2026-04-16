#include <JuceHeader.h>
#include "Callbacks.h"
#include "State.h"
#include "../Log.h"

static constexpr DWORD MS_VC_EXCEPTION { 0x406D1388 };

//==============================================================================
// Exception handlers
//==============================================================================

using ExceptionHandler = HRESULT (*) (debug::State*, PEXCEPTION_RECORD64, ULONG firstChance);

static HRESULT handleBreakpoint (debug::State* state, PEXCEPTION_RECORD64 /*exception*/, ULONG /*firstChance*/)
{
    if (state->initialBreakPhase == debug::InitialBreakPhase::notHit)
        state->initialBreakPhase = debug::InitialBreakPhase::pending;

    state->executionState = debug::ExecutionState::stopped;
    return DEBUG_STATUS_BREAK;
}

static HRESULT handleThreadName (debug::State* /*state*/, PEXCEPTION_RECORD64 /*exception*/, ULONG /*firstChance*/)
{
    return DEBUG_STATUS_GO_NOT_HANDLED;
}

static HRESULT handleUnknownException (debug::State* state, PEXCEPTION_RECORD64 exception, ULONG firstChance)
{
    HRESULT result { DEBUG_STATUS_BREAK };

    if (firstChance != 0)
    {
        result = DEBUG_STATUS_GO_NOT_HANDLED;
    }
    else
    {
        if (state != nullptr and exception != nullptr)
        {
            state->hasExceptionStopped = true;
            state->exceptionCode       = static_cast<std::uint32_t> (exception->ExceptionCode);
            state->exceptionAddress    = static_cast<std::uint64_t> (exception->ExceptionAddress);
            state->executionState      = debug::ExecutionState::stopped;
        }
    }

    return result;
}

static const std::unordered_map<DWORD, ExceptionHandler> exceptionHandlers
{
    { EXCEPTION_BREAKPOINT, handleBreakpoint  },
    { MS_VC_EXCEPTION,      handleThreadName  },
};

static const std::unordered_map<DWORD, const char*> exceptionNames
{
    { 0xC0000005, "ACCESS_VIOLATION"            },
    { 0xC000001D, "ILLEGAL_INSTRUCTION"         },
    { 0xC0000025, "NONCONTINUABLE_EXCEPTION"    },
    { 0xC0000026, "INVALID_DISPOSITION"         },
    { 0xC000008C, "ARRAY_BOUNDS_EXCEEDED"       },
    { 0xC000008D, "FLOAT_DENORMAL_OPERAND"      },
    { 0xC000008E, "FLOAT_DIVIDE_BY_ZERO"        },
    { 0xC000008F, "FLOAT_INEXACT_RESULT"        },
    { 0xC0000090, "FLOAT_INVALID_OPERATION"     },
    { 0xC0000091, "FLOAT_OVERFLOW"              },
    { 0xC0000092, "FLOAT_STACK_CHECK"           },
    { 0xC0000093, "FLOAT_UNDERFLOW"             },
    { 0xC0000094, "INTEGER_DIVIDE_BY_ZERO"      },
    { 0xC0000095, "INTEGER_OVERFLOW"            },
    { 0xC0000096, "PRIV_INSTRUCTION"            },
    { 0xC00000FD, "STACK_OVERFLOW"              },
    { 0xE06D7363, "CPP_EXCEPTION"               },
};

namespace debug
{

juce::String getExceptionName (std::uint32_t code) noexcept
{
    juce::String result { juce::String ("0x") + juce::String::toHexString (static_cast<juce::int64> (code)) };

    const auto entry { exceptionNames.find (static_cast<DWORD> (code)) };

    if (entry != exceptionNames.end ())
        result = juce::String (entry->second);

    return result;
}

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

HRESULT OutputCallbacks::Output2 (ULONG which, ULONG flags, ULONG64 arg, PCWSTR text)
{
    juce::ignoreUnused (flags);

    const bool isText { which == DEBUG_OUTCB_TEXT };

    if (isText and text != nullptr)
    {
        const ULONG mask { static_cast<ULONG> (arg) };
        const bool isDebuggeeOutput { (mask & DEBUG_OUTPUT_DEBUGGEE) != 0 };

        if (isDebuggeeOutput)
        {
            auto* state { State::getContext () };
            state->debuggeeOutputText += juce::String (text);
            state->hasDebuggeeOutput = true;
        }
    }

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

    const auto entry { exceptionHandlers.find (exception->ExceptionCode) };
    const ExceptionHandler handler { entry != exceptionHandlers.end ()
                                         ? entry->second
                                         : handleUnknownException };

    return handler (State::getContext (), exception, firstChance);
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
    logWrite ("WHATDBG: ExitProcess: code=%lu\n", static_cast<unsigned long> (exitCode));
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
