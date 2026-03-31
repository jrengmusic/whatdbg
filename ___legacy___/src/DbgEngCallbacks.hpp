#pragma once

#include <atomic>
#include <windows.h>
#include <dbgeng.h>

#include "DapProtocol.hpp"

class BreakpointManager;
class DbgEngSession;

// ---------------------------------------------------------------------------
// DbgEngCallbacks
//
// Two COM callback classes registered with dbgeng:
//
//   EventCallbacks   — implements IDebugEventCallbacks
//                      Receives debug events (breakpoint, exception, thread,
//                      module, process lifecycle).
//
//   OutputCallbacks  — implements IDebugOutputCallbacks2
//                      Receives all debugger output (OutputDebugString,
//                      engine messages, DML).
//
// Phase 2b: all callbacks are stubs that log to stderr.
// DAP event forwarding is wired in Phase 3 / Phase 5.
//
// Lifetime contract:
//   - Both objects are constructed with a DapProtocol reference.
//   - The DapProtocol reference must outlive both callback objects.
//   - refCount starts at 1; the owner holds the initial reference.
//   - Register with IDebugClient5::SetEventCallbacks /
//     SetOutputCallbacks2 — dbgeng will AddRef internally.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// EventCallbacks — IDebugEventCallbacks
// ---------------------------------------------------------------------------

class EventCallbacks : public IDebugEventCallbacks
{
public:
    explicit EventCallbacks (DapProtocol& protocol);

    // Set the BreakpointManager to use for Breakpoint and LoadModule callbacks.
    // Must be called before the first debug event fires.
    // The pointer must remain valid for the lifetime of this object.
    void setBreakpointManager (BreakpointManager* manager);

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, PVOID* ppv);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // IDebugEventCallbacks
    STDMETHOD(GetInterestMask)(PULONG mask);
    STDMETHOD(Breakpoint)(PDEBUG_BREAKPOINT bp);
    STDMETHOD(Exception)(PEXCEPTION_RECORD64 exception, ULONG firstChance);
    STDMETHOD(CreateThread)(ULONG64 handle, ULONG64 dataOffset, ULONG64 startOffset);
    STDMETHOD(ExitThread)(ULONG exitCode);
    STDMETHOD(CreateProcess)(ULONG64 imageFileHandle,
                             ULONG64 handle,
                             ULONG64 baseOffset,
                             ULONG   moduleSize,
                             PCSTR   moduleName,
                             PCSTR   imageName,
                             ULONG   checkSum,
                             ULONG   timeDateStamp,
                             ULONG64 initialThreadHandle,
                             ULONG64 threadDataOffset,
                             ULONG64 startOffset);
    STDMETHOD(ExitProcess)(ULONG exitCode);
    STDMETHOD(LoadModule)(ULONG64 imageFileHandle,
                          ULONG64 baseOffset,
                          ULONG   moduleSize,
                          PCSTR   moduleName,
                          PCSTR   imageName,
                          ULONG   checkSum,
                          ULONG   timeDateStamp);
    STDMETHOD(UnloadModule)(PCSTR imageBaseName, ULONG64 baseOffset);
    STDMETHOD(SystemError)(ULONG error, ULONG level);
    STDMETHOD(SessionStatus)(ULONG status);
    STDMETHOD(ChangeDebuggeeState)(ULONG flags, ULONG64 argument);
    STDMETHOD(ChangeEngineState)(ULONG flags, ULONG64 argument);
    STDMETHOD(ChangeSymbolState)(ULONG flags, ULONG64 argument);

    void setSystemObjects (IDebugSystemObjects4* sysObj);
    void setSession (DbgEngSession* sess);

    // Check and clear the module-loaded flag.
    // Main loop calls this after WaitForEvent to know if pending BPs should be re-resolved.
    bool consumeModuleLoadFlag ();

    std::optional<nlohmann::json> consumeBreakpointStop ();
    std::optional<int>            consumeExitEvent ();

private:
    DapProtocol&           protocol;
    BreakpointManager*     breakpointManager { nullptr };
    IDebugSystemObjects4*  systemObjects { nullptr };
    DbgEngSession*         session { nullptr };
    std::atomic<bool>      hasNewModuleLoaded { false };
    bool                   isInitialBreakpointSeen { false };
    std::optional<nlohmann::json> pendingStoppedBody {};
    std::optional<int>            pendingExitCode {};
    ULONG                  refCount { 1 };
};

// ---------------------------------------------------------------------------
// OutputCallbacks — IDebugOutputCallbacks2
// ---------------------------------------------------------------------------

class OutputCallbacks : public IDebugOutputCallbacks2
{
public:
    explicit OutputCallbacks (DapProtocol& protocol);

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, PVOID* ppv);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // IDebugOutputCallbacks (base interface — plain text path)
    STDMETHOD(Output)(ULONG mask, PCSTR text);

    // IDebugOutputCallbacks2 (extended — DML + wide text path)
    STDMETHOD(GetInterestMask)(PULONG mask);
    STDMETHOD(Output2)(ULONG which, ULONG flags, ULONG64 arg, PCWSTR text);

private:
    DapProtocol& protocol;
    ULONG        refCount { 1 };
};
