# whatdbg - Architecture

**Purpose:** Single source of truth for project structure, patterns, and contracts.

**Status:** Active

**Last Updated:** 2026-03-31

**Version:** 0.2.0

---

## Project Overview

### Purpose

whatdbg is a DAP (Debug Adapter Protocol) adapter for debugging JUCE audio plugins loaded in DAWs on Windows. It uses the dbgeng COM API to attach to or launch host processes, then bridges DAP requests from nvim-dap to dbgeng operations over stdin/stdout.

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
- `juce::HeapBlock<juce::var>` — flat backing storage, single allocation
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
    +-- debug::BreakpointManager (DAP-to-dbgeng BP mapping)
    +-- debug::Callbacks (COM callbacks, write to State)
    +-- debug::Loader (sidecar DLL loader)
    +-- dap::Reader (stdin thread, pushes to FIFO)
    +-- dap::Types (DAP message builders)
```

### Module Inventory

| Module | Location | Responsibility |
|--------|----------|----------------|
| Whatdbg | `Source/Whatdbg.h/.cpp` | Main loop. Owns all objects. Drains FIFO, dispatches commands, polls WaitForEvent, writes stdout. |
| debug::State | `Source/debug/State.h` | SSOT. Execution state, pending events, breakpoint data. Main-thread-local, no atomics. Derives from Context<State>. |
| debug::Session | `Source/debug/Session.h/.cpp` | Dumb COM wrapper. ComPtr<T>. launch(), attach(), resume(), pollEvents(), shutdown(). No state. |
| debug::BreakpointManager | `Source/debug/BreakpointManager.h/.cpp` | DAP-to-dbgeng breakpoint mapping. handleSetBreakpoints(), tryResolve(), onModuleLoad(), onBreakpointHit(). |
| debug::Callbacks | `Source/debug/Callbacks.h/.cpp` | COM callback classes. Write to State during WaitForEvent. |
| debug::Loader | `Source/debug/Loader.h/.cpp` | LoadLibrary from sidecar path, resolve DebugCreate. |
| dap::Reader | `Source/dap/Reader.h/.cpp` | juce::Thread. Reads stdin, parses Content-Length framing, pushes to AbstractFifo. |
| dap::Types | `Source/dap/Types.h` | DAP message builders using juce::var/DynamicObject. |

---

## Data Flow

### DAP Command (stdin to stdout)

```
stdin -> [Reader thread] -> FIFO -> [Main thread] -> execute -> write stdout
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

---

## Main Loop

```
while (running)
{
    // 1. Drain FIFO
    while (fifo has message)
        handleCommand (message)

    // 2. Poll dbgeng (skip when target stopped)
    if (target is running)
        WaitForEvent (0, 50)

    // 3. Process deferred events
    if (breakpoint hit)
        emit stopped event
    if (module loaded with pending BPs)
        resolve pending, emit breakpoint changed events
    if (process exited)
        emit exited + terminated events
}
```

---

## State Management

### debug::State

Main-thread-local. No atomics (except what FIFO handles). Plain data.

- `ExecutionState executionState` — Idle, Launching, Running, Stopped, Exited
- `bool isInitialBreakSeen`
- `int processExitCode`
- Deferred event fields (breakpoint stop body, module load flag, exit flag)

Since State is main-thread-only, it can use plain types. No CriticalSection, no std::atomic. Context<State> provides global access.

---

## Key Design Decisions

### Decision: Two-Thread Model (not three, not event-driven)

**Context:** Initial design attempted three threads (stdin, COM, message) with callAsync notifications. This caused: COM thread isolation issues, callback flooding, message thread starvation, timing bugs.

**Decision:** Two threads. Main thread owns everything. Stdin thread is a FIFO buffer. Same proven architecture as the working legacy adapter.

**Rationale:** Simple. Proven. No cross-thread state synchronization needed. COM lives on one thread. JUCE provides the FIFO (AbstractFifo), thread (juce::Thread), and utilities.

### Decision: AbstractFifo for stdin buffering

**Context:** stdin read is blocking. Main thread cannot block on stdin and WaitForEvent simultaneously.

**Decision:** juce::AbstractFifo (lock-free SPSC) with juce::HeapBlock backing storage. 64 slots.

**Rationale:** JUCE-native. Lock-free. Single allocation. No hand-rolled queue.

### Decision: No JUCE Message Thread

**Context:** Initial design used juce::MessageManager::runDispatchLoop() and callAsync for event delivery.

**Decision:** Skip JUCE message/event system entirely. Main thread runs its own loop. No initialiseJuce_GUI, no runDispatchLoop, no callAsync.

**Rationale:** We are a console app with a simple main loop. JUCE message thread adds complexity for no benefit. We use JUCE for utilities (File, String, JSON, Thread, AbstractFifo, ComPtr), not its event system.

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
        Main.cpp                    Entry point, sidecar extraction
        Log.h                       Shared logging
        Whatdbg.h / .cpp            Main loop, command dispatch, event processing
        debug/
            State.h                 SSOT, main-thread-local
            Session.h / .cpp        COM wrapper (ComPtr<T>)
            Loader.h / .cpp         Sidecar DLL loader
            Callbacks.h / .cpp      COM callbacks -> State
            BreakpointManager.h / .cpp  DAP-to-dbgeng BP mapping
        dap/
            Reader.h / .cpp         stdin thread + FIFO
            Types.h                 DAP message builders
    Resources/
        windows/
            dbgeng.dll, dbghelp.dll, dbgcore.dll, symsrv.dll
    modules/
        jreng_core/                 Context, Owner, utilities
    carol/
        ARCHITECTURAL-MANIFESTO.md
        NAMING-CONVENTION.md
        JRENG-CODING-STANDARD.md
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
| FIFO | First In First Out queue. AbstractFifo + HeapBlock. |
| SPSC | Single Producer Single Consumer. |

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-03-30 | COUNSELOR | Initial draft (three-thread model) |
| 0.2 | 2026-03-31 | COUNSELOR | Simplified to two-thread model, dropped JUCE message system |

---

**End of Architecture Document**

**JRENG!**
