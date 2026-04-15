# whatdbg - Architecture
## Windows Host Abstraction Translator for dbgeng

**Repository:** https://github.com/jrengmusic/whatdbg

**Purpose:** Single source of truth for project structure, patterns, and contracts.

**Status:** Active

**Last Updated:** 2026-04-02

**Version:** 0.3.0

---

## Project Overview

### Purpose

whatdbg (Windows Host Abstraction Translator for dbgeng) is a DAP (Debug Adapter Protocol) debug adapter for Windows C/C++ development with neovim. It uses the dbgeng COM API (the engine behind WinDbg) to attach to or launch any Windows executable, then bridges DAP requests from nvim-dap to dbgeng operations over stdin/stdout.

### Architecture Philosophy

- **JUCE-native:** Leverage JUCE's battle-tested infrastructure. No hand-rolled abstractions, no manual callbacks, no manual threading primitives. Single-liner JUCE utilities over manual implementations.
- **Two threads, one loop:** Main thread owns everything (COM, State, logic, stdout). Stdin reader thread is a dumb FIFO buffer.
- **SSOT:** `debug::State` holds all execution state. Main-thread-local data. No cross-thread atomics except the FIFO.
- **Legacy-proven pattern:** Same architecture as the working legacy adapter, rebuilt with JUCE infrastructure and coding contracts.

### Technology Stack

- **Language:** C++17
- **Framework:** JUCE (juce_core, juce_events), jreng_core (Context, Owner, utilities)
- **Build System:** CMake + Ninja (MSVC via vcvarsall)
- **Platform:** Windows only
- **Debug Engine:** dbgeng COM API (sidecar DLLs embedded in BinaryData)
- **COM Pointers:** Microsoft::WRL::ComPtr<T> (RAII, no manual Release)

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
- Owns everything: COM session, State, BreakpointManager, stdout
- Main loop:
  1. Drain FIFO — process all pending DAP commands
  2. WaitForEvent(0, 50) — short timeout, non-blocking effectively
  3. Process deferred events (breakpoint hit, module load, exit)
  4. Write DAP responses/events to stdout
- All dbgeng COM calls happen here (CoInitializeEx, CreateProcess2, GetOffsetByLine, WaitForEvent, etc.)

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
    +-- debug::Session (COM wrapper, ComPtr<T>)
    |       Session.cpp            — lifecycle: initialize, launch, attach, resume, shutdown,
    |                                stepping, thread control, symbol path, breakpoint API
    |       SessionInspection.cpp  — variable inspection: getStackTrace, getLocals,
    |                                getVariableChildren, evaluateExpression, CaptureOutputCallback
    |       SessionPrettyPrint.cpp — type formatters: prettyPrint, formatSymbolValue,
    |                                readTargetString, parseHexAddress, findChildByName
    +-- debug::BreakpointManager (DAP-to-dbgeng BP mapping)
    +-- debug::Callbacks (COM callbacks, write to State)
    +-- debug::Loader (sidecar DLL loader)
    +-- dap::Reader (stdin thread, pushes to FIFO)
    +-- dap::Types (DAP message builders, DynObj alias)
```

### Module Inventory

| Module | Location | Responsibility |
|--------|----------|----------------|
| Whatdbg | `Source/Whatdbg.h/.cpp` | Main loop. Owns all objects. Drains FIFO, dispatches commands via table, polls WaitForEvent, writes stdout. |
| debug::State | `Source/debug/State.h` | SSOT. Execution state, pending events, breakpoint data. Main-thread-local, no atomics. Derives from Context<State>. |
| debug::Session | `Source/debug/Session.h` | COM wrapper interface. ComPtr<T>. |
| debug::Session (lifecycle) | `Source/debug/Session.cpp` | initialize, launch, attach, resume, pollEvents, shutdown, stepping, thread ops, breakpoint API, symbol path. |
| debug::Session (inspection) | `Source/debug/SessionInspection.cpp` | getStackTrace, getLocals, getVariableChildren, evaluateExpression, enumerateSymbols helper, CaptureOutputCallback. |
| debug::Session (pretty-print) | `Source/debug/SessionPrettyPrint.cpp` | debug::detail namespace: prettyPrint, formatSymbolValue, stripDecimalPrefix, readTargetString, parseHexAddress, findChildByName, getChildValueText. |
| debug::PrettyPrint | `Source/debug/PrettyPrint.h` | Shared declarations for debug::detail functions used across Inspection and PrettyPrint. |
| debug::BreakpointManager | `Source/debug/BreakpointManager.h/.cpp` | DAP-to-dbgeng breakpoint mapping. handleSetBreakpoints(), tryResolve(), onModuleLoad(), onBreakpointHit(). |
| debug::Callbacks | `Source/debug/Callbacks.h/.cpp` | COM callback classes. Write to State during WaitForEvent. |
| debug::Loader | `Source/debug/Loader.h/.cpp` | LoadLibrary from sidecar path, resolve DebugCreate. |
| dap::Reader | `Source/dap/Reader.h/.cpp` | juce::Thread. Reads stdin, parses Content-Length framing, pushes to AbstractFifo. |
| dap::Types | `Source/dap/Types.h` | DAP message builders using juce::var/DynamicObject. DynObj alias (single definition). |

---

## Data Flow

### DAP Command (stdin to stdout)

```
stdin -> [Reader thread] -> FIFO -> [Main thread] -> dispatch table -> write stdout
```

### Debug Event (COM callback to stdout)

```
WaitForEvent -> [COM callback on main thread] -> write to State -> main loop reads -> write stdout
```

### Breakpoint Resolution

```
setBreakpoints arrives in FIFO
    -> Main thread pops, calls BreakpointManager.handleSetBreakpoints()
    -> tryResolve() calls GetOffsetByLine (COM, same thread)
    -> Immediate result: verified or pending
    -> Write DAP response to stdout
    -> Pending BPs resolved later on LoadModule callback
```

### Variable Inspection

```
stackTrace -> assigns unique frameIds (stored in frameIdMap)
scopes     -> allocates variablesReference for locals scope
variables  -> looks up (frameIndex, symbolIndex) from variablesRefMap
           -> getLocals or getVariableChildren via IDebugSymbolGroup2
           -> prettyPrint dispatches to per-type formatters
           -> child refs allocated for expandable variables
```

### Expression Evaluation

```
evaluate -> evaluateExpression -> secondary IDebugClient + CaptureOutputCallback
         -> ?? command output captured -> stripDecimalPrefix applied
         -> juce::String: Evaluate(.text.data) -> readTargetString for actual content
```

### OutputDebugString

```
OutputCallbacks::Output2(DEBUG_OUTCB_TEXT, DEBUG_OUTPUT_DEBUGGEE)
    -> state->debuggeeOutputText accumulated
    -> state->hasDebuggeeOutput = true
    -> main loop: emit DAP output event (category: console)
```

---

## Main Loop

```
while (running)
{
    // 1. Drain FIFO
    while (fifo has message)
        handleCommand (message)  // dispatch via commandHandlers table

    // 2. Poll dbgeng (skip when target stopped)
    if (target is running)
        WaitForEvent (0, 50)
        -> step completion detection
        -> pause completion detection

    // 3. Process deferred events
    if (breakpoint hit)
        -> user BP: emit stopped/breakpoint
        -> internal BP (stepOut gu): emit stopped/step
    if (step completed)
        emit stopped/step
    if (module loaded with pending BPs)
        resolve pending, emit breakpoint changed events
        resume target
    if (debuggee output)
        emit output event
    if (process exited)
        emit exited + terminated events
}
```

---

## State Management

### debug::State

Main-thread-local. No atomics (except what FIFO handles). Plain data.

- `ExecutionState executionState` — Idle, Launching, Running, Stopped, Exited
- `bool isInitialBreakSeen` — set on first EXCEPTION_BREAKPOINT
- `bool isInitialBreakHandled` — permanent flag: distinguishes first BP from subsequent ones across WaitForEvent cycles
- `int processExitCode`
- `ULONG targetProcessId` — set on CreateProcess callback; used by interrupt()
- Deferred event fields: breakpoint engine ID, step completed, module loaded, debuggee output, process exited

Since State is main-thread-only, it uses plain types. No CriticalSection, no std::atomic. Context<State> provides global access.

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

**Context:** else-if chain of 16 branches in handleCommand.

**Decision:** `std::unordered_map<std::string, CommandHandler>` initialized in constructor. handleCommand does a single map lookup.

**Rationale:** O(1) dispatch, no branch cascade, new commands added by inserting one map entry.

### Decision: Session Split (three files)

**Context:** Session.cpp grew to 1228 lines spanning lifecycle, inspection, and type formatting concerns.

**Decision:** Split into Session.cpp (lifecycle), SessionInspection.cpp (variable inspection), SessionPrettyPrint.cpp (type formatters). Shared declarations in PrettyPrint.h (debug::detail namespace).

**Rationale:** Each file has one concern. Session.h interface unchanged — the split is implementation-only.

### Decision: Symbol Group Cache

**Context:** IDebugSymbolGroup2 is expensive to create. scopes and variables requests arrive sequentially for the same frame.

**Decision:** Cache the last created group by frameIndex. Invalidate on every stop event via resetSymbolGroupCache().

**Rationale:** Eliminates repeated SetScopeFrameByIndex + GetScopeSymbolGroup2 calls per variable request.

### Decision: Terminate vs Disconnect

**Context:** DAP specifies separate `terminate` and `disconnect` commands with optional `terminateDebuggee`.

**Decision:** Both map to handleDisconnect. `shouldTerminateOnExit` flag set based on command name or `terminateDebuggee` argument. Session::shutdown(shouldTerminate) calls DEBUG_END_ACTIVE_TERMINATE or DEBUG_END_ACTIVE_DETACH.

### Decision: DynObj Alias in dap::Types

**Context:** `DynObj` (ReferenceCountedObjectPtr<DynamicObject>) was redefined in three files.

**Decision:** Single definition in dap::Types.h. All files that need it import via `using dap::DynObj`.

---

## Stepping Implementation

- `next` (step over): `SetExecutionStatus(DEBUG_STATUS_STEP_OVER)` + `isStepPending = true`
- `stepIn` (step into): `SetExecutionStatus(DEBUG_STATUS_STEP_INTO)` + `isStepPending = true`
- `stepOut` (step out): `Execute("gu")` — runs until return; the resulting internal BP is detected by checking `isUserBreakpoint(engineId)` in main loop
- Step completion detected when WaitForEvent returns S_OK with no breakpoint/module/exit flags set while `isStepPending`
- EXCEPTION_SINGLE_STEP also sets `hasStepCompleted` directly

## Pause Implementation

- `pause`: `DebugBreakProcess(OpenProcess(pid))` + `isPausePending = true`
- Pause detected when WaitForEvent returns S_OK with no other events while `isPausePending`
- Emits stopped/pause event with current event thread system ID

## Multi-Thread Support

- `getThreads()`: IDebugSystemObjects::GetThreadIdsByIndex, GetThreadDescription per thread
- `getEventThreadSystemId()`: GetEventThread -> system ID lookup
- `setCurrentThreadBySystemId()`: GetThreadIdBySystemId -> SetCurrentThreadId
- Frame ID map: stackTrace assigns unique IDs per frame, stores (threadSystemId, frameIndex)
- scopes/variables restore thread context via setCurrentThreadBySystemId before inspecting

---

## File Structure

```
whatdbg/
    CMakeLists.txt
    ARCHITECTURE.md
    PLAN.md
    install.sh
    build.bat
    Source/
        Main.cpp                        Entry point, sidecar extraction
        Log.h                           Shared logging (#if JUCE_DEBUG guard)
        Whatdbg.h / .cpp                Main loop, dispatch table, event processing
        debug/
            State.h                     SSOT, main-thread-local
            Session.h                   COM wrapper interface
            Session.cpp                 Lifecycle, stepping, thread ops, breakpoint API
            SessionInspection.cpp       Stack trace, locals, children, expression eval
            SessionPrettyPrint.cpp      Type formatters (debug::detail namespace)
            PrettyPrint.h               Shared declarations for debug::detail
            Loader.h / .cpp             Sidecar DLL loader
            Callbacks.h / .cpp          COM callbacks -> State
            BreakpointManager.h / .cpp  DAP-to-dbgeng BP mapping
        dap/
            Reader.h / .cpp             stdin thread + FIFO
            Types.h                     DAP message builders, DynObj alias
    Resources/
        windows/
            dbgeng.dll, dbghelp.dll, dbgcore.dll, symsrv.dll
        macos/                      (gitignored — built by scripts/build-liblldb-mac.sh)
            liblldb/
                liblldb.dylib       (universal — arm64 + x86_64)
                include/lldb/API/   (SB API headers)
                licenses/LLVM-LICENSE.TXT
    Builds/                         (gitignored — JUCE output + liblldb build machinery)
        Ninja/                      (JUCE project build)
        liblldb/                    (LLVM source clone + cmake tree)
            llvm-project/           (pinned clone of llvm/llvm-project at LLVM_TAG)
            cmake/                  (out-of-source cmake build tree)
    scripts/
        build-liblldb-mac.sh        (pinned LLVM build — writes Builds/liblldb + Resources/macos/liblldb)
    modules/
        jreng_core/                     Context, Owner, utilities
    carol/
        MANIFESTO.md
        NAMES.md
        SPRINT-LOG.md
```

---

## Glossary

| Term | Definition |
|------|------------|
| DAP | Debug Adapter Protocol. JSON-RPC over stdin/stdout. |
| dbgeng | Windows Debug Engine COM API. |
| Sidecar | Pinned DLLs embedded in binary, extracted at runtime. |
| WaitForEvent | Blocking dbgeng call that processes debug events. |
| SSOT | Single Source of Truth. |
| FIFO | First In First Out queue. AbstractFifo + std::vector. |
| SPSC | Single Producer Single Consumer. |
| DynObj | `juce::ReferenceCountedObjectPtr<juce::DynamicObject>`. Alias defined in dap::Types. |
| prettyPrint | Per-type value formatter for juce::String, std::string, std::unique_ptr, std::vector. |
| dispatch table | `std::unordered_map<std::string, CommandHandler>` replacing the else-if chain in handleCommand. |

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-03-30 | COUNSELOR | Initial draft (three-thread model) |
| 0.2 | 2026-03-31 | COUNSELOR | Simplified to two-thread model, dropped JUCE message system |
| 0.3 | 2026-04-01 | MACHINIST | Session split (3 files), variable inspection, expression eval, OutputDebugString, pause, multi-thread, stepping, terminate/disconnect, symbol group cache, dispatch table, DynObj consolidation |

---

**End of Architecture Document**

**JRENG!**
