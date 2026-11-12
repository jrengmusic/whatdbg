# whatdbg - Architecture
## Cross-Platform DAP Debug Adapter (Windows dbgeng / macOS liblldb)

**Repository:** https://github.com/jrengmusic/whatdbg

**Purpose:** Single source of truth for project structure, patterns, and contracts.

**Status:** Active

**Last Updated:** 2026-09-03

**Version:** 0.6.0

---

## Project Overview

### Purpose

whatdbg is a cross-platform DAP (Debug Adapter Protocol) debug adapter for C/C++ development with neovim. On Windows it uses the dbgeng COM API (the engine behind WinDbg); on macOS it uses the liblldb SB API (LLVM's scriptable interface). Both backends bridge DAP requests from nvim-dap to native debug engine operations over stdin/stdout.

### Architecture Philosophy

- **JUCE-native:** Leverage JUCE's battle-tested infrastructure. No hand-rolled abstractions, no manual callbacks, no manual threading primitives. Single-liner JUCE utilities over manual implementations.
- **Two threads, one loop:** Main thread owns everything (COM, State, logic, stdout). Stdin reader thread is a dumb FIFO buffer.
- **SSOT:** `debug::State` holds all execution state. Main-thread-local data. No cross-thread atomics except the FIFO.
- **Legacy-proven pattern:** Same architecture as the working legacy adapter, rebuilt with JUCE infrastructure and coding contracts.

### Technology Stack

- **Language:** C++17
- **Framework:** JUCE (juce_core), jam_core (`jam::Instance`, `jam::Union`, `jam::debug::Log`)
- **Build System:** `cast` generates `CMakeLists.txt` and `Source/generated/ProjectInfo.h` from `project-info.md`. CMake + Ninja underneath. Do not edit `CMakeLists.txt` by hand.
- **Platforms:**
  - Windows: MSVC via vcvarsall
  - macOS: Xcode clang, native architecture (CMake reads `uname -m`; `Resources/macos/` holds an arm64 and an x86_64 sidecar)
- **Debug Engines:**
  - Windows: dbgeng COM API (sidecar DLLs embedded in BinaryData)
  - macOS: liblldb SB API (sidecar dylib embedded in BinaryData, per-arch)
- **Windows COM Pointers:** `Microsoft::WRL::ComPtr<T>` (RAII, no manual Release)
- **macOS SB API:** liblldb headers from `Resources/macos/include/lldb/API/`

---

## Threading Model

### Two Threads

```
[stdin thread] -- read -- parse framing -- push to FIFO
                                                |
[Main thread]  -- pop from FIFO -- execute -- WaitForEvent -- write stdout
```

**stdin thread (juce::Thread):**
- Reads stdin (blocking)
- Parses Content-Length framing
- Pushes parsed juce::var messages to AbstractFifo
- No logic, no COM, no state access

**Main thread:**
- Owns everything: Session, State, BreakpointManager, stdout
- Main loop (`Whatdbg::run`, `Source/Whatdbg.cpp`):
  1. Drain FIFO — pop every pending DAP message and dispatch via `onCommand`
  2. `session.pollEvents (pollTimeoutMs)` — 50 ms timeout, called only while
     `state.executionState` is `running` or `launching`; skipped entirely while
     `stopped`, `idle`, or `exited`
  3. `processDeferredEvents()` — drain flags written by the platform backend
     (breakpoint hit, step/pause completed, module loaded, output, exception, exit)
  4. `juce::Thread::sleep (idleSleepMs)` — 10 ms, only while `idle`, so the loop
     does not spin hot with no target attached
- All engine calls happen here: dbgeng COM calls (Windows) or liblldb SB API calls
  (macOS) — both single-threaded, same thread that initialized the backend.

### FIFO

- `juce::AbstractFifo` — lock-free SPSC index manager
- `std::vector<juce::var>` — backing storage, pre-sized to capacity (juce::var is non-trivial, needs proper construction — HeapBlock would require manual placement new)
- 64 slots (generous for DAP burst of 5-6 messages)
- Only shared data structure between threads

### Why Two Threads

stdin is blocking. WaitForEvent is blocking (with timeout). They cannot both block on one thread. The stdin thread isolates the blocking read. Everything else is single-threaded.

### Thread Safety Rules

- Main thread owns ALL mutable state. No locks needed except the FIFO.
- stdin thread ONLY pushes to FIFO. No other shared data access.
- COM callbacks fire synchronously during WaitForEvent on the main thread.
- All dbgeng COM calls MUST happen on the main thread (same thread as CoInitializeEx).

---

## Module Structure

### Module Map

```
Whatdbg (main loop, owns everything)
    |
    +-- debug::State (SSOT, main-thread-local)
    +-- debug::Session (debug engine wrapper)
    |       [Windows]
    |       Session.cpp              — lifecycle: initialize, launch, attach, resume, shutdown,
    |                                  stepping, thread control, symbol path, breakpoint API;
    |                                  variable inspection: getStackTrace, getLocals,
    |                                  getVariableChildren, evaluateExpression
    |       SessionPrettyPrint.cpp   — type formatters: prettyPrint, formatSymbolValue,
    |                                  readTargetString, findChildByName
    |       [macOS]
    |       Session_mac.cpp          — lifecycle: initialize, launch, attach, resume, shutdown,
    |                                  stepping, thread control, breakpoint API (liblldb SB API);
    |                                  variable inspection: getStackTrace, getLocals,
    |                                  getVariableChildren, evaluateExpression (SBValue tree)
    |       SessionPrettyPrint_mac.cpp — type formatters: prettyPrint for juce::String,
    |                                  unique_ptr, and filtered types (SB API)
    +-- debug::BreakpointManager (DAP-to-engine BP mapping)
    +-- debug::Callbacks (COM callbacks, write to State) [Windows only]
    +-- debug::Loader (sidecar DLL loader) [Windows only]
    +-- dap::Reader (stdin thread, pushes to FIFO)
    +-- dap::Types (DAP message builders, DynObj alias)
```

### Module Inventory

| Module | Location | Responsibility |
|--------|----------|----------------|
| Whatdbg | `Source/Whatdbg.h/.cpp` | Main loop. Owns all objects. Drains FIFO, dispatches commands via table, polls the engine, writes stdout. Holds the DAP request handlers (`onInitialize`, `onLaunch`, `onSetBreakpoints`, `onDisconnect`, etc.) and the deferred-event drains. |
| debug::State | `Source/debug/State.h` | SSOT. Execution state, pending events, breakpoint data. Main-thread-local, no atomics. Derives from `jam::Instance<State>`. |
| debug::Session | `Source/debug/Session.h` | Cross-platform engine wrapper interface. `ComPtr<T>` (Windows) or SB API value types (macOS) behind one shared header. |
| debug::Session (Win) | `Source/debug/Session.cpp` | initialize, launch, attach, resume, pollEvents, shutdown, stepping, thread ops, breakpoint API, symbol path, getStackTrace, getLocals, getVariableChildren, evaluateExpression, getSymbols (file-local static). |
| debug::Session pretty-print (Win) | `Source/debug/SessionPrettyPrint.cpp` | Formatters declared in `PrettyPrint.h` and defined non-static here: prettyPrint, formatSymbolValue, stripDecimalPrefix, readTargetString, findChildByName, getChildValueText. The per-type helpers are file-local static. |
| debug::Session (mac) | `Source/debug/Session_mac.cpp` | liblldb SB API: initialize, launch, attach, resume, pollEvents, shutdown, stepping, thread ops, breakpoint API, getStackTrace, getLocals, getVariableChildren, evaluateExpression, getVariableObject via SBValue tree. |
| debug::Session pretty-print (mac) | `Source/debug/SessionPrettyPrint_mac.cpp` | type formatters for juce::String, unique_ptr, and filtered types via SB API. |
| debug::PrettyPrint | `Source/debug/PrettyPrint.h` | Shared, header-only helpers used across both platforms' Session and PrettyPrint files: `shouldSkipSymbol`, `parseHexAddress`. |
| debug::BreakpointManager | `Source/debug/BreakpointManager.h/.cpp` | DAP-to-engine breakpoint mapping. `onSetBreakpoints()`, `onModuleLoad()`, `onReload()`, `getBreakpointStoppedEvent()`, `onBreakpointLocationFound()`. Private resolution machinery: `getBreakpointOffset`, `setBreakpointLocation`, `removeOrphanedBreakpoints`, `addBreakpoint`, `hasUnresolvedBreakpoints`. |
| debug::Callbacks | `Source/debug/Callbacks.h/.cpp` | COM callback classes. Write to State during WaitForEvent. [Windows only] |
| debug::Loader | `Source/debug/Loader.h/.cpp` | LoadLibrary from sidecar path, resolve DebugCreate. [Windows only] |
| dap::Reader | `Source/dap/Reader.h/.cpp` | juce::Thread. Reads stdin, parses Content-Length framing, pushes to AbstractFifo. |
| dap::Types | `Source/dap/Types.h` | DAP message builders using juce::var/DynamicObject. `getResponse`, `getErrorResponse`, `getEvent`, `getCapabilities`. DynObj alias (single definition). |

---

## Data Flow

### DAP Command (stdin to stdout)

```
stdin -> [Reader thread] -> FIFO -> [Main thread] -> commands table (onCommand) -> write stdout
```

### Debug Event (engine to stdout)

```
[Windows] WaitForEvent -> COM callback (Callbacks.cpp) on main thread -> write to State
[macOS]   session.pollEvents -> onProcessStateStopped dispatch (Session_mac.cpp) -> write to State
                                                                       |
                              processDeferredEvents (main loop) reads State -> write stdout
```

### Breakpoint Resolution

```
setBreakpoints arrives in FIFO
    -> Main thread pops, calls Whatdbg::onSetBreakpoints -> BreakpointManager::onSetBreakpoints()
    -> addBreakpoint() -> Session::addBreakpointByLocation, writes engineId/resolvedLine on success
       [Windows] Session::getOffsetStatus / Session::getOffset (file-local getSourceOffset helper), then Session::addBreakpoint
       [macOS]   SBTarget::BreakpointCreateByLocation
    -> Immediate result: verified or pending (engineId is empty)
    -> Write DAP response to stdout
    -> Whatdbg::onSetBreakpoints calls onReload(); it reloads symbols only when an entry is unresolved
    -> [Windows] Pending BPs resolve on module load: LoadModule callback (stopReason moduleLoad)
       -> drainModuleLoaded -> BreakpointManager::onModuleLoad() -> onSymbolsLoaded()
       -> setBreakpointLocation() -> getBreakpointOffset()
       (onReload() runs the same onSymbolsLoaded path after setBreakpoints, a step, and the initial break)
    -> [macOS] liblldb resolves pending BPs: onBreakpointEvent (locations resolved or added)
       -> State::hasBreakpointLocationsResolved -> Whatdbg::drainBreakpointLocationResolved
       -> BreakpointManager::onBreakpointLocationFound()
```

### Variable Inspection

```
stackTrace -> assigns unique frameIds (stored in frameIdMap)
scopes     -> allocates variablesReference for locals scope
variables  -> looks up (frameIndex, symbolIndex) from variablesRefMap
           [Windows] getLocals / getVariableChildren via IDebugSymbolGroup2
           [macOS]   getLocals / getVariableChildren via SBValue tree, getVariableObject per value
           -> prettyPrint dispatches to per-type formatters
           -> child refs allocated for expandable variables
```

### Expression Evaluation

```
[Windows]
evaluate -> Session::evaluateExpression -> scope symbol group (getOrCreateSymbolGroup, frameIndex)
         -> AddSymbol (expression) with SYMOPT_NO_PUBLICS; success only when AddSymbol returns S_OK
         -> value from getSymbolValue (formatSymbolValue, prettyPrint); symbol removed on scope exit
         -> empty result: Whatdbg::onEvaluate sends the error response "Could not evaluate: <expr>"

[macOS]
evaluate -> Session::evaluateExpression -> SBFrame::EvaluateExpression on the frame at frameIndex
         -> result formatted through the same prettyPrint dispatch as variable inspection
```

### Debuggee Output Capture

```
[Windows]
OutputCallbacks::Output2(DEBUG_OUTCB_TEXT, DEBUG_OUTPUT_DEBUGGEE)
    -> state->debuggeeOutputText accumulated
    -> state->hasDebuggeeOutput = true

[macOS]
drainProcessStdio (Session_mac.cpp) polls process.GetSTDOUT / process.GetSTDERR
    -> state->debuggeeOutputText accumulated, state->debuggeeOutputCategory set
    -> state->hasDebuggeeOutput = true

-> main loop: drainDebuggeeOutput emits DAP output event (category: console/stdout/stderr)
```

### Exception Flow

```
[Windows] Exception (SEH, second-chance)
  -> EventCallbacks::Exception (dbgeng callback)
  -> onUnknownException (Callbacks.cpp)
      -- sets State::stopReason = exception, and populates exceptionCode, exceptionAddress
      -- sets executionState = stopped
      -- returns DEBUG_STATUS_BREAK

[macOS] Signal or Mach exception stop
  -> onProcessStateStopped -> onSignalStop / onExceptionStop (Session_mac.cpp)
      -- sets State::stopReason = exception, and populates exceptionCode, exceptionAddress, isMachException

  -> Whatdbg::processDeferredEvents (Whatdbg.cpp) -> drainExceptionStopped
      -- emits DAP `stopped` event (reason=exception)
      -- emits DAP `output` event (category=stderr)
  -> nvim-dap receives events, requests `stackTrace` + `exceptionInfo`
  -> Whatdbg::onStackTrace / onExceptionInfo respond
```

---

## Main Loop

```
while (state.isRunning)
{
    // 1. Drain FIFO
    while (message = reader.tryPop ())
        onCommand (message)  // dispatch via commands table

    // 2. Poll the engine while running or launching, else sleep
    if (executionState == running or executionState == launching)
        session.pollEvents (pollTimeoutMs)   // 50 ms
    else
        juce::Thread::sleep (idleSleepMs)    // 10 ms

    // 3. Process deferred events (processDeferredEvents)
    stops.find (stopReason) -> one drain:
        initialBreak -> drainInitialBreak ()
        moduleLoad   -> drainModuleLoaded ()
        breakpoint   -> drainBreakpointHit ()
        step         -> drainStepCompleted ()
        pause        -> drainPauseCompleted ()
        exception    -> drainExceptionStopped ()
    drainBreakpointLocationResolved ()
    drainDebuggeeOutput ()
    drainProcessExited ()
    drainTerminateTimeout ()
}

session.shutdown (getEndModeForExit ())
```

`State::stopReason` holds one stop cause at a time, so one drain runs per iteration.
`drainBreakpointHit` asks `BreakpointManager::getBreakpointStoppedEvent` for the
stopped event. That event has `reason: "step"` when no registry entry holds the engine
id, as for the internal stepOut breakpoint.

---

## State Management

### debug::State

Main-thread-local. No atomics (except what FIFO handles). Plain data.

- `ExecutionState executionState` — idle, launching, running, stopped, exited
- `StopReason stopReason` — `none`, `initialBreak`, `moduleLoad`, `breakpoint`, `step`, `pause`, `exception`; one enum field replaces the former per-event bools and the initial-break phase enum (SSOT). `processDeferredEvents` looks it up in the `stops` `jam::Function::Map` and runs one drain. Each drain sets it back to `none`; `drainInitialBreak` does so once `isConfigurationDone` is true
- `int processExitCode`
- `std::uint32_t targetProcessId` — set on CreateProcess callback (Windows) or launch/attach (macOS); used by `Session::interrupt`
- `std::uint32_t terminateDeadlineMs` — set by `onDisconnect`; `drainTerminateTimeout` forces `isRunning = false` if the debuggee's exit event never arrives
- `bool isRunning` — main loop continuation flag; `onDisconnect` and `drainTerminateTimeout` are the only writers
- `bool shouldTerminateOnExit` — set by `onDisconnect`; read by `getEndModeForExit` to choose `EndMode::terminate` vs `EndMode::detach`
- `bool isConfigurationDone`, `bool isStepPending`, `bool isPausePending` — request-driven flags. `drainInitialBreak` reads `isConfigurationDone`. `Session::pollEvents` and `LoadModule` read `isStepPending`. `onBreakpoint` (Windows) and the macOS stop-reason handler read `isPausePending`
- Deferred event fields: `breakpointEngineId`, `lastLoadedImageName`, `hasBreakpointLocationsResolved` + `resolvedBreakpointEngineId`/`resolvedBreakpointLine`, `hasProcessExited`, `hasDebuggeeOutput` + `debuggeeOutputText`/`debuggeeOutputCategory`. `BreakpointManager::hasUnresolvedBreakpoints` (private) derives the unresolved state from the registry
- `StopReason::exception` — set by `onBreakpoint` or `onUnknownException` (Windows, second-chance) or `onSignalStop`/`onExceptionStop` (macOS); `drainExceptionStopped` sets `stopReason` to `none` and emits the DAP `stopped` event
- `std::uint32_t exceptionCode` — NTSTATUS code (Windows) or signal/Mach exception code (macOS); persists after consumption for the `exceptionInfo` request response
- `std::uint64_t exceptionAddress` — faulting virtual address; persists after consumption
- `bool isMachException` — macOS only; distinguishes a Mach exception from a POSIX signal stop for `getExceptionName`'s table lookup
- `int nextVariablesRef`, `std::unordered_map<int, std::pair<int, int>> variablesRefMap` — variablesReference allocation, reset every stop event
- `int nextFrameId`, `std::unordered_map<int, std::pair<std::uint32_t, int>> frameIdMap` — frameId → (threadSystemId, frameIndex), reset every stop event
- `std::uint32_t lastScopesThreadId` — thread context restored by `onVariables` when a request arrives without an explicit frame reselect

Since State is main-thread-only, it uses plain types. No CriticalSection, no
std::atomic. `State` derives from `jam::Instance<State>`, which provides global
access via `getInstance()` — there is no `jam::Context<T>` in this codebase.

---

## Key Design Decisions

### Decision: Two-Thread Model (not three, not event-driven)

**Context:** Initial design attempted three threads (stdin, COM, message) with callAsync notifications. This caused: COM thread isolation issues, callback flooding, message thread starvation, timing bugs.

**Decision:** Two threads. Main thread owns everything. Stdin thread is a FIFO buffer. Same proven architecture as the working legacy adapter.

**Rationale:** Simple. Proven. No cross-thread state synchronization needed. COM lives on one thread. JUCE provides the FIFO (AbstractFifo), thread (juce::Thread), and utilities.

### Decision: AbstractFifo for stdin buffering

**Context:** stdin read is blocking. Main thread cannot block on stdin and WaitForEvent simultaneously.

**Decision:** juce::AbstractFifo (lock-free SPSC) with std::vector<juce::var> backing storage. 64 slots.

**Rationale:** JUCE-native. Lock-free. Single allocation. No hand-rolled queue.

### Decision: No JUCE Message Thread

**Context:** Initial design used juce::MessageManager::runDispatchLoop() and callAsync for event delivery.

**Decision:** Skip JUCE message/event system entirely. Main thread runs its own loop. No initialiseJuce_GUI, no runDispatchLoop, no callAsync.

**Rationale:** We are a console app with a simple main loop. JUCE message thread adds complexity for no benefit. We use JUCE for utilities (File, String, JSON, Thread, AbstractFifo, ComPtr), not its event system.

### Decision: Dispatch Table for DAP Commands

**Context:** An earlier revision used an else-if chain across DAP command names.

**Decision:** The `commands` table is a `jam::Function::Map<juce::String, jam::Function::pointerCapacity, void (const juce::var&)>`. The `Whatdbg` constructor fills it with `.add (name, lambda)`. `onCommand` does a single map lookup.

**Rationale:** O(1) dispatch, no branch cascade, new commands added by inserting one map entry.

### Decision: Session Split (two files per platform)

**Context:** A single Session file grows past the point where lifecycle, inspection, and type-formatting concerns stay separable.

**Decision:** Split per platform into `Session[_mac].cpp` and `SessionPrettyPrint[_mac].cpp`. `Session[_mac].cpp` holds the lifecycle and the variable inspection. `SessionPrettyPrint[_mac].cpp` holds the type formatters. ARCHITECT: "Merge only". The inspection files merged into the Session files. The handler files merged into `Whatdbg.cpp` and `BreakpointManager.cpp`. `PrettyPrint.h` declares the type formatters that the Session files call. It also holds the inline helpers `shouldSkipSymbol` and `parseHexAddress`. There is no `debug::detail` namespace; CODING.md forbids implementation-hiding namespaces outright.

**Rationale:** One object is one header and one `.cpp` (CODING.md). Each `Session[_mac].cpp` holds the whole Session object. The free functions of `PrettyPrint.h` keep their own files. `Session.h`'s interface is the same on both platforms — the split is implementation-only, selected by `JUCE_WINDOWS`/`JUCE_MAC` at the build-target level.

### Decision: Symbol Group Cache

**Context:** IDebugSymbolGroup2 is expensive to create. scopes and variables requests arrive sequentially for the same frame.

**Decision:** Cache the last created group by frameIndex. Invalidate on every stop event via resetSymbolGroupCache().

**Rationale:** Eliminates repeated SetScopeFrameByIndex + GetScopeSymbolGroup2 calls per variable request.

### Decision: Terminate vs Disconnect

**Context:** DAP specifies separate `terminate` and `disconnect` commands with optional `terminateDebuggee`.

**Decision:** Both route to `Whatdbg::onDisconnect` (`Source/Whatdbg.cpp`). `EndMode` enum (`Source/debug/Session.h`) selects behavior. `Session::shutdown (EndMode)` (`Source/debug/Session.cpp` on Windows, `Source/debug/Session_mac.cpp` on macOS) dispatches on three cases:
- `terminate` — kills the debuggee; used when `terminate` arrives, or `disconnect` arrives with `terminateDebuggee: true`. Windows: `DEBUG_END_ACTIVE_TERMINATE`. macOS: `process.Signal (SIGKILL)` followed by `resume()` (see zombie fix below).
- `detach` — releases the debuggee, which keeps running; used when `disconnect` arrives without `terminateDebuggee`. Windows: `DEBUG_END_ACTIVE_DETACH`. macOS: `process.Detach()`.
- `passive` — target has already exited; releases session state only, without acting on a target that no longer exists. Required because ending an active session against an already-exited target hangs or double-frees on both platforms.

`onDisconnect` itself only sends the kill/detach signal and moves `executionState`
to `running` so the main loop keeps polling for the resulting exit event —
`Session::shutdown` runs once, at `run()`'s exit, via `getEndModeForExit()`.

**macOS zombie fix (2026-09 sprint):** `Session::terminateDebuggee` (called from
`onDisconnect`, not from `shutdown`) sends `SIGKILL` via `process.Signal`, then
calls `resume()` so the main loop's normal `pollEvents` path observes the
debuggee's exit through liblldb's own state machine before anything tears the
session down. `Session::shutdown`'s `EndMode::terminate` case does the same
`Signal (SIGKILL)` + `resume()` pair as a fallback for the case where `shutdown`
runs without a prior `terminateDebuggee` call. `shutdown` never calls
`SBDebugger::Destroy` until `debugger.IsValid()` confirms it has not already run
— `run()` calls `shutdown()` explicitly at loop exit, and `~Session()` calls it
again unconditionally, so the guard makes the second call a no-op instead of a
double-terminate. The prior defect (RFC-ZOMBIE-TERMINATION.md) was a raw
`::kill()` followed immediately by `SBDebugger::Destroy()` with no reap in
between, which severed `debugserver`'s ptrace connection before the kernel
collected the debuggee's exit status — `launchd` inherits zombies and never
reaps them. The fix keeps the debuggee's exit observation on the same
`pollEvents`/`onProcessStateStopped` path every other stop uses, instead of a
bespoke wait loop.

### Decision: DynObj Alias in dap::Types

**Context:** `DynObj` (ReferenceCountedObjectPtr<DynamicObject>) was redefined in three files.

**Decision:** Single definition in dap::Types.h. All files that need it import via `using dap::DynObj`.

---

## Stepping Implementation

- `next` (step over):
  - Windows: `SetExecutionStatus(DEBUG_STATUS_STEP_OVER)` + `isStepPending = true`
  - macOS: `Session::stepOver` → `SBThread::StepOver(eOnlyDuringStepping)` on the selected thread
- `stepIn` (step into):
  - Windows: `SetExecutionStatus(DEBUG_STATUS_STEP_INTO)` + `isStepPending = true`
  - macOS: `Session::stepInto` → `SBThread::StepInto()`
- `stepOut` (step out):
  - Windows: `Execute("gu")` — runs until return; the stop at the resulting internal BP gives `reason: "step"`, because `getBreakpointStoppedEvent` finds no registry entry for its engine id
  - macOS: `Session::stepOut` → `SBThread::StepOutOfFrame(frame)`
- Step completion detection:
  - Windows: `Session::pollEvents` sees `WaitForEvent` return `S_OK` while `isStepPending` and `stopReason` is `none`, and sets `stopReason` to `step`
  - macOS: `pollEvents` dispatches to `onProcessStateStopped`, whose `lldb::eStopReasonTrace` and `lldb::eStopReasonPlanComplete` cases both route to `onStepStop`, setting `stopReason` to `step`

## Pause Implementation

- `pause`:
  - Windows: `DebugBreakProcess(OpenProcess(pid))` + `isPausePending = true`
  - macOS: `Session::interrupt` → `SBProcess::SendAsyncInterrupt()` (the bound `process` member already knows the target; the `processId` parameter is unused on this platform) + `isPausePending = true`
- Pause detection:
  - Windows: the injected int3 arrives at `onBreakpoint` (`Callbacks.cpp`), which sets `stopReason` to `pause` while `isPausePending`
  - macOS: `onProcessStateStopped` dispatches `lldb::eStopReasonInterrupt` to `onInterruptStop`, setting `stopReason` to `pause`. A stop while `isPausePending` also goes to `onInterruptStop`
- Emits stopped/pause event with the current event thread's system ID

## Multi-Thread Support

- `getThreads()`:
  - Windows: `IDebugSystemObjects::GetThreadIdsByIndex`, `GetThreadDescription` per thread
  - macOS: `SBProcess::GetThreadAtIndex`, `SBThread::GetName()` per thread — liblldb does not expose an OS-level thread-description API equivalent to Windows' `GetThreadDescription`; unnamed threads report `SBThread::GetName()`'s own fallback string, not a whatdbg-synthesized "Thread <TID>" label
- `getEventThreadSystemId()`: resolves the reporting thread's OS thread ID on both platforms (`GetEventThread` on Windows, the stopped `SBThread`'s system ID on macOS)
- `setCurrentThreadBySystemId()`: selects the given OS thread as current on both platforms (`GetThreadIdBySystemId` + `SetCurrentThreadId` on Windows, `SBProcess::SetSelectedThreadByID` on macOS)
- Frame ID map: `stackTrace` assigns unique IDs per frame, stores (threadSystemId, frameIndex)
- `scopes`/`variables` restore thread context via `setCurrentThreadBySystemId` before inspecting

---

## File Structure

```
whatdbg/
    project-info.md                     Build manifest — cast reads this
    cast/
        spell.md                        Output rows and toolchain wiring
        cmake.cast                      CMakeLists template
    CMakeLists.txt                      GENERATED — do not edit
    entitlements.plist                  macOS codesign entitlements
    ARCHITECTURE.md
    SPEC.md
    Source/
        Main.cpp                        Entry point, sidecar extraction, re-exec trampoline (macOS)
        Whatdbg.h / .cpp                Main loop, DAP request handlers, deferred event drains
        generated/
            ProjectInfo.h               GENERATED — do not edit
        debug/
            State.h                     SSOT, main-thread-local
            Session.h                   Debug engine wrapper interface (shared)
            Session.cpp                 [Windows] Lifecycle, stepping, thread ops, breakpoint API, stack trace, locals, children, expression eval
            SessionPrettyPrint.cpp      [Windows] Type formatters (file-local static functions)
            Session_mac.cpp             [macOS] Lifecycle, stepping, thread ops, breakpoint API (SB API), stack trace, locals, children, expression eval (SBValue)
            SessionPrettyPrint_mac.cpp  [macOS] Type formatters (juce::String, unique_ptr, filters)
            PrettyPrint.h               Shared header-only helpers (shouldSkipSymbol, parseHexAddress)
            Loader.h / .cpp             [Windows] Sidecar DLL loader
            Callbacks.h / .cpp          [Windows] COM callbacks -> State
            BreakpointManager.h / .cpp  DAP-to-engine BP mapping, resolution machinery (getBreakpointOffset, setBreakpointLocation, orphan removal)
        dap/
            Reader.h / .cpp             stdin thread + FIFO
            Types.h                     DAP message builders, DynObj alias
    tests/
        smoke/
            run_smoke.lua               Orchestrator — nvim-headless Lua driver, 10 scenarios
            scenario_runner.lua         Shared DAP request/response driving helpers
            scenario_breakpoint.lua     Launch+breakpoint, step, variables, evaluate scenarios
            scenario_process.lua        Attach, pause, output, crash, disconnect-detach scenarios
            scenario_terminate.lua      Terminate-without-zombie scenario
            dap_client.lua              Minimal DAP client over stdio
            report.lua                  Pass/fail summary
            fixture.cpp                 Standard breakpoint/variables/evaluate fixture
            fixture_wait.cpp            Long-running fixture for attach/pause
            fixture_crash.cpp           Fixture that raises an unhandled exception
    Resources/
        windows/
            x64/
                dbgeng.dll, dbghelp.dll, dbgcore.dll, symsrv.dll
        macos/                          (Git LFS — built by build-liblldb.sh)
            arm64/
                liblldb.dylib
            x86_64/
                liblldb.dylib
            include/lldb/API/           SB API headers
            licenses/LLVM-LICENSE.TXT
    Builds/                             (gitignored — build output + liblldb build machinery)
        Release/                        (CMake/Ninja build tree — Release)
        Debug/                          (CMake/Ninja build tree — Debug)
        liblldb/                        (LLVM source clone + cmake tree)
            llvm-project/               (pinned clone of llvm/llvm-project at LLVM_TAG)
            cmake/                      (out-of-source cmake build tree)
    build-liblldb.sh                    (pinned LLVM build — writes Builds/liblldb + Resources/macos/)
    build.sh                            Copy of cast/build.sh — escape hatch for Windows builds from the MSVC shell of the host architecture.
    carol/
        SPRINT-LOG.md                   Cross-session sprint memory
        SMOKE-*.md                      Per-run smoke test reports
        MANIFESTO.md, NAMES.md, CODING.md, ARCHITECT.md, LANGUAGE.md, ODE.md,
        ARCHITECTURE-WRITER.md, SPEC-WRITER.md   Symlinks into ~/.carol/
        config.yml
```

---

## Glossary

| Term | Definition |
|------|------------|
| DAP | Debug Adapter Protocol. JSON-RPC over stdin/stdout. |
| dbgeng | Windows Debug Engine COM API (the engine behind WinDbg). |
| liblldb | LLVM's debugger library. Exposed via the SB (Scriptable Bridge) API. |
| SB API | liblldb's public C++ interface (`lldb::SBDebugger`, `SBTarget`, `SBProcess`, etc.). |
| Sidecar | Pinned debug engine binaries embedded in BinaryData, extracted at runtime. Windows: DLLs extracted to `~/.config/whatdbg/dbgeng/` and loaded via `LoadLibrary`. macOS: `liblldb.dylib` extracted to `~/Library/Application Support/whatdbg/liblldb/`, then the process re-execs itself with `DYLD_LIBRARY_PATH` set so dyld resolves SB API symbols. |
| Re-exec trampoline | macOS startup pattern: `Main.cpp` extracts `liblldb.dylib`, sets `DYLD_LIBRARY_PATH`, then `execv`s itself. The second exec finds the dylib and proceeds normally. No explicit `dlopen` needed. |
| WaitForEvent | Blocking dbgeng call (Windows) that processes debug events. |
| SSOT | Single Source of Truth. |
| FIFO | First In First Out queue. AbstractFifo + std::vector. |
| SPSC | Single Producer Single Consumer. |
| DynObj | `juce::ReferenceCountedObjectPtr<juce::DynamicObject>`. Alias defined in dap::Types. |
| prettyPrint | Per-type value formatter for juce::String, std::string, std::unique_ptr, std::vector. |
| dispatch table | `commands`, a `jam::Function::Map<juce::String, jam::Function::pointerCapacity, void (const juce::var&)>`, in `Whatdbg`. It replaces an else-if chain. `onCommand` looks up the DAP command name. |
| cast | Code generator. It reads `project-info.md` and `cast/spell.md`. It writes `CMakeLists.txt`, `Source/generated/ProjectInfo.h`, `gh.cmake`, and `RELEASE.md`. It then runs the selected toolchain row. One invocation generates and builds. |
| toolchain row | A row in the `## toolchain` table of `project-info.md`. Each row names one architecture. `cast --arm64` selects one row. |
| `jam::Instance<T>` | CRTP base in jam_core. It gives `getInstance()`. `debug::State` derives from it. |
| `jam::Union` | Packed transport type in jam_core. It holds 2 to 4 trivially copyable values in one word. `BreakpointLocation` uses it. |
| BreakpointLocation | `jam::Union<std::int32_t, std::uint16_t>`. It carries an engine breakpoint ID and a resolved line. |

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-03-30 | COUNSELOR | Initial draft (three-thread model) |
| 0.2 | 2026-03-31 | COUNSELOR | Simplified to two-thread model, dropped JUCE message system |
| 0.3 | 2026-04-01 | MACHINIST | Session split (3 files), variable inspection, expression eval, OutputDebugString, pause, multi-thread, stepping, terminate/disconnect, symbol group cache, dispatch table, DynObj consolidation |
| 0.4.0 | 2026-04-24 | ENGINEER | Cross-platform: macOS liblldb backend (Session_mac, SessionInspection_mac, SessionPrettyPrint_mac), per-arch sidecar layout, re-exec trampoline, JAM build system (`configure_app()`), updated stack and module map |
| 0.5.0 | 2026-09-02 | COUNSELOR | Build moved to `cast` (`project-info.md`, `cast/spell.md`, `cast/cmake.cast`); `jam::Context` replaced by `jam::Instance`; `Log.h` replaced by `jam::debug::Log`; `BreakpointInfo` collapsed and `BreakpointLocation` added; Session out-parameters removed; `getOffsetByLine` split into `getOffsetStatus` and `getOffset` |
| 0.6.0 | 2026-09-03 | COUNSELOR | Corrected this document against the current codebase: main loop, module map, and data-flow diagrams rewritten cross-platform; `debug::detail`/`Context<State>`/`handleCommand`/`handleUnknownException`/`handleStackTrace`/`handleExceptionInfo`/`handleDisconnect`/`handleSetBreakpoints`/`tryResolve` (none exist) replaced with the real `onCommand`/`onUnknownException`/`onStackTrace`/`onExceptionInfo`/`onDisconnect`/`onSetBreakpoints`/`getBreakpointOffset` names; `State::targetProcessId` corrected from `ULONG` to `std::uint32_t`; macOS zombie-termination fix (`Session::terminateDebuggee`/`Session::shutdown` kill-then-resume) documented under Terminate vs Disconnect; stepping/pause/multi-thread sections given macOS equivalents; `tests/smoke/` and `Resources/macos/` (LFS, not gitignored) added to File Structure |

---

**End of Architecture Document**

**JRENG!**
