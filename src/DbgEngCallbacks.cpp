#include "DbgEngCallbacks.hpp"
#include "BreakpointManager.hpp"
#include "DbgEngSession.hpp"
#include "DapTypes.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <windows.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

// ---------------------------------------------------------------------------
// Interest mask constants — all events we want dbgeng to deliver
// ---------------------------------------------------------------------------

static constexpr ULONG kEventInterestMask {
    DEBUG_EVENT_BREAKPOINT            |
    DEBUG_EVENT_EXCEPTION             |
    DEBUG_EVENT_CREATE_THREAD         |
    DEBUG_EVENT_EXIT_THREAD           |
    DEBUG_EVENT_CREATE_PROCESS        |
    DEBUG_EVENT_EXIT_PROCESS          |
    DEBUG_EVENT_LOAD_MODULE           |
    DEBUG_EVENT_UNLOAD_MODULE         |
    DEBUG_EVENT_SYSTEM_ERROR          |
    DEBUG_EVENT_SESSION_STATUS        |
    DEBUG_EVENT_CHANGE_DEBUGGEE_STATE |
    DEBUG_EVENT_CHANGE_ENGINE_STATE   |
    DEBUG_EVENT_CHANGE_SYMBOL_STATE
};

// Convert a wide string to UTF-8 using the Windows API.
// Returns an empty string if wide is null or conversion fails.
std::string wideToUtf8 (const wchar_t* wide)
{
    std::string result;

    if (wide != nullptr)
    {
        int requiredBytes { WideCharToMultiByte (
            CP_UTF8,
            0,
            wide,
            -1,       // null-terminated input
            nullptr,
            0,
            nullptr,
            nullptr) };

        if (requiredBytes > 0)
        {
            result.resize (static_cast<size_t> (requiredBytes));
            WideCharToMultiByte (
                CP_UTF8,
                0,
                wide,
                -1,
                &result[0],
                requiredBytes,
                nullptr,
                nullptr);

            // WideCharToMultiByte includes the null terminator in the count
            // when given -1 as the input length; strip it from the std::string.
            if (result.empty () == false and result.back () == '\0')
            {
                result.pop_back ();
            }
        }
    }

    return result;
}

// Map a dbgeng output mask to a DAP output category string.
// Used by OutputCallbacks::Output2.
const char* outputMaskToCategory (ULONG mask)
{
    const char* category { "console" };

    if ((mask & DEBUG_OUTPUT_ERROR) != 0)
    {
        category = "stderr";
    }

    return category;
}

} // anonymous namespace

// ===========================================================================
// EventCallbacks
// ===========================================================================

EventCallbacks::EventCallbacks (DapProtocol& proto)
    : protocol (proto)
{
}

// ---------------------------------------------------------------------------
// EventCallbacks::setBreakpointManager
// ---------------------------------------------------------------------------

void EventCallbacks::setBreakpointManager (BreakpointManager* manager)
{
    breakpointManager = manager;
}

// ---------------------------------------------------------------------------
// EventCallbacks::setSystemObjects
// ---------------------------------------------------------------------------

void EventCallbacks::setSystemObjects (IDebugSystemObjects4* sysObj)
{
    systemObjects = sysObj;
}

void EventCallbacks::setSession (DbgEngSession* sess)
{
    session = sess;
}

bool EventCallbacks::consumeModuleLoadFlag ()
{
    return hasNewModuleLoaded.exchange (false);
}

std::optional<nlohmann::json> EventCallbacks::consumeBreakpointStop ()
{
    auto result { std::move (pendingStoppedBody) };
    pendingStoppedBody.reset ();
    return result;
}

std::optional<int> EventCallbacks::consumeExitEvent ()
{
    auto result { pendingExitCode };
    pendingExitCode.reset ();
    return result;
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE EventCallbacks::QueryInterface (REFIID iid, PVOID* ppv)
{
    HRESULT hr { E_NOINTERFACE };

    if (ppv != nullptr)
    {
        *ppv = nullptr;

        if (iid == __uuidof (IUnknown) or iid == __uuidof (IDebugEventCallbacks))
        {
            *ppv = static_cast<IDebugEventCallbacks*> (this);
            AddRef ();
            hr = S_OK;
        }
    }

    return hr;
}

ULONG STDMETHODCALLTYPE EventCallbacks::AddRef ()
{
    return ++refCount;
}

ULONG STDMETHODCALLTYPE EventCallbacks::Release ()
{
    ULONG count { --refCount };

    if (count == 0)
    {
        delete this;
    }

    return count;
}

// ---------------------------------------------------------------------------
// IDebugEventCallbacks
// ---------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE EventCallbacks::GetInterestMask (PULONG mask)
{
    if (mask != nullptr)
    {
        *mask = kEventInterestMask;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::Breakpoint (PDEBUG_BREAKPOINT bp)
{
    fprintf (stderr, "WHATDBG: [event] breakpoint hit\n");

    if (breakpointManager != nullptr and bp != nullptr)
    {
        IDebugBreakpoint2* bp2 { nullptr };

        HRESULT hrQi { bp->QueryInterface (__uuidof (IDebugBreakpoint2),
                                           reinterpret_cast<void**> (&bp2)) };

        if (SUCCEEDED (hrQi) and bp2 != nullptr)
        {
            ULONG threadId { 0 };

            if (systemObjects != nullptr)
            {
                systemObjects->GetCurrentThreadId (&threadId);
            }

            pendingStoppedBody = breakpointManager->onBreakpointHit (bp2, threadId);

            bp2->Release ();
        }
    }

    return DEBUG_STATUS_BREAK;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::Exception (PEXCEPTION_RECORD64 exception,
                                                      ULONG               firstChance)
{
    ULONG exceptionCode { 0 };

    if (exception != nullptr)
    {
        exceptionCode = exception->ExceptionCode;
    }

    // Exceptions the debugger must handle — never pass to the app.
    static constexpr ULONG kBreakpointException    { 0x80000003 };  // INT3
    static constexpr ULONG kSingleStepException    { 0x80000004 };  // single-step trap
    static constexpr ULONG kSetThreadNameException  { 0x406D1388 };  // MS_VC_EXCEPTION

    // The initial loader breakpoint (ntdll!LdrpDoDebuggerBreak) is the
    // first INT3 that fires after process creation.  dbgeng's symbol engine
    // only becomes functional after this event is processed.  We track it
    // with isInitialBreakpointSeen so we can log the transition.
    if (exceptionCode == kBreakpointException and isInitialBreakpointSeen == false)
    {
        isInitialBreakpointSeen = true;
        fprintf (stderr, "WHATDBG: [event] initial loader breakpoint — symbol engine ready\n");
        return DEBUG_STATUS_BREAK;
    }

    // Subsequent breakpoints and single-step traps are debugger-owned.
    // Passing them through as "not handled" crashes the target.
    if (exceptionCode == kBreakpointException or exceptionCode == kSingleStepException)
    {
        return DEBUG_STATUS_BREAK;
    }

    // SetThreadName — harmless, let the app continue.
    if (exceptionCode == kSetThreadNameException)
    {
        return DEBUG_STATUS_GO_NOT_HANDLED;
    }

    fprintf (stderr,
            "WHATDBG: [event] exception 0x%08X (first chance: %d)\n",
            exceptionCode,
            static_cast<int> (firstChance));

    // First-chance exceptions: let the app handle them.
    // Second-chance (unhandled): break into debugger.
    if (firstChance == 1)
    {
        return DEBUG_STATUS_GO_NOT_HANDLED;
    }

    return DEBUG_STATUS_BREAK;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::CreateThread (ULONG64 /*handle*/,
                                                         ULONG64 /*dataOffset*/,
                                                         ULONG64 /*startOffset*/)
{
    fprintf (stderr, "WHATDBG: [event] thread created\n");
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::ExitThread (ULONG exitCode)
{
    fprintf (stderr, "WHATDBG: [event] thread exited (%u)\n", exitCode);
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::CreateProcess (ULONG64 /*imageFileHandle*/,
                                                           ULONG64 /*handle*/,
                                                           ULONG64 /*baseOffset*/,
                                                           ULONG   /*moduleSize*/,
                                                           PCSTR   moduleName,
                                                           PCSTR   imageName,
                                                           ULONG   /*checkSum*/,
                                                           ULONG   /*timeDateStamp*/,
                                                           ULONG64 /*initialThreadHandle*/,
                                                           ULONG64 /*threadDataOffset*/,
                                                           ULONG64 /*startOffset*/)
{
    const char* displayName { "unknown" };

    if (imageName != nullptr)
    {
        displayName = imageName;
    }
    else if (moduleName != nullptr)
    {
        displayName = moduleName;
    }

    fprintf (stderr, "WHATDBG: [event] process created: %s\n", displayName);

    // Do NOT return DEBUG_STATUS_BREAK here.
    //
    // The CreateProcess event fires BEFORE the Windows loader has run.
    // dbgeng's symbol engine only initializes after the initial loader
    // breakpoint (ntdll!LdrpDoDebuggerBreak) is processed.  Returning
    // BREAK here stops the target before that breakpoint fires, leaving
    // the symbol engine in a "partially initialized" state where
    // GetOffsetByLine and Reload always fail with E_UNEXPECTED.
    //
    // Returning NO_CHANGE lets the engine continue running the target
    // to the initial breakpoint.  The Exception callback will return
    // DEBUG_STATUS_BREAK for that INT3, which is where the target
    // actually stops and the symbol engine becomes functional.
    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::ExitProcess (ULONG exitCode)
{
    fprintf (stderr, "WHATDBG: [event] process exited (%u)\n", exitCode);

    if (session != nullptr)
    {
        session->clearTarget ();
    }

    pendingExitCode = static_cast<int> (exitCode);

    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::LoadModule (ULONG64 /*imageFileHandle*/,
                                                       ULONG64 /*baseOffset*/,
                                                       ULONG   /*moduleSize*/,
                                                       PCSTR   moduleName,
                                                       PCSTR   imageName,
                                                       ULONG   /*checkSum*/,
                                                       ULONG   /*timeDateStamp*/)
{
    const char* displayName { "unknown" };

    if (imageName != nullptr)
    {
        displayName = imageName;
    }
    else if (moduleName != nullptr)
    {
        displayName = moduleName;
    }

    fprintf (stderr, "WHATDBG: [event] module loaded: %s\n", displayName);

    // Set flag so the main loop knows to re-resolve pending breakpoints.
    hasNewModuleLoaded.store (true);

    // If there are pending breakpoints, stop the target here so the main loop
    // can safely call GetOffsetByLine and AddBreakpoint2.
    //
    // Symbol APIs (GetOffsetByLine, Reload) require the target to be stopped —
    // they return E_UNEXPECTED when called while the target is running.
    // Returning DEBUG_STATUS_BREAK from LoadModule is the correct way to stop
    // the target at module-load time; dbgeng delivers this as a break event and
    // WaitForEvent returns S_OK with execStatus == DEBUG_STATUS_BREAK.
    //
    // Do NOT call AddBreakpoint2 or tryResolve here — dbgeng is not re-entrant
    // from within an event callback.
    if (breakpointManager != nullptr and breakpointManager->hasPending ())
    {
        fprintf (stderr, "WHATDBG: pending BPs exist — breaking at module load to resolve\n");
        return DEBUG_STATUS_BREAK;
    }

    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::UnloadModule (PCSTR   imageBaseName,
                                                         ULONG64 /*baseOffset*/)
{
    fprintf (stderr,
            "WHATDBG: [event] module unloaded: %s\n",
            imageBaseName != nullptr ? imageBaseName : "unknown");

    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::SystemError (ULONG error, ULONG level)
{
    fprintf (stderr,
            "WHATDBG: [event] system error 0x%08X (level %u)\n",
            error,
            level);

    return DEBUG_STATUS_NO_CHANGE;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::SessionStatus (ULONG status)
{
    fprintf (stderr, "WHATDBG: [event] session status %u\n", status);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::ChangeDebuggeeState (ULONG   /*flags*/,
                                                                ULONG64 /*argument*/)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::ChangeEngineState (ULONG   /*flags*/,
                                                              ULONG64 /*argument*/)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EventCallbacks::ChangeSymbolState (ULONG   /*flags*/,
                                                              ULONG64 /*argument*/)
{
    return S_OK;
}

// ===========================================================================
// OutputCallbacks
// ===========================================================================

OutputCallbacks::OutputCallbacks (DapProtocol& proto)
    : protocol (proto)
{
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE OutputCallbacks::QueryInterface (REFIID iid, PVOID* ppv)
{
    HRESULT hr { E_NOINTERFACE };

    if (ppv != nullptr)
    {
        *ppv = nullptr;

        if (iid == __uuidof (IUnknown)                  or
            iid == __uuidof (IDebugOutputCallbacks)      or
            iid == __uuidof (IDebugOutputCallbacks2))
        {
            *ppv = static_cast<IDebugOutputCallbacks2*> (this);
            AddRef ();
            hr = S_OK;
        }
    }

    return hr;
}

ULONG STDMETHODCALLTYPE OutputCallbacks::AddRef ()
{
    return ++refCount;
}

ULONG STDMETHODCALLTYPE OutputCallbacks::Release ()
{
    ULONG count { --refCount };

    if (count == 0)
    {
        delete this;
    }

    return count;
}

// ---------------------------------------------------------------------------
// IDebugOutputCallbacks
// ---------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE OutputCallbacks::Output (ULONG /*mask*/, PCSTR text)
{
    fprintf (stderr, "WHATDBG: [output] %s", text != nullptr ? text : "");
    return S_OK;
}

// ---------------------------------------------------------------------------
// IDebugOutputCallbacks2
// ---------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE OutputCallbacks::GetInterestMask (PULONG mask)
{
    if (mask != nullptr)
    {
        *mask = DEBUG_OUTCBI_ANY_FORMAT;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE OutputCallbacks::Output2 (ULONG   which,
                                                     ULONG   flags,
                                                     ULONG64 /*arg*/,
                                                     PCWSTR  text)
{
    const char* category { outputMaskToCategory (which) };

    // Suppress the unused-parameter warning for flags in Phase 2b.
    // flags will be used in Phase 5 when DML stripping is implemented.
    (void)flags;

    fprintf (stderr,
            "WHATDBG: [output2] [%s] %ls\n",
            category,
            text != nullptr ? text : L"");

    return S_OK;
}
