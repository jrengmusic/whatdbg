# PLAN: Audio Architecture Discipline Part 4 — The Atoms, Under the Old Loop

**RFC:** `RFC-message-loop-model.md`. Canon: `PLAN-audio-architecture-discipline-0-tracker.md`, rulings 11 and 13; decision 1.
**Date:** 2026-11-12
**BLESSED Compliance:** verified
**Language Constraints:** C++17 / JUCE 8.0.17 / JAM (LANGUAGE.md, C++ / JUCE)

## Overview

`Whatdbg.cpp` is 786 lines and holds six responsibilities: the loop, the command bodies, the stop drains, the event builders, the variable and frame registry, and the stdout writer.
This part gives four of them their own owner, each with one responsibility (ruling 11): `dap::Writer`, `debug::VariableManager`, `debug::StopManager`, and the builders of `dap::Types`.
The hand-written loop, `debug::State`, and the single thread stay. So each move is proven by the smoke sequences alone, with no change of a thread (ruling 13).

## What this part does not do

- No second context, no `jam::Model`, no events table.
- No change of an engine call or of a DAP message.
- No change of the native code.

## Language / Framework Constraints

- Ruling 11: "the atom of each individual object that only responsible for ONE responsibility".
- MANIFESTO **E**, On established patterns. The pattern of an atom of the debug layer is `debug::BreakpointManager`: it holds `Session& session` (`BreakpointManager.h:208`), it owns its registry, and its `on*` members do the work and return the DAP messages as `juce::Array<juce::var>` (`BreakpointManager.h:80-125`). The owner writes the messages.
- CODING.md: no out-parameter. An atom returns its messages.
- CODING.md, Uniform signature in the type: the `stops` table declares its one signature.
- CODING.md: one object, one translation unit.

## Dependency & API Inventory

Reads of 2026-11-12. Lines are of `Source/Whatdbg.cpp` at commit `7f0d713`; Part 3 changed the literals to `Id::` names.

| Need | Existing piece | Where |
|---|---|---|
| The stdout writer | `writeMessage` | `Whatdbg.cpp:311-322` |
| The stdout mode on Windows | `_setmode (_fileno (stdout), _O_BINARY);` and its two includes | `Whatdbg.cpp:55-58`, `:12-15` |
| The stdin twin | `dap::Reader::start` sets stdin to binary | `Reader.cpp:32-35` |
| The message array writer | `writeMessages` | `Whatdbg.cpp:166-172` |
| The registry fields | `nextVariablesRef` (`:283`), `variablesRefMap` (`:291`), `nextFrameId` (`:299`), `frameIdMap` (`:306`), `lastScopesThreadId` (`:313`) | `Source/debug/State.h` |
| The registry bodies | `resetVariablesState` (`:324-332`), `selectFrameFromId` (`:513-534`), `getDapVariables` (`:564-584`), `addDapVariable` (`:601-624`); the registry lines of `onStackTrace` (`:492-505`), `onScopes` (`:541-549`), `onVariables` (`:591-594`); `maxStackFrames` (`:19`) | `Whatdbg.cpp` |
| The stops table | six entries; the type `jam::Function::Map<debug::StopReason, jam::Function::pointerCapacity, void ()>`; the lookup | `Whatdbg.cpp:45-50`, `:268-274`; `Whatdbg.h:82` |
| The stop drains | `drainInitialBreak` (`:120-127`), `drainBreakpointHit` (`:129-135`), `drainStepCompleted` (`:137-147`), `drainPauseCompleted` (`:149-158`), `drainModuleLoaded` (`:174-180`), `drainExceptionStopped` (`:210-226`), `resumeAfterInitialBreak` (`:282-301`), `resumeExecution` (`:160-164`) | `Whatdbg.cpp` |
| The drains that are no stop | `drainBreakpointLocationResolved` (`:182-193`), `drainDebuggeeOutput` (`:195-208`), `drainProcessExited` (`:228-249`), `drainTerminateTimeout` (`:251-264`) | `Whatdbg.cpp` |
| The event builders | `getStoppedEvent` (`:733-741`), `getExceptionDescription` (`:743-749`), `getExceptionStoppedEvent` (`:751-763`), `getExceptionOutputEvent` (`:765-773`), `getProcessEvent` (`:775-786`); the `output` event of `:201-205` | `Whatdbg.cpp` |
| The member order of `Whatdbg` | `commands` (`:75`), `stops` (`:82`), `state` (`:370`), `session` (`:375`), `breakpointManager` (`:378`), `reader` (`:381`) | `Whatdbg.h` |
| The constructor form of an atom | `BreakpointManager::BreakpointManager (Session& newSession) : session { newSession }` | `BreakpointManager.cpp:18-21` |
| The state access of the debug layer | `State::getInstance ()` | `Session.cpp:220`; `Session_mac.cpp:324` |

## The design

### `dap::Writer`

`class Writer` in `namespace dap`, in `Source/dap/Writer.h` and `.cpp`. It is the sibling of `dap::Reader`.
- `Writer ();` On Windows its body is the `_setmode` statement of `Whatdbg.cpp:57`, inside `#if JUCE_WINDOWS`. The two includes of `Whatdbg.cpp:12-15` move to `Writer.cpp`.
- `void writeMessage (const juce::var& message) noexcept;` Its body is `Whatdbg.cpp:313-321`, unchanged.

### The builders of `dap::Types`

Each one is a free function of `namespace dap`, declared in `Types.h` and defined in `Types.cpp`.

| Declaration | Body |
|---|---|
| `juce::var getStoppedEvent (const juce::String& reason, int threadId);` | `Whatdbg.cpp:735-740`. `static_cast<int> (session.getEventThreadSystemId ())` becomes `threadId`. |
| `juce::String getExceptionDescription (juce::int64 exceptionCode, juce::int64 exceptionAddress);` | `Whatdbg.cpp:745-748`. The two `static_cast<juce::int64> (state.…)` expressions become the two parameters. |
| `juce::var getExceptionStoppedEvent (const juce::String& exceptionName, const juce::String& description, int threadId);` | `Whatdbg.cpp:755-762`, unchanged |
| `juce::var getExceptionOutputEvent (const juce::String& exceptionName, const juce::String& description);` | `Whatdbg.cpp:768-772`, unchanged |
| `juce::var getProcessEvent (const juce::String& name, std::uint32_t processId, const juce::String& startMethod);` | `Whatdbg.cpp:779-785`, unchanged |
| `juce::var getOutputEvent (const juce::String& category, const juce::String& output);` | `Whatdbg.cpp:201-205`. `state.debuggeeOutputCategory` and `state.debuggeeOutputText` become the two parameters. It returns the event. |

### `debug::VariableManager`

`class VariableManager` in `namespace debug`, in `Source/debug/VariableManager.h` and `.cpp`. It is the registry of the DAP variable references and frame ids.

Public:
- `explicit VariableManager (Session& newSession);`
- `void resetVariablesState () noexcept;` — `Whatdbg.cpp:326-331`, with `state.` removed from each field.
- `juce::Array<juce::var> onStackTrace (int threadId);` — `Whatdbg.cpp:492-505`, with `state.` removed. It returns `frames`.
- `int onScopes (int frameId);` — `Whatdbg.cpp:541-549`, with `state.` removed. It returns `localsRef`.
- `juce::Array<juce::var> onVariables (int ref);` — `Whatdbg.cpp:591-594`, with `state.` removed, then `return getDapVariables (ref);`.
- `std::pair<std::uint32_t, int> selectFrameFromId (int frameId);` — `Whatdbg.cpp:513-534`, with `state.` removed.

Private:
- `juce::Array<juce::var> getDapVariables (int ref);` and `void addDapVariable (juce::Array<juce::var>& dapVariables, const juce::var& rawVar, int frameIndex);` — `Whatdbg.cpp:564-584` and `:601-624`, with `state.` removed.
- `Session& session;`
- The five fields of `State.h:283-313`, with their types and defaults.
- `maxStackFrames` moves to `VariableManager.cpp` as it is (`Whatdbg.cpp:19`).

### `debug::StopManager`

`class StopManager` in `namespace debug`, in `Source/debug/StopManager.h` and `.cpp`. It applies the stop that the engine reported, and it gives the DAP messages of that stop.

Public:
- `StopManager (Session& newSession, BreakpointManager& newBreakpointManager, VariableManager& newVariableManager);` It fills `stops` with the six entries of `Whatdbg.cpp:45-50`. Each entry is `[this] (const debug::StopReason&, const juce::String& targetProgram) { return drain… (…); }`. Only `drainInitialBreak` takes `targetProgram`.
- `juce::Array<juce::var> onStop (const juce::String& targetProgram);` — the lookup of `Whatdbg.cpp:268-274`. It reads `State::getInstance ()->stopReason`. When the table holds the reason, it returns the result of the entry. When it does not, it returns an empty array.

Private:
- `jam::Function::Map<debug::StopReason, jam::Function::pointerCapacity, juce::Array<juce::var> (const juce::String&)> stops;`
- `Session& session;`, `BreakpointManager& breakpointManager;`, `VariableManager& variableManager;`
- The six drains and `resumeAfterInitialBreak`, each with the return type `juce::Array<juce::var>`. `drainInitialBreak` and `resumeAfterInitialBreak` take `const juce::String& targetProgram`. `void resumeExecution ();` is `Whatdbg.cpp:162-163`.

Each drain body is its text of today, with these changes only:

| Old | New |
|---|---|
| the first line of the body | `juce::Array<juce::var> messages;` |
| `state.field` | `State::getInstance ()->field` |
| `state.targetProgram` | `targetProgram` |
| `writeMessage (x);` | `messages.add (x);` |
| `writeMessages (x);` | `messages.addArray (x);` |
| `resetVariablesState ();` | `variableManager.resetVariablesState ();` |
| `getStoppedEvent (reason)` | `dap::getStoppedEvent (reason, static_cast<int> (session.getEventThreadSystemId ()))` |
| `getExceptionDescription ()` | `dap::getExceptionDescription (static_cast<juce::int64> (State::getInstance ()->exceptionCode), static_cast<juce::int64> (State::getInstance ()->exceptionAddress))` |
| `getExceptionStoppedEvent`, `getExceptionOutputEvent`, `getProcessEvent` | the same call with `dap::` before it |
| the last line of the body | `return messages;` |

`drainInitialBreak` returns the result of `resumeAfterInitialBreak (targetProgram)` inside its test, and an empty array after it.

### `Whatdbg` after this part

- Members, in this order: `commands`, `state`, `session`, `breakpointManager`, `variableManager { session }`, `stopManager { session, breakpointManager, variableManager }`, `writer`, `reader`.
- New private functions: `void addMessage (juce::var message);`, whose one statement is `writer.writeMessage (message);`, and `void addMessages (const juce::Array<juce::var>& messages);`, which calls `addMessage` for each element (the body of `Whatdbg.cpp:168-171`).
- Each `writeMessage (` call becomes `addMessage (`. Each `writeMessages (` call becomes `addMessages (`.
- `processDeferredEvents`: the lookup of `:268-274` becomes `addMessages (stopManager.onStop (state.targetProgram));`. The four calls of `:276-279` stay.
- `onStackTrace`: lines `:492-505` become `juce::Array<juce::var> frames (variableManager.onStackTrace (threadId));`.
- `onScopes`: lines `:541-549` become `const int localsRef { variableManager.onScopes (frameId) };`.
- `onVariables`: lines `:591-597` become the body creation of `:596` and `body->setProperty (Id::variables, juce::var (variableManager.onVariables (ref)));`.
- `onEvaluate`: `selectFrameFromId (frameId)` becomes `variableManager.selectFrameFromId (frameId)`.
- `onContinue`: `resumeExecution ();` becomes the two statements of `:162-163`.
- `onExceptionInfo` and `drainDebuggeeOutput` call the `dap::` builders.
- `initialize` keeps its return statement only (`:60`).

## Validation Gate

Each step is validated by COUNSELOR before proceeding to the next — against
MANIFESTO.md (BLESSED), NAMES.md, ~/.carol/CODING.md, and the locked PLAN decisions
(no deviation, no scope drift). @Auditor runs ONCE, after the final step, covering
the whole sprint — never per step.

## Steps

### Step 1: Delete
**Scope:** `Source/Whatdbg.h` and `.cpp`; `Source/debug/State.h`.
**Action:**
1. Delete from `Whatdbg`: `writeMessage`, `writeMessages`, the `stops` member and its six `add` lines, the six stop drains, `resumeAfterInitialBreak`, `resumeExecution`, `resetVariablesState`, `selectFrameFromId`, `getDapVariables`, `addDapVariable`, the five event builders, and `maxStackFrames`.
2. Delete from `State.h` the five registry fields (`:283-313`).

**Validation:** The build fails. The compiler errors are the list of what Steps 2 to 6 change. No error is silenced with a stub.

### Step 2: `dap::Writer`
**Scope:** new `Source/dap/Writer.h` and `.cpp`; `Source/Whatdbg.h` and `.cpp`.
**Action:** Write the class as section `dap::Writer` gives it. Add the member `writer` and the two functions `addMessage` and `addMessages` to `Whatdbg`. Change each call site.
**Validation:** `Whatdbg.cpp` holds no `std::cout` and no `_setmode`. `Writer.cpp` holds the one stdout write of the program.

### Step 3: The builders
**Scope:** `Source/dap/Types.h` and `.cpp`.
**Action:** Add the six builders as the table gives them.
**Validation:** No builder reads `debug::State` or `debug::Session`. `Types.h` holds declarations only.

### Step 4: `debug::VariableManager`
**Scope:** new `Source/debug/VariableManager.h` and `.cpp`.
**Action:** Write the class as section `debug::VariableManager` gives it.
**Validation:** A diff of each moved body against its old text shows only `state.` removed. An LSP `findReferences` on `variablesRefMap` and on `frameIdMap` returns positions in `VariableManager` only.

### Step 5: `debug::StopManager`
**Scope:** new `Source/debug/StopManager.h` and `.cpp`.
**Action:** Write the class as section `debug::StopManager` gives it.
**Validation:** A diff of each drain against its old text shows only the changes of the table. No drain writes stdout. The `stops` table declares its signature, and no `add` names a template argument.

### Step 6: `Whatdbg`
**Scope:** `Source/Whatdbg.h` and `.cpp`.
**Action:** Change `Whatdbg` as section `Whatdbg` after this part gives it.
**Validation:** `Whatdbg.cpp` holds the loop, `onCommand`, the eighteen command bodies, `addSearchPaths`, `getEndModeForExit`, the four drains that are no stop, `processDeferredEvents`, `addMessage`, and `addMessages`, and no other function. Each atom has one caller, `Whatdbg`.

### Step 7: Build and check, macOS
**Scope:** build of whatdbg; the smoke suite.
**Action:** ARCHITECT builds. Run the smoke suite of Part 1 two times.
**Validation:** The 21 sequence checks pass in both runs.

### Step 8: Build and check, Windows
**Scope:** a Windows machine.
**Owner:** ARCHITECT builds, runs, and debugs on the Windows machine. This step blocks no macOS step.
**Action:** Build, and run the suite against the Windows baseline.
**Validation:** The sequence checks pass.

## BLESSED Alignment

- **B — Bound.** `Whatdbg` owns each atom. An atom holds references to the atoms that it tells, as `BreakpointManager` holds `Session&`. The member order of `Whatdbg` builds each atom after the atoms that it names.
- **L — Lean.** Six responsibilities in one file become one responsibility for each object.
- **E — Explicit.** A builder takes each value as a parameter. A drain returns its messages.
- **S — SSOT.** The registry and its counters have one owner. The stdout write has one owner.
- **S — Stateless.** `StopManager` holds no state of its own in this part: the stop is in `debug::State`. `VariableManager` holds the registry, which is its responsibility.
- **E — Encapsulation.** Ruling 11. `Whatdbg` tells an atom and writes what it returns. No atom knows `Whatdbg`.
- **D — Deterministic.** The message order is the order of the returned arrays, which is the order of the old write calls.
- LANGUAGE.md: no override applies beyond the C++ / JUCE Lean adaptation.

## Risks / Open Questions

None open.

Facts for the review of the whole PLAN:

- **The order inside a drain is kept by the array.** Each drain wrote its messages in sequence with no other write between them (`Whatdbg.cpp:120-226`). The owner writes the returned array in that sequence.
- **`resumeExecution` has two users**: `resumeAfterInitialBreak` (`:291`) and `onContinue` (`:630`). After this part the first is in `StopManager`, and `onContinue` holds the two statements.
- **`debug::State` is still the state** in this part. Part 6 changes each `State::getInstance ()` access of the atoms to the Model.
