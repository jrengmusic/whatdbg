/** @file Callbacks.cpp
 *  @brief Windows dbgeng COM callback implementations and the exception-code table.
 *
 *  Implements the IDebugEventCallbacks, IDebugOutputCallbacks, and IDebugOutputCallbacks2
 *  methods declared in Callbacks.h, the exception handlers that EventCallbacks::Exception
 *  dispatches to, and getExceptionName. dbgeng invokes the callbacks during WaitForEvent
 *  on the engine context. They fire events on Session::events. None writes to stdout.
 */

#include <JuceHeader.h>
#include "State.h"

#if JUCE_WINDOWS
#include "Callbacks.h"

static constexpr DWORD msvcThreadNameException { 0x406D1388 };

//==============================================================================
// Exception callbacks
//==============================================================================

static HRESULT onBreakpoint (const DWORD& /*exceptionCode*/, WHATDBGModel* model, debug::Events& events, PEXCEPTION_RECORD64 exception, ULONG /*firstChance*/)
{
    if (static_cast<debug::ExecutionState> (model->getParameter<jam::Parameter<int>> (model->sessionTag, Id::executionState)->getValue ()) == debug::ExecutionState::launching)
    {
        events.get (Id::initialBreak);
    }
    else if (model->getParameter<jam::Parameter<int>> (model->sessionTag, Id::isPausePending)->getValue () != 0)
    {
        events.get (Id::pause);
    }
    else
    {
        events.get (Id::exception,
                    static_cast<juce::int64> (static_cast<std::uint32_t> (exception->ExceptionCode)),
                    static_cast<juce::int64> (exception->ExceptionAddress),
                    0);
    }

    return DEBUG_STATUS_BREAK;
}

static HRESULT onThreadName (const DWORD& /*exceptionCode*/, WHATDBGModel* /*model*/, debug::Events& /*events*/, PEXCEPTION_RECORD64 /*exception*/, ULONG /*firstChance*/)
{
    return DEBUG_STATUS_GO_NOT_HANDLED;
}

static HRESULT onUnknownException (const DWORD& /*exceptionCode*/, WHATDBGModel* /*model*/, debug::Events& events, PEXCEPTION_RECORD64 exception, ULONG firstChance)
{
    HRESULT result { DEBUG_STATUS_BREAK };

    if (firstChance != 0)
    {
        result = DEBUG_STATUS_GO_NOT_HANDLED;
    }
    else
    {
        events.get (Id::exception,
                    static_cast<juce::int64> (static_cast<std::uint32_t> (exception->ExceptionCode)),
                    static_cast<juce::int64> (exception->ExceptionAddress),
                    0);
    }

    return result;
}

namespace debug
{

juce::String getExceptionName (std::uint32_t code, bool isMachException) noexcept
{
    juce::ignoreUnused (isMachException);

    static const jam::HashMap<DWORD, const char*> exceptionNames
    {
        { 0xC0000005, "ACCESS_VIOLATION"            },
        { 0x80000003, "BREAKPOINT"                  },
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

    juce::String result { juce::String ("0x") + juce::String::toHexString (static_cast<juce::int64> (code)) };

    const auto exceptionEntry { exceptionNames.find (static_cast<DWORD> (code)) };

    if (exceptionEntry != exceptionNames.end ())
    {
        const auto& [exceptionCode, exceptionName] { *exceptionEntry };
        result = juce::String (exceptionName);
    }

    return result;
}

OutputCallbacks::OutputCallbacks (Events& newEvents)
    : events { newEvents }
{
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
    return --refCount;
}

HRESULT OutputCallbacks::QueryInterface (REFIID interfaceId, PVOID* outInterface)
{
    HRESULT result { E_NOINTERFACE };
    *outInterface = nullptr;

    if (interfaceId == IID_IUnknown)
    {
        *outInterface = static_cast<IUnknown*> (static_cast<IDebugOutputCallbacks2*> (this));
        AddRef ();
        result = S_OK;
    }
    else if (interfaceId == IID_IDebugOutputCallbacks)
    {
        *outInterface = static_cast<IDebugOutputCallbacks*> (this);
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
#if JUCE_DEBUG
    jam::debug::Log::write (juce::String (text));
#endif
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
        const bool isDebuggeeOutput { (mask bitand DEBUG_OUTPUT_DEBUGGEE) != 0 };

        if (isDebuggeeOutput)
        {
            events.get (Id::console, juce::String (text));
        }
    }

    return S_OK;
}

EventCallbacks::EventCallbacks (Events& newEvents)
    : events { newEvents }
{
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
    return --refCount;
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
    jassert (bp != nullptr);

    ULONG engineId { 0 };
    bp->GetId (&engineId);

    events.get (Id::breakpoint, static_cast<int> (engineId));

#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: Breakpoint hit, engineId=" + juce::String (engineId));
#endif

    return DEBUG_STATUS_BREAK;
}

HRESULT EventCallbacks::Exception (PEXCEPTION_RECORD64 exception, ULONG firstChance)
{
#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: Exception code=0x"
                             + juce::String::toHexString (static_cast<unsigned long> (exception->ExceptionCode))
                             + " firstChance=" + juce::String (static_cast<unsigned long> (firstChance)));
#endif

    static const auto exceptions ([] {
        jam::Function::Map<DWORD, jam::Function::pointerCapacity, HRESULT (WHATDBGModel*, debug::Events&, PEXCEPTION_RECORD64, ULONG)> exceptions;

        exceptions.add (EXCEPTION_BREAKPOINT,    onBreakpoint);
        exceptions.add (msvcThreadNameException, onThreadName);
        return exceptions;
    } ());

    const auto exceptionEntry { exceptions.find (exception->ExceptionCode) };

    if (exceptionEntry != exceptions.end ())
    {
        const auto& [exceptionKey, exceptionCallback] { *exceptionEntry };
        return exceptionCallback (exceptionKey, WHATDBGModel::getInstance (), events, exception, firstChance);
    }

    return onUnknownException (exception->ExceptionCode, WHATDBGModel::getInstance (), events, exception, firstChance);
}

HRESULT EventCallbacks::CreateThread (ULONG64 /*handle*/, ULONG64 /*dataOffset*/, ULONG64 /*startOffset*/)
{
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::ExitThread (ULONG /*exitCode*/)
{
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

    events.get (Id::targetProcessId, static_cast<int> (pid));

#if JUCE_DEBUG
    jam::debug::Log::write (juce::String ("WHATDBG: CreateProcess: ")
                             + (moduleName != nullptr ? moduleName : "(null)")
                             + " (PID=" + juce::String (static_cast<unsigned long> (pid)) + ")");
#endif

    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::ExitProcess (ULONG exitCode)
{
#if JUCE_DEBUG
    jam::debug::Log::write ("WHATDBG: ExitProcess: code=" + juce::String (static_cast<unsigned long> (exitCode)));
#endif
    events.get (Id::exited, static_cast<int> (exitCode));
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::LoadModule (ULONG64 /*imageFileHandle*/, ULONG64 /*baseOffset*/,
                                    ULONG /*moduleSize*/, PCSTR moduleName, PCSTR imageName,
                                    ULONG /*checkSum*/, ULONG /*timeDateStamp*/)
{
    auto* model { WHATDBGModel::getInstance () };
#if JUCE_DEBUG
    jam::debug::Log::write (juce::String ("WHATDBG: LoadModule: ")
                             + (moduleName != nullptr ? moduleName : "(null)")
                             + " (" + (imageName != nullptr ? imageName : "(null)") + ")");
#endif

    if (model->getParameter<jam::Parameter<int>> (model->sessionTag, Id::isStepPending)->getValue () != 0)
        return DEBUG_STATUS_NO_CHANGE;

    events.get (Id::moduleLoad, imageName != nullptr ? juce::String (imageName) : juce::String ());
    return DEBUG_STATUS_BREAK;
}

HRESULT EventCallbacks::UnloadModule (PCSTR /*imageBaseName*/, ULONG64 /*baseOffset*/)
{
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::SystemError (ULONG /*error*/, ULONG /*level*/)
{
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT EventCallbacks::SessionStatus (ULONG /*status*/)
{
    return S_OK;
}

HRESULT EventCallbacks::ChangeDebuggeeState (ULONG /*flags*/, ULONG64 /*argument*/)
{
    return S_OK;
}

HRESULT EventCallbacks::ChangeEngineState (ULONG /*flags*/, ULONG64 /*argument*/)
{
    return S_OK;
}

HRESULT EventCallbacks::ChangeSymbolState (ULONG /*flags*/, ULONG64 /*argument*/)
{
    return S_OK;
}

} // namespace debug

#endif
