# whatdbg - Architecture
## Cross-Platform DAP Debug Adapter (Windows dbgeng / macOS liblldb)

**Repository:** https://github.com/jrengmusic/whatdbg

**Purpose:** Single source of truth for project structure, patterns, and contracts.

**Status:** Active

**Last Updated:** 2026-11-14

**Version:** 0.7.0

---

## Project Overview

### Purpose

whatdbg is a cross-platform DAP (Debug Adapter Protocol) debug adapter for C/C++ development with neovim. On Windows it uses the dbgeng COM API (the engine behind WinDbg); on macOS it uses the liblldb SB API (LLVM's scriptable interface). Both backends bridge DAP requests from nvim-dap to native debug engine operations over stdin/stdout.

### Architecture Philosophy

- **JUCE-native:** Leverage JUCE's battle-tested infrastructure. No hand-rolled abstractions, no manual callbacks, no manual threading primitives.
- **Audio processor shape:** `Whatdbg` is main. It owns the Model, the processor, the writer, and the reader, and it listens on the message thread. `Processor` owns the engine atoms on its own serial context and runs each request and each stop there. The engine context is the hot path; the message thread is the traffic orchestrator.
- **SSOT:** `WHATDBGModel` (a `jam::Model`) holds every scalar of the session state. The registries live in the atom that owns them (`VariableManager`, `BreakpointManager`).
- **One-way data flow:** stdin → `dap::Reader` → `requests` lane → engine context → `messages` lane and Model parameters → message thread → stdout.

### Technology Stack

- **Language:** C++17
- **Framework:** JUCE 8.0.17 (`juce_core`, `juce_data_structures`, `juce_events`, `juce_graphics`, `juce_gui_basics`, `juce_audio_processors`) and JAM (`jam_core`, `jam_document`, `jam_markdown`, `jam_freetype`, `jam_graphics`, `jam_data_structures`, `jam_animation`, `jam_web`, `jam_style`, `jam_gui`, `jam_mermaid_diagram`, `jam_markdown_graphics`, `jam_terminal`, `jam_tui`), as `project-info.md` lists them
- **Build System:** `cast` generates `CMakeLists.txt` and `Source/generated/*` from `project-info.md` and `cast/*.md`. CMake + Ninja underneath. Do not edit `CMakeLists.txt` or `Source/generated/*` by hand.
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

### Three Threads

| Thread | Owns | Never does |
|---|---|---|
| Message thread (the main thread, the JUCE message loop) | `Whatdbg`, `WHATDBGModel`, `dap::Writer`, stdout | an engine call |
| Engine context (`Processor`, a `jam::AsyncUpdater` context) | `debug::Session`, `debug::BreakpointManager`, `debug::VariableManager`, `debug::StopManager`, the `commands` table | a write to stdout; a touch of the `juce::ValueTree` |
| Reader thread (`dap::Reader`, a `juce::Thread`) | stdin | anything else, except `Processor::addRequest`, which calls `session.wake ()` and `session.cancel ()` |

The engine context is the serial context of `jam::AsyncUpdater`: a serial GCD queue on macOS, a thread pool with one thread on Windows (`jam_AsyncUpdater.h:18-21`). Each engine call, `Session::initialize` and `Session::shutdown` included, runs on it. DbgEng: "Clients are specific to the thread that created them. Calls from other threads fail immediately."

### The message loop

`Whatdbg` derives from `juce::JUCEApplicationBase`. `runAdapter` (`Main.cpp`) sets `juce::JUCEApplicationBase::createInstance` and calls `juce::JUCEApplicationBase::main` with the sidecar path as the one parameter. `JUCEApplicationBase::main` creates `NSApp` on macOS (`juce_ApplicationBase.cpp:243`), makes the `ScopedJuceInitialiser_GUI`, creates `Whatdbg`, calls `initialise`, and runs `runDispatchLoop` (`:263-282`). On macOS `runDispatchLoop` is `[NSApp run]` (`juce_MessageManager_mac.mm:323-351`), so a loop with no `NSApp` returns at once. The application object is the only way to start it. `--help` and the macOS re-exec run in `main` before the application.

### The lanes

`dap::FIFO<MessageType>` (`Source/dap/FIFO.h`) is a `juce::AbstractFifo` with a `std::vector<MessageType>` storage, sized at construction. A write to a full lane asserts and does not write the message; a Release build then drops it with no signal.

| Lane | Type | Capacity | Writer | Reader |
|---|---|---|---|---|
| `requests` | `dap::FIFO<jam::JsonDocument>` | 64 | message thread (`prepare`, `releaseResources`), then the reader thread | engine context (`process`) |
| `messages` | `dap::FIFO<std::string>` | 4096 | engine context (`addMessage`) | message thread (`applyMessages`) |

The `requests` lane has three writers in sequence, never two at one time: `prepare` writes before `reader.start ()`; then only the reader thread writes; `releaseResources` writes after the reader stopped.

### Thread Safety Rules

- The engine context makes every engine call. The message thread makes none.
- The reader thread calls only `Processor::addRequest`. Its two engine calls are `Session::wake` and `Session::cancel`, the engine's own any-thread calls: `SetInterrupt` "can be called at any time and from any thread"; on macOS a broadcast to the listener.
- `Parameter::store`, `getValue`, and `Model::storeValue` are lock-free on any thread (`jam_Model.h:14`). `flush`, the tree, and the attachments are message-thread only (`jam_Model.h:15-16`).
- Only `Id::numMessages` and `Id::isRunning` use `setValue`, which reaches the message thread through `jam::Model::ParameterAttachment`. Each other parameter uses `storeValue`.
- On Windows the COM callbacks fire synchronously inside `WaitForEvent`, on the engine context.

---

## Module Structure

### Module Map

```
Whatdbg (juce::JUCEApplicationBase; main, message thread)
    |
    +-- WHATDBGModel (jam::Model; SESSION and EXCEPTION rows)
    +-- Processor (jam::AsyncUpdater; engine context)
    |       +-- debug::Session (debug engine wrapper)
    |       |       [Windows]
    |       |       Session.cpp              — lifecycle: initialize, launch, attach, resume, shutdown,
    |       |                                  wake, cancel, resetCancel, stepping, thread control,
    |       |                                  symbol path, breakpoint API; variable inspection
    |       |       SessionPrettyPrint.cpp   — type formatters
    |       |       Callbacks.cpp            — COM callbacks, report through Session::events
    |       |       Loader.cpp               — sidecar DLL loader
    |       |       [macOS]
    |       |       Session_mac.cpp          — lifecycle, wake broadcaster, stepping, thread control,
    |       |                                  breakpoint API, variable inspection (SBValue tree)
    |       |       SessionPrettyPrint_mac.cpp — type formatters
    |       +-- debug::BreakpointManager (DAP-to-engine breakpoint registry)
    |       +-- debug::VariableManager (frameId and variablesReference registry)
    |       +-- debug::StopManager (the stops table and the stop drains)
    |       +-- dap::FIFO requests, dap::FIFO messages
    +-- dap::Writer (stdout, message thread)
    +-- dap::Reader (stdin thread)
dap::Types (DAP message builders over jam::JsonWriter)
```

### Module Inventory

| Module | Location | Responsibility |
|--------|----------|----------------|
| Whatdbg | `Source/Whatdbg.h/.cpp` | Main. `juce::JUCEApplicationBase` and `juce::ValueTree::Listener`. Owns `WHATDBGModel`, `Processor`, `dap::Writer`, `dap::Reader`, the `events` map, and two attachments (`numMessagesAttachment`, `isRunningAttachment`). `initialise` calls `processor.prepare` and `reader.start`. Two `events` entries: `Id::numMessages` drains the `messages` lane to the writer; `Id::isRunning` at 0 sets the application return value from `Id::adapterExitCode` and calls `quit`. |
| WHATDBGModel | `Source/WHATDBGModel.h/.cpp` | `jam::Model` and `jam::Instance<WHATDBGModel>`. Root `Id::whatdbg`; rows `sessionTag` (`Id::session`) and `exceptionTag` (`Id::exception`). The constructor creates every parameter and stops the Model timer. |
| Processor | `Source/Processor.h/.cpp` | `private jam::AsyncUpdater`. Owns the atoms and both lanes. `prepare` and `releaseResources` write a request and wait in `handleUpdateNow`. `addRequest` (reader thread) runs the `cancellations` entry, writes the `requests` lane, triggers the context, and wakes the session. `process` runs each request, polls the engine, runs the stop drain, and checks the terminate deadline. Holds the eighteen DAP command bodies, `onPrepare`, `onReleaseResources`, `drainProcessExited`, `drainTerminateTimeout`, `getEndModeForExit`, and the engine `events` entries that are no stop. |
| debug::Session | `Source/debug/Session.h` | Cross-platform engine wrapper interface. `ComPtr<T>` (Windows) or SB API value types (macOS) behind one shared header. `events` is the `debug::Events` map through which the native code reports. |
| debug::Session (Win) | `Source/debug/Session.cpp` | initialize, launch, attach, resume, pollEvents, shutdown, wake (no statement), cancel (`SetInterrupt (DEBUG_INTERRUPT_EXIT)`), resetCancel (`GetInterrupt`), stepping, thread ops, breakpoint API, symbol path, variable inspection, expression evaluation. |
| debug::Session pretty-print (Win) | `Source/debug/SessionPrettyPrint.cpp` | Formatters declared in `PrettyPrint.h`: prettyPrint, formatSymbolValue, stripDecimalPrefix, readTargetString, findChildByName, getChildValueText. |
| debug::Session (mac) | `Source/debug/Session_mac.cpp` | liblldb SB API: initialize, launch, attach, resume, pollEvents, shutdown, wake (`broadcaster.BroadcastEventByType (wakeEventMask)`), cancel (`wake`), resetCancel (no statement), stepping, thread ops, breakpoint API, variable inspection, expression evaluation. |
| debug::Session pretty-print (mac) | `Source/debug/SessionPrettyPrint_mac.cpp` | Type formatters for juce::String, unique_ptr, and filtered types via SB API. |
| debug::PrettyPrint | `Source/debug/PrettyPrint.h` | Shared helpers: `shouldSkipSymbol`, `parseHexAddress`. |
| State.h (namespace `debug`) | `Source/debug/State.h` | The enums `ExecutionState`, `StopReason`, `OffsetStatus`, the alias `Events`, and `getExceptionName`. Holds no state. |
| debug::BreakpointManager | `Source/debug/BreakpointManager.h/.cpp` | DAP-to-engine breakpoint registry. `onSetBreakpoints`, `onModuleLoad`, `onReload`, `appendBreakpointStoppedEvent`, `onBreakpointLocationFound`. Private resolution machinery: `getBreakpointOffset`, `setBreakpointLocation`, `onSymbolsLoaded`, `hasUnresolvedBreakpoints`. |
| debug::VariableManager | `Source/debug/VariableManager.h/.cpp` | The frameId and variablesReference registry: `onStackTrace`, `onScopes`, `onVariables`, `selectFrameFromId`, `resetVariablesState`. |
| debug::StopManager | `Source/debug/StopManager.h/.cpp` | The `stops` table keyed by `StopReason`, the six stop drains, and the stop `events` entries. `onStop (targetProgram)` returns the DAP messages of the current stop. |
| debug::Callbacks | `Source/debug/Callbacks.h/.cpp` | COM callback classes. Report through `Session::events` during `WaitForEvent`. [Windows only] |
| debug::Loader | `Source/debug/Loader.h/.cpp` | LoadLibrary from sidecar path, resolve DebugCreate. [Windows only] |
| dap::Reader | `Source/dap/Reader.h/.cpp` | `juce::Thread`. Reads stdin, parses Content-Length framing into `jam::JsonDocument`, and calls `onMessage`. Holds no lane. |
| dap::Writer | `Source/dap/Writer.h/.cpp` | `writeMessage (std::string_view)`: the Content-Length frame to stdout. |
| dap::FIFO | `Source/dap/FIFO.h` | The lane template. |
| dap::Types | `Source/dap/Types.h/.cpp` | DAP message builders over `jam::JsonWriter`: `appendResponse`, `appendErrorResponse`, `appendEvent`, `appendBreakpointChangedEvent`, `appendSource`, `appendCapabilities`, `getRequest`, `getStoppedEvent`, `getExceptionStoppedEvent`, `getExceptionOutputEvent`, `getProcessEvent`, `getOutputEvent`, `getExceptionDescription`. `nextSeq` reads and stores `Id::nextSequenceNumber`. |
| Help page | `Source/HELP.md`, `jam::TerminalPage` (`jam/jam_tui/page/jam_TerminalPage.h/.cpp`) | `whatdbg --help` parses `HELP.md` with `jam::MarkdownDocument`, paints it through `jam::TerminalPage`, a `juce::Component` that paints with the terminal graphics context, and writes the frame to stdout. The style sheet is `jam/jam_tui/page/style.css`, embedded as BinaryData. |

---

## Data Flow

### DAP Request (stdin to stdout)

```
stdin -> [Reader thread] -> onMessage -> Processor::addRequest -> requests lane
      -> [engine context] process -> onCommand -> commands table -> on<Command>
      -> addMessage -> messages lane, setValue (Id::numMessages)
      -> [message thread] attachment -> model.flush -> Whatdbg events (Id::numMessages)
      -> processor.applyMessages -> writer.writeMessage -> stdout
```

### Debug Event (engine to stdout)

```
[Windows] WaitForEvent -> COM callback (Callbacks.cpp) -> session.events.get (Id::...)
[macOS]   session.pollEvents -> onProcessEvent / onBreakpointEvent (Session_mac.cpp) -> session.events.get (Id::...)
                                       |
          StopManager entries: model.storeValue (stopReason, payload)
          Processor entries: exit, running, output, breakpoint location -> messages lane
                                       |
          process: stopManager.onStop (targetProgram) -> one stop drain -> messages lane
```

The `events` entries, by key:

| Key | Owner | Effect |
|---|---|---|
| `initialBreak` | StopManager | `stopReason = initialBreak`, `executionState = stopped` |
| `pause` | StopManager | `isPausePending = 0`, `stopReason = pause`, `executionState = stopped` |
| `exception` | StopManager | `stopReason = exception`, `exceptionCode`, `exceptionAddress`, `isMachException`, `executionState = stopped` |
| `breakpoint` | StopManager | `stopReason = breakpoint`, `breakpointEngineId`, `executionState = stopped` |
| `step` | StopManager | `isStepPending = 0`, `executionState = stopped`, `stopReason = step` when it is `none` |
| `moduleLoad` | StopManager | `stopReason = moduleLoad`; keeps the image name |
| `targetProcessId` | Processor | stores `targetProcessId` |
| `exited` | Processor | stores `processExitCode`, then `drainProcessExited` |
| `terminated` | Processor | `drainProcessExited` |
| `running` | Processor | `executionState = running` |
| `breakpointLocationFound` | Processor | `breakpointManager.onBreakpointLocationFound` messages |
| `console`, `standardOutput`, `standardError` | Processor | one `output` event for each report |

### Breakpoint Resolution

```
setBreakpoints -> Processor::onSetBreakpoints -> BreakpointManager::onSetBreakpoints
    -> Session::addBreakpointByLocation, writes engineId/resolvedLine on success
       [Windows] Session::getOffsetStatus / Session::getOffset, then Session::addBreakpoint
       [macOS]   SBTarget::BreakpointCreateByLocation
    -> response: verified or pending
    -> onReload () messages; it reloads symbols only when an entry is unresolved
    -> [Windows] pending entries resolve on module load: LoadModule -> stopReason moduleLoad
       -> StopManager::drainModuleLoaded -> BreakpointManager::onModuleLoad -> onSymbolsLoaded
    -> [macOS] liblldb resolves pending entries: onBreakpointEvent -> events (breakpointLocationFound)
       -> BreakpointManager::onBreakpointLocationFound
```

### Variable Inspection

```
stackTrace -> VariableManager::onStackTrace assigns unique frameIds (frameId -> threadSystemId, frameIndex)
scopes     -> VariableManager::onScopes allocates the variablesReference of the locals scope
variables  -> VariableManager::onVariables looks up (frameIndex, symbolIndex) in variablesRefMap
           [Windows] Session::applyToLocals / applyToVariableChildren via IDebugSymbolGroup2
           [macOS]   Session::applyToLocals / applyToVariableChildren via SBValue tree
           -> prettyPrint dispatches to per-type formatters
           -> child refs allocated for expandable variables
A stop drain calls resetVariablesState.
```

### Expression Evaluation

```
[Windows]
evaluate -> Session::evaluateExpression -> scope symbol group (frameIndex)
         -> AddSymbol (expression) with SYMOPT_NO_PUBLICS; success only when AddSymbol returns S_OK
         -> empty result: Processor::onEvaluate sends the error response "Could not evaluate: <expr>"

[macOS]
evaluate -> Session::evaluateExpression -> SBFrame::EvaluateExpression on the frame at frameIndex
         -> result formatted through the same prettyPrint dispatch as variable inspection
```

### Debuggee Output Capture

```
[Windows] OutputCallbacks::Output2 -> session.events.get (Id::console, text)
[macOS]   drainProcessStdio (Session_mac.cpp) reads GetSTDOUT / GetSTDERR
          -> session.events.get (Id::standardOutput or Id::standardError, text)
-> Processor entry -> one DAP output event for each report
```

### Exception Flow

```
[Windows] second-chance exception -> onUnknownException (Callbacks.cpp) -> events (exception)
[macOS]   signal or Mach exception stop -> onSignalStop / onExceptionStop (Session_mac.cpp) -> events (exception)
  -> StopManager entry stores stopReason, exceptionCode, exceptionAddress, isMachException
  -> process -> StopManager::drainExceptionStopped
      -- DAP stopped event (reason exception), then DAP output event (category stderr)
  -> nvim-dap requests stackTrace + exceptionInfo
  -> Processor::onStackTrace / onExceptionInfo respond
```

---

## The Engine Context

```
Processor::process ()
    1. requests.applyPendingData -> onCommand (request)
    2. when executionState is running or launching, or a terminate deadline is pending:
           session.pollEvents (pollTimeoutMs)          // 50 ms; macOS rounds up to 1 s
    3. addMessages (stopManager.onStop (targetProgram))  // one stop drain, by stopReason
    4. drainTerminateTimeout ()
    5. when isRunning is not 0 and (executionState is running or launching,
       or a terminate deadline is pending): triggerAsyncUpdate ()
```

While the target is stopped and no terminate is pending, statement 5 queues nothing, and the context has no work. The next request starts it. On macOS `Session::wake` ends a `pollEvents` wait at once when a request arrives while the target runs.

`stopReason` holds one stop cause at a time, so one drain runs per tick. `drainBreakpointHit` asks `BreakpointManager::appendBreakpointStoppedEvent` for the stopped event. That event has `reason: "step"` when no registry entry holds the engine id, as for the internal stepOut breakpoint.

### Start

1. `Whatdbg::initialise` calls `processor.prepare (sidecarDirectory)`.
2. `prepare` writes `dap::getRequest (prepare)` to `requests` and waits in `handleUpdateNow`.
3. On the context, `onPrepare` calls `session.initialize`. On failure it stores `1` to `Id::adapterExitCode` and sets `Id::isRunning` to 0.
4. `initialise` calls `reader.start ()`. The message loop then runs.

### End

1. A drain, `onDisconnect`, or `onPrepare` sets `Id::isRunning` to 0 on the context.
2. The attachment posts one message. On the message thread the flush changes the tree, and the `Id::isRunning` entry sets the return value and calls `quit`.
3. The members of `Whatdbg` die in reverse order: the attachments, the reader (its thread stops), the writer, the processor.
4. `~Processor` calls `releaseResources`: `cancelPendingUpdate`, a `releaseResources` request, and `handleUpdateNow`. On the context `onReleaseResources` calls `session.shutdown (getEndModeForExit ())` and stores `ExecutionState::idle`.

---

## State Management

### WHATDBGModel

| Row | Parameter | Type | Default |
|---|---|---|---|
| SESSION | `Id::executionState` | int | `ExecutionState::idle` |
| SESSION | `Id::stopReason` | int | `StopReason::none` |
| SESSION | `Id::processExitCode` | int | 0 |
| SESSION | `Id::targetProcessId` | int64 | 0 |
| SESSION | `Id::terminateDeadlineMs` | int64 | 0 |
| SESSION | `Id::isRunning` | int | 1 |
| SESSION | `Id::shouldTerminateOnExit` | int | 0 |
| SESSION | `Id::isConfigurationDone` | int | 0 |
| SESSION | `Id::isStepPending` | int | 0 |
| SESSION | `Id::isPausePending` | int | 0 |
| SESSION | `Id::breakpointEngineId` | int | 0 |
| SESSION | `Id::nextSequenceNumber` | int | 1 |
| SESSION | `Id::numMessages` | int | 0 |
| SESSION | `Id::adapterExitCode` | int | 0 |
| EXCEPTION | `Id::exceptionCode` | int64 | 0 |
| EXCEPTION | `Id::exceptionAddress` | int64 | 0 |
| EXCEPTION | `Id::isMachException` | int | 0 |

A `bool` or an enum is a `jam::Parameter<int>`. A `uint32` or a `uint64` is a `jam::Parameter<int64_t>`.

### The other homes

| State | Home |
|---|---|
| `nextVariablesRef`, `variablesRefMap`, `nextFrameId`, `frameIdMap`, `lastScopesThreadId` | members of `debug::VariableManager` |
| the breakpoint registry and `nextDapId` | members of `debug::BreakpointManager` |
| the image name of the last module load | member of `debug::StopManager` |
| `targetProgram` | member of `Processor`, given to `StopManager::onStop` at the call |

---

## Key Design Decisions

### Decision: Audio processor shape — engine context, message loop, and Model

**Context:** The old record (version 0.6.0 of this document) held three threading decisions: two threads with no event-driven delivery, AbstractFifo stdin buffering, and no JUCE message loop. The first design had three threads (stdin, COM, message) with callAsync notifications. The record names four failure causes: COM thread isolation issues, callback flooding, message thread starvation, and timing bugs. The two-thread loop that replaced it polled the engine, slept 10 ms for each pass of a stopped target, and on macOS made a request wait for the poll to end, up to 1 s.

**Decision:** whatdbg has the shape of an audio processor. `Whatdbg` is main, a `juce::JUCEApplicationBase` on the JUCE message loop. It owns `WHATDBGModel`, `Processor`, `dap::Writer`, and `dap::Reader`. `Processor` owns the atoms on its `jam::AsyncUpdater` context and runs each request and each stop there. The threads, the lanes, and the one-way flow are those of section Threading Model.

**Rationale:**
- ARCHITECT, on the thread owner of the engine: *"crossplatform. lldb, dbgeng should be at its own hot path lock free path this is isomporhic to audio thread, which how we design eve, end with audio architecture discipline, the traffic orchestrator state machine with jam::Model is at message thread"*.
- ARCHITECT, on the shape: *"exactly. the shape, the principle, the contract, not derived. those layers are ENCAPSULATION."* and *"EVEProcessor is `main`, owner of all hot path"*.
- ARCHITECT, on event latency: *"Send at once"*. A message reaches the message thread at once through `setValue` and `jam::Model::ParameterAttachment`.
- The four failure causes, each answered:

| Cause | Answer |
|---|---|
| COM thread isolation issues | The engine context is one thread on Windows: minimum 1, maximum 1. Each engine call, `initialize` and `shutdown` included, runs on it. |
| callback flooding | `juce::AsyncUpdater::triggerAsyncUpdate` posts one message while one is pending. One flush drains each message of the lane. |
| message thread starvation | The message thread makes no engine call and no engine wait. It writes stdout only. |
| timing bugs | One context writes the `messages` lane, so it is the one author of the DAP order. |

- The timings of the smoke suite under scout, Release, macOS: `epoch` median 4.5387e10 ns (±0.12%) against 6.2698e10 ns (±0.327%) before the rewrite; `user` 2.376e9 ns (±0.49%) against 2.169e9 ns (±2.07%); `system` 1.643e9 ns (±0.84%) against 1.563e9 ns (±1.58%). Reports: `benchmark/smoke-report.md`, `benchmark/smoke-report-baseline.md`.

### Decision: Dispatch Table for DAP Commands

**Context:** An earlier revision used an else-if chain across DAP command names.

**Decision:** The `commands` table of `Processor` is a `jam::Function::Map<juce::String, jam::Function::pointerCapacity, void (const jam::JsonDocument&)>`. The `Processor` constructor fills it with `.add (name, lambda)`: the eighteen DAP commands, plus `prepare` and `releaseResources`, the two tells of the lifecycle. `onCommand` does a single map lookup.

**Rationale:** O(1) dispatch, no branch cascade, new commands added by inserting one map entry.

### Decision: Session Split (two files per platform)

**Context:** A single Session file grows past the point where lifecycle, inspection, and type-formatting concerns stay separable.

**Decision:** Split per platform into `Session[_mac].cpp` and `SessionPrettyPrint[_mac].cpp`. `Session[_mac].cpp` holds the lifecycle and the variable inspection. `SessionPrettyPrint[_mac].cpp` holds the type formatters. ARCHITECT: "Merge only". `PrettyPrint.h` declares the type formatters that the Session files call. It also holds the inline helpers `shouldSkipSymbol` and `parseHexAddress`.

**Rationale:** One object is one header and one `.cpp` (CODING.md). Each `Session[_mac].cpp` holds the whole Session object. `Session.h`'s interface is the same on both platforms — the split is implementation-only, selected by `JUCE_WINDOWS`/`JUCE_MAC` at the build-target level.

### Decision: Symbol Group Cache

**Context:** IDebugSymbolGroup2 is expensive to create. scopes and variables requests arrive sequentially for the same frame.

**Decision:** Cache the last created group by frameIndex. Invalidate on every stop event via resetSymbolGroupCache().

**Rationale:** Eliminates repeated SetScopeFrameByIndex + GetScopeSymbolGroup2 calls per variable request.

### Decision: Terminate vs Disconnect

**Context:** DAP specifies separate `terminate` and `disconnect` commands with optional `terminateDebuggee`.

**Decision:** Both route to `Processor::onDisconnect`. `EndMode` (`Source/debug/Session.h`) selects the end. `Session::shutdown (EndMode)` dispatches on three cases:
- `terminate` — kills the debuggee; used when `terminate` arrives, or `disconnect` arrives with `terminateDebuggee: true`. Windows: `DEBUG_END_ACTIVE_TERMINATE`. macOS: `process.Signal (SIGKILL)` followed by `resume()`.
- `detach` — releases the debuggee, which keeps running. Windows: `DEBUG_END_ACTIVE_DETACH`. macOS: `process.Detach()`.
- `passive` — the target has already exited; releases session state only.

The reader thread calls `session.cancel ()` for `disconnect` and `terminate`, before it writes the request to the lane. Windows: `SetInterrupt (DEBUG_INTERRUPT_EXIT)` forces a running `WaitForEvent` to return. The first statement of `onDisconnect` is `session.resetCancel ()`, so `terminateDebuggee` and `shutdown` run with no interrupt registered.

`onDisconnect` sends the response, then the kill or the detach. On the kill path it sets `executionState` to `running` and stores a terminate deadline of 5 s, so the context keeps polling for the exit event. A kill that the engine reports first as a signal stop sets `executionState` to `stopped`; the pending deadline keeps the context polling (`process`, statements 2 and 5), so the exit event that follows is observed: the client receives `stopped` (reason `exception`) for the signal, then `exited` and `terminated`. On an attach session the engine reports the exit only, and the client receives `exited` and `terminated`. `drainTerminateTimeout` ends the session only when no exit event arrives before the deadline. `Session::shutdown` runs in `onReleaseResources`, via `getEndModeForExit()`. `~Session` calls it again, and the `debugger.IsValid()` test makes that second call a no-op.

**macOS zombie fix (2026-09 sprint):** `Session::terminateDebuggee` sends `SIGKILL` via `process.Signal`, then calls `resume()` so the normal `pollEvents` path observes the debuggee's exit through liblldb's own state machine before anything tears the session down. `Session::shutdown`'s `EndMode::terminate` case does the same pair as a fallback. `shutdown` calls `SBDebugger::Destroy` only while `debugger.IsValid()`: `onReleaseResources` calls `shutdown`, and `~Session()` calls it again, so the second call is a no-op. The prior defect (RFC-ZOMBIE-TERMINATION.md) was a raw `::kill()` followed immediately by `SBDebugger::Destroy()` with no reap in between, which severed `debugserver`'s ptrace connection before the kernel collected the debuggee's exit status.

---

## Stepping Implementation

- `next` (step over):
  - Windows: `SetExecutionStatus(DEBUG_STATUS_STEP_OVER)`; `Id::isStepPending` = 1
  - macOS: `Session::stepOver` → `SBThread::StepOver(eOnlyDuringStepping)` on the selected thread
- `stepIn` (step into):
  - Windows: `SetExecutionStatus(DEBUG_STATUS_STEP_INTO)`; `Id::isStepPending` = 1
  - macOS: `Session::stepInto` → `SBThread::StepInto()`
- `stepOut` (step out):
  - Windows: `Execute("gu")` — runs until return; the stop at the resulting internal breakpoint gives `reason: "step"`, because `appendBreakpointStoppedEvent` finds no registry entry for its engine id
  - macOS: `Session::stepOut` → `SBThread::StepOutOfFrame(frame)`
- Step completion detection:
  - Windows: `Session::pollEvents` sees `WaitForEvent` return `S_OK` while `Id::isStepPending` is set, and reports `events (step)`
  - macOS: `lldb::eStopReasonTrace` and `lldb::eStopReasonPlanComplete` route to `onStepStop`, which reports `events (step)`
  - The `step` entry writes `stopReason = step` only when `stopReason` is `none`, on both platforms

## Pause Implementation

- `pause`:
  - Windows: `DebugBreakProcess(OpenProcess(pid))`; `Id::isPausePending` = 1
  - macOS: `Session::interrupt` → `SBProcess::SendAsyncInterrupt()`; `Id::isPausePending` = 1
- Pause detection:
  - Windows: the injected int3 arrives at `onBreakpoint` (`Callbacks.cpp`), which reports `events (pause)` while `Id::isPausePending` is set
  - macOS: `lldb::eStopReasonInterrupt` routes to `onInterruptStop`. A stop while `Id::isPausePending` is set also goes to `onInterruptStop`
- Emits stopped/pause event with the current event thread's system ID

## Multi-Thread Support

- `appendThreads`:
  - Windows: `IDebugSystemObjects::GetThreadIdsByIndex`, `GetThreadDescription` per thread
  - macOS: `SBProcess::GetThreadAtIndex`, `SBThread::GetName()` per thread; unnamed threads report `SBThread::GetName()`'s own fallback string
- `getEventThreadSystemId()`: resolves the reporting thread's OS thread ID on both platforms
- `setCurrentThreadBySystemId()`: selects the given OS thread as current on both platforms
- Frame ID map: `VariableManager::onStackTrace` assigns unique IDs per frame, stores (threadSystemId, frameIndex)
- `scopes`/`variables` restore thread context via `setCurrentThreadBySystemId` before inspecting

---

## File Structure

```
whatdbg/
    project-info.md                     Build manifest — cast reads this
    cast/
        spell.md                        Output rows and toolchain wiring
        cmake.cast                      CMakeLists template
        identifiers.md                  Id:: vocabulary
        files.md                        BinaryData file name of the help page (`whatdbgHelp`)
    CMakeLists.txt                      GENERATED — do not edit
    entitlements.plist                  macOS codesign entitlements
    ARCHITECTURE.md
    SPEC.md
    Source/
        Main.cpp                        Entry point: --help, sidecar extraction, re-exec trampoline (macOS), runAdapter
        HELP.md                         The --help page
        Whatdbg.h / .cpp                Main: JUCEApplicationBase, Model listener, lane drain to stdout
        WHATDBGModel.h / .cpp           The Model: SESSION and EXCEPTION rows
        Processor.h / .cpp              Engine context: lanes, commands table, command bodies, process
        generated/                      GENERATED — do not edit
            ProjectInfo.h, Generated.h / .cpp, Identifiers.h, Files.h
        debug/
            State.h                     ExecutionState, StopReason, OffsetStatus, Events, getExceptionName
            Session.h                   Debug engine wrapper interface (shared)
            Session.cpp                 [Windows] Lifecycle, wake/cancel/resetCancel, stepping, thread ops, breakpoint API, inspection
            SessionPrettyPrint.cpp      [Windows] Type formatters
            Session_mac.cpp             [macOS] Lifecycle, wake broadcaster, stepping, thread ops, breakpoint API, inspection
            SessionPrettyPrint_mac.cpp  [macOS] Type formatters
            PrettyPrint.h               Shared helpers (shouldSkipSymbol, parseHexAddress)
            Loader.h / .cpp             [Windows] Sidecar DLL loader
            Callbacks.h / .cpp          [Windows] COM callbacks -> Session::events
            BreakpointManager.h / .cpp  DAP-to-engine breakpoint registry
            VariableManager.h / .cpp    frameId and variablesReference registry
            StopManager.h / .cpp        Stops table and stop drains
        dap/
            Reader.h / .cpp             stdin thread
            Writer.h / .cpp             stdout frames
            FIFO.h                      The lane template
            Types.h / .cpp              DAP message builders, nextSeq
    tests/
        smoke/
            run_smoke.lua               Orchestrator — nvim-headless Lua driver, 21 scenarios
            scenario_runner.lua         Runs one scenario, compares its transcript with the baseline
            scenario_breakpoint.lua     Launch+breakpoint, step, variables, evaluate, breakpoint scenarios
            scenario_process.lua        Attach, pause, output, crash, disconnect-detach, order, unknown-command scenarios
            scenario_terminate.lua      Terminate-without-zombie scenario
            scenario_inspect.lua        Struct children, thread frames, evaluate failure, inner frame scenarios
            dap_client.lua              Minimal DAP client over stdio, transcript
            report.lua                  Pass/fail summary
            build.sh                    Builds the fixtures into tests/smoke/build/
            fixture.cpp                 Standard breakpoint/variables/evaluate fixture
            fixture_wait.cpp            Long-running fixture for attach/pause
            fixture_crash.cpp           Fixture that raises an unhandled exception
            fixture_module.cpp          Module that loads late (pending breakpoint)
            fixture_load.cpp            Loads fixture_module
            baseline/macos/, baseline/windows/   Transcript baselines
    benchmark/
        smoke.md, smoke.sh              scout manifest and launcher, macOS
        smoke-windows.md, smoke.cmd     scout manifest and launcher, Windows
        smoke-report-baseline.md        Timing before the rewrite
        smoke-report.md                 Timing after the rewrite
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
    build-liblldb.sh                    (pinned LLVM build — writes Builds/liblldb + Resources/macos/)
    build.sh                            Copy of cast/build.sh — escape hatch for Windows builds from the MSVC shell of the host architecture.
    carol/
        SPRINT-LOG.md                   Cross-session sprint memory
```

---

## Glossary

| Term | Definition |
|------|------------|
| DAP | Debug Adapter Protocol. JSON-RPC over stdin/stdout. |
| dbgeng | Windows Debug Engine COM API (the engine behind WinDbg). |
| liblldb | LLVM's debugger library. Exposed via the SB (Scriptable Bridge) API. |
| SB API | liblldb's public C++ interface (`lldb::SBDebugger`, `SBTarget`, `SBProcess`, etc.). |
| Sidecar | Pinned debug engine binaries embedded in BinaryData, extracted at runtime. Windows: DLLs extracted to `~/.config/whatdbg/dbgeng/` and loaded via `LoadLibrary`. macOS: `liblldb.dylib` extracted to the sidecar folder, then the process re-execs itself with `DYLD_LIBRARY_PATH` set so dyld resolves SB API symbols. |
| Re-exec trampoline | macOS startup pattern: `Main.cpp` extracts `liblldb.dylib`, sets `DYLD_LIBRARY_PATH`, then `execv`s itself. The second exec finds the dylib and proceeds normally. |
| WaitForEvent | Blocking dbgeng call (Windows) that processes debug events. |
| SSOT | Single Source of Truth. |
| Engine context | The serial context of `Processor`'s `jam::AsyncUpdater`. Every engine call runs on it. |
| Lane | A `dap::FIFO`: `requests` into the engine context, `messages` out of it. |
| FIFO | First In First Out queue. AbstractFifo + std::vector. |
| SPSC | Single Producer Single Consumer. |
| prettyPrint | Per-type value formatter for juce::String, std::string, std::unique_ptr, std::vector. |
| dispatch table | `commands`, a `jam::Function::Map<juce::String, jam::Function::pointerCapacity, void (const jam::JsonDocument&)>`, in `Processor`. `onCommand` looks up the DAP command name. |
| cast | Code generator. It reads `project-info.md` and `cast/*.md`. It writes `CMakeLists.txt`, `Source/generated/*`, `gh.cmake`, and `RELEASE.md`. It then runs the selected toolchain row. |
| toolchain row | A row in the `## toolchain` table of `project-info.md`. |
| `jam::Instance<T>` | CRTP base in jam_core. It gives `getInstance()`. `WHATDBGModel` derives from it. |
| `jam::Union` | Packed transport type in jam_core. `BreakpointLocation` uses it. |
| BreakpointLocation | `jam::Union<std::int32_t, std::uint16_t>`. It carries an engine breakpoint ID and a resolved line. |
| `jam::TerminalPage` | `juce::Component` of `jam_tui` that paints a markdown document with the terminal graphics context. The `--help` page. |

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-03-30 | COUNSELOR | Initial draft (three-thread model) |
| 0.2 | 2026-03-31 | COUNSELOR | Simplified to two-thread model, dropped JUCE message system |
| 0.3 | 2026-04-01 | MACHINIST | Session split (3 files), variable inspection, expression eval, OutputDebugString, pause, multi-thread, stepping, terminate/disconnect, symbol group cache, dispatch table, DynObj consolidation |
| 0.4.0 | 2026-04-24 | ENGINEER | Cross-platform: macOS liblldb backend (Session_mac, SessionInspection_mac, SessionPrettyPrint_mac), per-arch sidecar layout, re-exec trampoline, JAM build system (`configure_app()`), updated stack and module map |
| 0.5.0 | 2026-09-02 | COUNSELOR | Build moved to `cast` (`project-info.md`, `cast/spell.md`, `cast/cmake.cast`); `jam::Context` replaced by `jam::Instance`; `Log.h` replaced by `jam::debug::Log`; `BreakpointInfo` collapsed and `BreakpointLocation` added; Session out-parameters removed; `getOffsetByLine` split into `getOffsetStatus` and `getOffset` |
| 0.6.0 | 2026-09-03 | COUNSELOR | Corrected this document against the codebase: main loop, module map, and data-flow diagrams rewritten cross-platform; macOS zombie-termination fix documented under Terminate vs Disconnect; stepping/pause/multi-thread sections given macOS equivalents |
| 0.7.0 | 2026-11-14 | COUNSELOR | Audio processor shape: `Whatdbg` (JUCEApplicationBase) on the message loop, `WHATDBGModel`, `Processor` on the engine context, the `requests` and `messages` lanes, `VariableManager`, `StopManager`, `dap::Writer`, `dap::FIFO`, `Session::wake/cancel/resetCancel`; the State class, the hand-written loop, and the three old threading decisions replaced by one decision; the help page (`jam::TerminalPage`) |

---

**End of Architecture Document**

**JRENG!**
