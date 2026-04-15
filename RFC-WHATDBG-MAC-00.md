# RFC-WHATDBG-MAC-00 — macOS Port via liblldb
**Project:** whatdbg  
**Date:** 2026-04-14  
**Status:** Backlog — Ready for COUNSELOR when scheduled  
**Author:** BRAINSTORMER

---

## Problem Statement

whatdbg is Windows-only, backed by the DbgEng COM API. The goal is to add macOS support using `liblldb` as the debug backend while keeping the nvim-dap setup and DX **100% identical** on both platforms. No new DAP protocol surface. No client-visible changes. Platform fork is strictly below `debug::Session`.

---

## Architecture Decision

**Platform-selected implementation files. No abstract base class. No virtual dispatch.**

`Session.h` already defines the complete API contract. CMake selects the correct `.cpp` files per platform. Same header, two implementations. This is the BLESSED-Lean choice — an `ISession` abstract base introduces indirection with no runtime benefit.

```cmake
if (APPLE)
    target_sources (whatdbg_App PRIVATE
        Source/debug/Session_mac.cpp
        Source/debug/SessionInspection_mac.cpp
        Source/debug/SessionPrettyPrint_mac.cpp)
    target_link_libraries (whatdbg_App PRIVATE
        "-framework LLDB"
        "-F/Applications/Xcode.app/Contents/SharedFrameworks")
else()
    target_sources (whatdbg_App PRIVATE
        Source/debug/Session.cpp
        Source/debug/SessionInspection.cpp
        Source/debug/SessionPrettyPrint.cpp
        Source/debug/Loader.cpp
        Source/debug/Callbacks.cpp)
endif()
```

`Loader` and `Callbacks` have no macOS equivalents — they disappear from the macOS build entirely.

---

## Platform Boundary

Everything above `debug::Session` is already cross-platform. Zero changes required:

| Component | Status |
|---|---|
| `Whatdbg.h/.cpp` | Cross-platform. One `#if JUCE_WINDOWS` guard around `_setmode`. |
| `WhatdbgHandlers.cpp` | Cross-platform. No changes. |
| `dap::Reader` | Pure JUCE. No changes. |
| `dap::Types` | Pure JUCE. No changes. |
| `debug::State` | Cross-platform. `ULONG` fields are storage-only — fine as-is. Optionally typedef to `uint32_t`. |
| `debug::BreakpointManager` | No COM, no Win32. No changes. |

Everything below `debug::Session` is replaced:

| Windows | macOS replacement |
|---|---|
| `Session.cpp` (DbgEng lifecycle) | `Session_mac.cpp` (liblldb lifecycle) |
| `SessionInspection.cpp` (IDebugSymbolGroup2) | `SessionInspection_mac.cpp` (SBFrame/SBValue) |
| `SessionPrettyPrint.cpp` (DbgEng formatters) | `SessionPrettyPrint_mac.cpp` (SBValue formatters) |
| `Callbacks.cpp` (COM vtable → State flags) | Folded into `Session_mac.cpp` event dispatch |
| `Loader.cpp` (LoadLibrary sidecar) | Deleted — direct framework link |

---

## Threading Model — Unchanged

whatdbg's two-thread model maps directly to liblldb. No third thread needed.

```
[stdin thread]  — dap::Reader — push to AbstractFifo — unchanged
[Main thread]   — drain FIFO — SBListener::WaitForEvent(50ms) — write stdout
```

The COM `WaitForEvent(0, 50ms)` → `SBListener::WaitForEvent(50ms, event)` on the main thread. Same timeout, same deferred-flag pattern in `debug::State`. The event dispatch block replaces the COM callbacks vtable — still fires synchronously on the calling thread.

lldb-dap uses a dedicated event thread for its more complex multi-session architecture. whatdbg does not need this — single session, single main loop, same as Windows.

---

## API Mapping — Session_mac.cpp

Complete DbgEng → liblldb translation table derived from lldb-dap source (`EventHelper.cpp`, `Handler/`):

| `debug::Session` method | DbgEng (Windows) | liblldb (macOS) |
|---|---|---|
| `initialize()` | `CoInitializeEx` + `DebugCreate` + QI chain | `SBDebugger::Initialize()` + `SBDebugger::Create()` + `SBListener` setup |
| `launch()` | `IDebugClient5::CreateProcess2` | `SBTarget::LaunchSimple` or `SBTarget::Launch` with `SBLaunchInfo` |
| `attach()` | `IDebugClient5::AttachProcess` | `SBTarget::AttachToProcessWithID` |
| `resume()` | `IDebugControl4::SetExecutionStatus(GO)` | `SBProcess::Continue()` |
| `pollEvents()` | `IDebugControl4::WaitForEvent(0, 50ms)` | `SBListener::WaitForEvent(50ms, event)` |
| `shutdown()` | `IDebugClient5::EndSession` + `CoUninitialize` | `SBProcess::Kill()` or `SBProcess::Detach()` + `SBDebugger::Destroy()` |
| `stepOver()` | `SetExecutionStatus(STEP_OVER)` | `SBThread::StepOver(eOnlyDuringStepping)` |
| `stepInto()` | `SetExecutionStatus(STEP_INTO)` | `SBThread::StepInto()` |
| `stepOut()` | `Execute("gu")` | `SBThread::StepOutOfFrame(frame)` |
| `interrupt()` | `DebugBreakProcess(OpenProcess(pid))` | `SBProcess::SendAsyncInterrupt()` |
| `addBreakpoint()` | `IDebugControl4::AddBreakpoint2` | `SBTarget::BreakpointCreateByLocation(file, line)` |
| `removeBreakpoint()` | `IDebugControl4::RemoveBreakpoint2` | `SBTarget::BreakpointDelete(bp_id)` |
| `getOffsetByLine()` | `IDebugSymbols3::GetOffsetByLine` | `SBCompileUnit::FindLineEntry` + `SBLineEntry::GetStartAddress().GetLoadAddress()` |
| `getLineByOffset()` | `IDebugSymbols3::GetLineByOffset` | `SBAddress::GetLineEntry()` → `SBLineEntry` |
| `loadModuleSymbols()` | `Execute(".reload /f <name>")` | No-op — liblldb loads symbols automatically on module load |
| `forceReloadAllSymbols()` | `Execute(".reload /f")` | No-op |
| `appendSymbolPath()` | `IDebugSymbols3::AppendSymbolPath` | `SBDebugger::SetSelectedPlatformWorkingDirectory` or `SBTarget::AppendImageSearchPath` |
| `appendSourcePath()` | `IDebugSymbols3::AppendSourcePath` | `SBDebugger::SetSourceMap` or compile unit path resolution |
| `getThreads()` | `IDebugSystemObjects::GetThreadIdsByIndex` + `GetThreadDescription` | `SBProcess::GetNumThreads()` + `SBProcess::GetThreadAtIndex(i).GetThreadID()` + `GetName()` |
| `getEventThreadSystemId()` | `IDebugSystemObjects::GetEventThread` | `SBProcess::GetSelectedThread().GetThreadID()` |
| `setCurrentThreadBySystemId()` | `GetThreadIdBySystemId` + `SetCurrentThreadId` | `SBProcess::SetSelectedThreadByID(systemId)` |
| `resetSymbolGroupCache()` | Reset `cachedSymbolGroup` ComPtr | Reset cached `SBFrame` / `SBValueList` |

---

## Event Dispatch — Folded into Session_mac.cpp

COM callbacks wrote flags to `debug::State` synchronously during `WaitForEvent`. liblldb uses `SBEvent` with typed dispatch. The flag-writing pattern in `State` is identical — only the source changes.

Pattern extracted directly from `lldb-dap/EventHelper.cpp`:

```cpp
HRESULT Session::pollEvents (ULONG timeoutMs) noexcept
{
    lldb::SBEvent event;
    const bool hasEvent { listener.WaitForEvent (
        timeoutMs / 1000, event) };   // SBListener takes seconds

    if (! hasEvent)
        return S_FALSE;

    if (lldb::SBProcess::EventIsProcessEvent (event))
    {
        const auto state { lldb::SBProcess::GetStateFromEvent (event) };
        auto& state { jreng::Context<debug::State>::get() };

        switch (state)
        {
            case lldb::eStateStopped:
            case lldb::eStateCrashed:
            case lldb::eStateSuspended:
                if (! lldb::SBProcess::GetRestartedFromEvent (event))
                {
                    // Determine stop reason from selected thread
                    auto thread { process.GetSelectedThread() };
                    switch (thread.GetStopReason())
                    {
                        case lldb::eStopReasonBreakpoint:
                            state.hasBreakpointHit   = true;
                            state.breakpointEngineId  =
                                static_cast<ULONG> (
                                    thread.GetStopReasonDataAtIndex (0));
                            state.executionState =
                                debug::ExecutionState::stopped;
                            break;
                        case lldb::eStopReasonTrace:
                        case lldb::eStopReasonPlanComplete:
                            state.hasStepCompleted = true;
                            state.executionState   =
                                debug::ExecutionState::stopped;
                            break;
                        case lldb::eStopReasonInterrupt:
                            state.executionState =
                                debug::ExecutionState::stopped;
                            break;
                        default:
                            break;
                    }
                }
                break;

            case lldb::eStateExited:
                state.processExitCode  = process.GetExitStatus();
                state.hasProcessExited = true;
                state.executionState   = debug::ExecutionState::exited;
                break;

            case lldb::eStateRunning:
            case lldb::eStateStepping:
                state.executionState = debug::ExecutionState::running;
                break;

            default:
                break;
        }
    }
    else if (lldb::SBTarget::EventIsTargetEvent (event))
    {
        const uint32_t mask { event.GetType() };

        if (mask & lldb::SBTarget::eBroadcastBitModulesLoaded)
        {
            // Module load — trigger BP resolution
            auto& state { jreng::Context<debug::State>::get() };
            const uint32_t numModules {
                lldb::SBTarget::GetNumModulesFromEvent (event) };

            if (numModules > 0)
            {
                auto module {
                    lldb::SBTarget::GetModuleAtIndexFromEvent (0, event) };
                state.lastLoadedImageName =
                    juce::String (module.GetFileSpec().GetFilename());
                state.hasNewModuleLoaded = true;
            }
        }
    }

    return S_OK;
}
```

`Whatdbg::processDeferredEvents()` reads these flags identically on both platforms — zero changes required.

---

## API Mapping — SessionInspection_mac.cpp

`IDebugSymbolGroup2` → `SBFrame::GetVariables()` + `SBValue` tree.

| Operation | DbgEng | liblldb |
|---|---|---|
| Get locals for frame | `GetScopeSymbolGroup2(DEBUG_SCOPE_GROUP_ALL)` | `SBFrame::GetVariables(args, locals, statics, inScopeOnly)` |
| Enumerate symbols | `IDebugSymbolGroup2::GetSymbolParameters` loop | `SBValueList` iteration |
| Has children | `DEBUG_SYMBOL_PARAMETER::Flags & DEBUG_SYMBOL_IS_EXPANDABLE` | `SBValue::MightHaveChildren()` |
| Get children | `IDebugSymbolGroup2::ExpandSymbol(index)` | `SBValue::GetNumChildren()` + `SBValue::GetChildAtIndex(i)` |
| Symbol name | `GetSymbolName(index, buf, size, &nameSize)` | `SBValue::GetName()` |
| Symbol type | `GetSymbolTypeName(index, buf, size, &typeSize)` | `SBValue::GetTypeName()` |
| Symbol value | `GetSymbolValueText(index, buf, size, &valSize)` | `SBValue::GetValue()` |
| Stack walk | `IDebugControl4::GetStackTrace` + `GetLineByOffset` per frame | `SBThread::GetNumFrames()` + `SBFrame::GetLineEntry()` per frame |
| Expression eval | `Execute("?? <expr>")` + capture output | `SBFrame::EvaluateExpression(expr)` → `SBValue::GetValue()` |

The `symbolIndex`/`frameIndex` cache in `Whatdbg` maps to `SBValue` references held per stop event. `resetSymbolGroupCache()` clears the cached `SBValueList`.

---

## API Mapping — SessionPrettyPrint_mac.cpp

`IDebugSymbolGroup2` value formatting → `SBValue` direct read. Most of `SessionPrettyPrint.cpp`'s complexity exists because DbgEng returns raw formatted strings requiring post-processing. `SBValue` returns structured data directly.

| DbgEng pattern | liblldb equivalent |
|---|---|
| `stripDecimalPrefix` on raw output | Not needed — `SBValue::GetValue()` is already clean |
| `readTargetString` for `std::string` | `SBValue::GetSummary()` handles most STL types natively |
| `parseHexAddress` for pointer display | `SBValue::GetValue()` returns formatted pointer string |
| `findChildByName` for struct field traversal | `SBValue::GetChildMemberWithName(name)` |
| `formatSymbolValue` dispatch on type name | `SBValue::GetSummary()` covers most cases; fallback to `GetValue()` |

`SBValue::GetSummary()` leverages LLDB's built-in data formatters (STL, Objective-C containers, etc.) — significantly more capable than the hand-rolled DbgEng formatters in `SessionPrettyPrint.cpp`.

---

## macOS-Specific Setup

### liblldb linkage

On macOS, `liblldb` ships as `LLDB.framework` inside Xcode:

```
/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework
```

CMake link:
```cmake
target_link_libraries (whatdbg_App PRIVATE
    "-framework LLDB"
    "-F/Applications/Xcode.app/Contents/SharedFrameworks")
```

Headers:
```cmake
target_include_directories (whatdbg_App PRIVATE
    "/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework/Headers")
```

### task_for_pid — Attach Entitlement

Attaching to a running process on macOS requires the `com.apple.security.get-task-allow` entitlement in the debuggee's build. This is a **build configuration concern**, not a whatdbg code concern. Debug builds of C++ targets compiled with Xcode or CMake + Apple Clang automatically carry this entitlement.

For plugin debugging (DAW hosts with Hardened Runtime): attach requires the host to be built with `com.apple.security.cs.allow-unsigned-executable-memory` or debuggability explicitly enabled. Out of scope for this RFC — document as a known limitation.

### SBListener Setup in initialize()

```cpp
bool Session::initialize (const juce::File& /* sidecarDir — unused on macOS */) noexcept
{
    lldb::SBDebugger::Initialize();
    debugger = lldb::SBDebugger::Create (/*source_init_files=*/false);
    debugger.SetAsync (true);

    listener = debugger.GetListener();
    listener.StartListeningForEventClass (
        debugger,
        lldb::SBProcess::GetBroadcasterClassName(),
        lldb::SBProcess::eBroadcastBitStateChanged
        | lldb::SBProcess::eBroadcastBitSTDOUT
        | lldb::SBProcess::eBroadcastBitSTDERR);

    listener.StartListeningForEventClass (
        debugger,
        lldb::SBTarget::GetBroadcasterClassName(),
        lldb::SBTarget::eBroadcastBitModulesLoaded
        | lldb::SBTarget::eBroadcastBitBreakpointChanged);

    return debugger.IsValid();
}
```

---

## BLESSED Compliance

| Pillar | Assessment |
|---|---|
| **B**ounds | `SBDebugger`, `SBTarget`, `SBProcess` are value types with reference-counted internals — RAII by default. `SBListener` owned by Session. No raw owning pointers. |
| **L**ean | Three files mirror the existing Windows split exactly. No speculative abstraction. No `ISession` base class. |
| **E**xplicit | All `SBError` returns checked. No silent fail paths. `jassert` at boundaries for invalid state. |
| **S**SOT | `debug::State` remains the single source of truth. `SBValue` references are transient per-stop state only. |
| **S**tateless | `Session_mac` holds only the objects required for its lifetime contract: `SBDebugger`, `SBTarget`, `SBListener`. No shadow state of process execution. |
| **E**ncapsulation | Event dispatch folds into `pollEvents()` — Whatdbg sees the same `State` flag interface on both platforms. No platform leakage above `Session`. |
| **D**eterministic | Same input (DAP command) → same State flags → same DAP output. Identical deferred-event processing path on both platforms. |

---

## File Inventory

### New files (macOS only)

| File | Responsibility |
|---|---|
| `Source/debug/Session_mac.cpp` | `SBDebugger` lifecycle, `SBListener` event dispatch, stepping, breakpoint API, thread ops |
| `Source/debug/SessionInspection_mac.cpp` | `getStackTrace`, `getLocals`, `getVariableChildren`, `evaluateExpression` via `SBFrame`/`SBValue` |
| `Source/debug/SessionPrettyPrint_mac.cpp` | Value formatting via `SBValue::GetSummary()` / `GetValue()` |

### Modified files

| File | Change |
|---|---|
| `CMakeLists.txt` | Platform-select source files, add LLDB.framework linkage on APPLE |
| `Source/Whatdbg.cpp` | `#if JUCE_WINDOWS` guard around `_setmode` (3 lines) |

### Unchanged files

Everything else — `Whatdbg.h`, `WhatdbgHandlers.cpp`, `debug/State.h`, `debug/Session.h`, `debug/BreakpointManager.h/.cpp`, `dap/Reader.h/.cpp`, `dap/Types.h`, `modules/jreng_core/`.

---

## Prior Art

**`llvm-project/lldb/tools/lldb-dap`** — C++, Apache 2.0, part of LLVM monorepo.

Key files consulted:
- `EventHelper.cpp` — `SBListener::WaitForEvent` dispatch loop, all process state → DAP event translations, stop reason mapping
- `Variables.h/.cpp` — `SBFrame::GetVariables()` → `SBValueList` → `SBValue` child traversal
- `Handler/LaunchRequestHandler.cpp`, `AttachRequestHandler.cpp` — `SBTarget::Launch`, `SBTarget::AttachToProcessWithID`
- `Handler/StackTraceRequestHandler.cpp` — `SBThread::GetNumFrames()` + `SBFrame::GetLineEntry()` walk
- `Handler/EvaluateRequestHandler.cpp` — `SBFrame::EvaluateExpression()`

COUNSELOR reads these files directly during implementation — they are the ground truth for liblldb SB API usage patterns.

---

## Open Questions

None blocking. The following are implementation-time decisions:

1. **`pollEvents` timeout unit** — `SBListener::WaitForEvent` takes seconds (uint32_t). `kPollTimeoutMs = 50` → `listener.WaitForEvent(0, event)` with a non-blocking approach, or keep 50ms by passing `0` and accepting the coarser granularity. Recommend `WaitForEvent(0, event)` (non-blocking) to match the Windows behaviour more closely, with a `juce::Thread::sleep(10)` fallback when nothing fires.

2. **Symbol path on macOS** — `appendSymbolPath` on Windows appends to dbgeng's symbol search. liblldb uses `SBTarget::AppendImageSearchPath` or debugger source maps. Behaviour difference is acceptable — macOS dSYM resolution is largely automatic via Spotlight/dsymutil. Implementation-time decision.

3. **`hasPendingBreakpoints` / module load resolution** — On Windows, BP resolution happens explicitly after `.reload /f`. On macOS, liblldb resolves BPs automatically when modules load. The `hasNewModuleLoaded` flag path in `processDeferredEvents` may simplify to a no-op for the `loadModuleSymbols` call. Validate during implementation.

---

## Handoff Notes for COUNSELOR

- Read `llvm-project/lldb/tools/lldb-dap/EventHelper.cpp` first — the `EventThread` function and `HandleProcessEvent` contain the complete stop-reason dispatch that maps to `debug::State` flags.
- `Session.h` is the contract. Do not modify it. Implement against it.
- `debug::State` uses `ULONG` for `breakpointEngineId` and `targetProcessId`. On macOS, `lldb::break_id_t` is `int32_t` and `lldb::pid_t` is `uint64_t`. Cast at the boundary — internal State storage is fine as-is.
- The `sidecarDir` parameter to `initialize()` is unused on macOS — accept it, ignore it, no header change needed.
- `Loader.cpp` and `Callbacks.cpp` are excluded from the macOS CMake target. Do not reference them from macOS implementation files.

---

*BLESSED. Rock 'n Roll!*  
**JRENG!**
