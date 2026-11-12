# PLAN: Audio Architecture Discipline Part 5 — One Events Table for Both Engines

**RFC:** `RFC-message-loop-model.md`. Canon: `PLAN-audio-architecture-discipline-0-tracker.md`, ruling 6; decision 6; differences D4 and D5.
**Date:** 2026-11-12
**BLESSED Compliance:** verified
**Language Constraints:** C++17 / JUCE 8.0.17 / JAM (LANGUAGE.md, C++ / JUCE)

## Overview

Today each engine writes the state itself. Eight Windows functions and twelve macOS functions write fields of `debug::State` (Part 1, Appendix A, T2 and T3), and four drains of the loop read and clear the flags that they set.
This part gives `debug::Session` one public `events` table, in the form of `EVETerminalProcessor::events`. The native bodies report an event and write no state. One entry for each event does the work, the same on both platforms.
Three flags and their drains go away: the output, the process exit, and the breakpoint location. The entry acts at the report (tracker, decision 6).
The loop, `debug::State`, and the single thread stay in this part.

## What this part does not do

- No second context and no `jam::Model`.
- No change of the native tests that select an event.
- No change of a stop drain, except the source of the image name.

## Language / Framework Constraints

- MANIFESTO **S**: "Logic duplicated across two files." MANIFESTO **E**, On established patterns: "Find the established pattern. Extend it."
- CODING.md, Uniform signature in the type: the events have several argument lists, so the map is the several-signature form, and each `add` with arguments names its argument list, as `events.add<int>` (`eve/Source/EVETerminalProcessor.cpp:126`).
- CODING.md, Consume the event payload.
- CODING.md: no non-owning pointer member; no `friend`; no forward declaration.
- DbgEng: the callbacks run inside `WaitForEvent` on the session thread, and `WaitForEvent` "cannot be called from the event callbacks" (learn.microsoft.com, `nf-dbgeng-idebugcontrol-waitforevent`). No entry makes an engine call.

## Dependency & API Inventory

Reads of 2026-11-12. The line numbers of whatdbg are of commit `7f0d713`.

| Need | Existing piece | Where |
|---|---|---|
| The events type of a lower layer | `using Events = jam::Function::Map<juce::Identifier, jam::Function::pointerCapacity, void (int), void (int64_t), void (const char*, int)>;` | `jam/jam_terminal/document/jam_TerminalSwap.h:34` |
| The registration | `events.add<int> (id, [this] (const juce::Identifier& key, int value) { ... });` | `eve/Source/EVETerminalProcessor.cpp:123-148` |
| The several-signature map | `add<Args...> (key, function)` (`:125-129`); `get (key, args...)` selects the slot by the argument types, and "A call that matches no signature, or more than one, does not compile" (`:100-107`, `:154-158`); the callable receives `const KeyType&` first (`:68`) | `jam/jam_core/function_map/jam_Function.h` |
| A `void ()` signature in a several-signature map | `jam::Function::Map<juce::Identifier, jam::Function::pointerCapacity, void (), void (juce::ValueTree&), void (juce::File&)> rules;` | `jam/jam_plugin_bootstrap/enax/jam_ENAXClientExtensions.h:58` |
| The Windows writers | `onBreakpoint` 22-42, `onUnknownException` 49-66, `Output2` 173-193, `Breakpoint` 244-261, `CreateProcess` 300-319, `ExitProcess` 321-331, `LoadModule` 333-350; `Session::pollEvents` (`Session.cpp:216-239`) | `Callbacks.cpp`; Appendix A, T3a |
| The Windows text types | `Output2 (ULONG which, ULONG flags, ULONG64 arg, PCWSTR text)`: the text is wide, and the old line is `state->debuggeeOutputText += juce::String (text);` (`:173`, `:187`). `LoadModule (…, PCSTR imageName, …)`: `imageName != nullptr ? juce::String (imageName) : juce::String ()` (`:334`, `:348`). | `Callbacks.cpp` |
| The macOS writers | `onBreakpointStop` 31-37, `onStepStop` 39-44, `onInterruptStop` 46-51, `onSignalStop` 53-61, `onExceptionStop` 63-71, `drainProcessStdio` 76-98, `onStopReason` 100-129, `onProcessStateStopped` 131-150, `onProcessStateExited` 152-161, `onProcessStateRunning` 163-170, `onProcessEvent` 172-211, `onBreakpointEvent` 213-234; `Session::launch` 278-279 | `Session_mac.cpp` |
| The macOS parameters today | The five stop handlers and `onStopReason` take `debug::State&`. `drainProcessStdio`, the three `onProcessState…` functions, `onProcessEvent`, and `onBreakpointEvent` take `debug::State*`. The two maps are `void (debug::State&, lldb::SBThread&)` (`:103`) and `void (debug::State*, lldb::SBProcess&, lldb::SBEvent&)` (`:180`). | `Session_mac.cpp` |
| The macOS output text | `juce::String (juce::CharPointer_UTF8 (buffer), juce::CharPointer_UTF8 (buffer + bytesRead))` | `Session_mac.cpp:91-92` |
| The owners of the callback objects | `OutputCallbacks outputCallbacks;` and `EventCallbacks eventCallbacks;`, members of `Session` | `Source/debug/Session.h:543`, `:548` |
| The shared header of the debug layer | `State.h`, included by `Session.h` | `Source/debug/Session.h:3` |
| The drains that go away | `drainBreakpointLocationResolved` (`:182-193`), `drainDebuggeeOutput` (`:195-208`); the flag test of `drainProcessExited` (`:231`, `:237`) | `Whatdbg.cpp` |
| The stops atom | `debug::StopManager` and its six drains | Part 4 |

## The design

`State.h` gets one alias, after the enums:

`using Events = jam::Function::Map<juce::Identifier, jam::Function::pointerCapacity, void (), void (int), void (int, int), void (juce::int64, juce::int64, int), void (const juce::String&)>;`

`debug::Session` gets the public member `Events events;`, after the declaration of `shutdown` (`Session.h:170`).

Two owners fill the table, each with the events of its responsibility. `StopManager` adds the six stop keys in a new private `registerEvents()`, called in its constructor after the `stops` entries. `Whatdbg` adds the other eight keys in a new private `registerEvents()`, called in its constructor after the `commands` entries.

Each row is one entry. "Body" is the complete body. `state` is `State::getInstance ()` in `StopManager` and the member in `Whatdbg`.

| Key | Arguments | Owner | Body | Windows reporter | macOS reporter |
|---|---|---|---|---|---|
| `Id::initialBreak` | none | `StopManager` | `stopReason = initialBreak`; `executionState = stopped` | `onBreakpoint`, while `launching` (`Callbacks.cpp:24-26`, `:40`) | `onProcessStateStopped`, while `launching` (`Session_mac.cpp:140-143`) |
| `Id::pause` | none | `StopManager` | `isPausePending = false`; `stopReason = pause`; `executionState = stopped` | `onBreakpoint`, while `isPausePending` (`:28-31`, `:40`) | `onStopReason`, while `isPausePending` (`:114-117`); `onInterruptStop` (`:46-51`) |
| `Id::exception` | `juce::int64 code`, `juce::int64 address`, `int isMach` | `StopManager` | `stopReason = exception`; `exceptionCode = static_cast<std::uint32_t> (code)`; `exceptionAddress = static_cast<std::uint64_t> (address)`; `isMachException = isMach != 0`; `executionState = stopped` | `onBreakpoint`, the other case (`:35-37`, `:40`), and `onUnknownException`, second chance (`:59-62`); `isMach` is 0 | `onSignalStop` with 0 (`:53-61`); `onExceptionStop` with 1 (`:63-71`) |
| `Id::breakpoint` | `int engineId` | `StopManager` | `stopReason = breakpoint`; `breakpointEngineId = engineId`; `executionState = stopped` | `Breakpoint` (`:250-254`) | `onBreakpointStop` (`:31-37`) |
| `Id::step` | none | `StopManager` | `isStepPending = false`; `executionState = stopped`; `stopReason = step` when `stopReason` is `none` | `Session::pollEvents`, on `S_OK` while `isStepPending` (`Session.cpp:227-233`) | `onStepStop` (`:39-44`) |
| `Id::moduleLoad` | `const juce::String& imageName` | `StopManager` | `stopReason = moduleLoad`; `lastLoadedImageName = imageName` | `LoadModule`, when no step is pending (`:344-348`) | none |
| `Id::targetProcessId` | `int processId` | `Whatdbg` | `state.targetProcessId = static_cast<std::uint32_t> (processId)` | `CreateProcess` (`:308-310`) | `Session::launch` (`:278-279`) |
| `Id::exited` | `int exitCode` | `Whatdbg` | `state.processExitCode = exitCode`; `drainProcessExited ()` | `ExitProcess` (`:327-329`) | `onProcessStateExited` (`:158-160`) |
| `Id::terminated` | none | `Whatdbg` | `drainProcessExited ()` | `Session::pollEvents`, on `E_UNEXPECTED` (`Session.cpp:222-225`) | none |
| `Id::running` | none | `Whatdbg` | `state.executionState = running` | none | `onProcessStateRunning` (`:163-170`) |
| `Id::breakpointLocationFound` | `int engineId`, `int line` | `Whatdbg` | `addMessages (breakpointManager.onBreakpointLocationFound (engineId, static_cast<std::uint16_t> (line)))` | none | `onBreakpointEvent` (`:225-231`) |
| `Id::console`, `Id::standardOutput`, `Id::standardError` | `const juce::String& text` | `Whatdbg` | `addMessage (dap::getOutputEvent (key.toString(), text))` | `Output2` reports `Id::console` (`:184-188`) | `drainProcessStdio` reports `Id::standardError` or `Id::standardOutput` (`:85-94`) |

The forms:
- An entry with arguments: `events.add<int> (Id::breakpoint, [this] (const juce::Identifier&, int engineId) { … });`.
- An entry with no argument: `events.add (Id::step, [this] (const juce::Identifier&) { … });`. The argument pack is empty (`jam_Function.h:125-129`).
- The three output keys share one loop over `{ Id::console, Id::standardOutput, Id::standardError }`, in the form of `EVETerminalProcessor.cpp:125-126`. The lambda takes `const juce::Identifier& key`.
- A report: `events.get (Id::breakpoint, static_cast<int> (engineId));`. Each argument has the exact type of the entry.

What changes around the entries:

| Old | New |
|---|---|
| `drainProcessExited`: `if (state.hasProcessExited)` and `state.hasProcessExited = false;` (`Whatdbg.cpp:231`, `:237`) | `if (state.executionState != debug::ExecutionState::exited)`. The clear line is removed. The other lines stay. So two exit reports in one wait give one `exited` and one `terminated`, as the one flag does today. |
| `processDeferredEvents`: five calls | `addMessages (stopManager.onStop (state.targetProgram));` and `drainTerminateTimeout ();` |
| `StopManager::drainModuleLoaded`: `State::getInstance ()->lastLoadedImageName` | the member `lastLoadedImageName`, a new private `juce::String` of `StopManager` |
| `debug::State` | loses `hasDebuggeeOutput`, `debuggeeOutputText`, `debuggeeOutputCategory`, `hasProcessExited`, `hasBreakpointLocationsResolved`, `resolvedBreakpointEngineId`, `resolvedBreakpointLine`, `lastLoadedImageName` |

What stays in the native bodies: the tests that select the entry. Windows: `executionState == launching` (`Callbacks.cpp:24`), `isPausePending` (`:28`), `isStepPending` (`:344`; `Session.cpp:227`), `firstChance` (`:55`), and the return value of each callback. macOS: `GetRestartedFromEvent` (`Session_mac.cpp:136`), `executionState == launching` (`:140`), `isPausePending` (`:114`), and the two dispatch maps (`:102-112`, `:179-189`). They read `debug::State` and write nothing.

How the reporters reach the table:
- Windows: `OutputCallbacks` and `EventCallbacks` each get the constructor `explicit …Callbacks (Events& newEvents)` and hold `Events& events;` as a reference member, as `ConfigModel` holds `ENDModel& appModel` (`end/Source/config/ConfigModel.h:118`). `Session` builds them with `{ events }` (`Session.h:543`, `:548`). The three file-static functions `onBreakpoint`, `onThreadName`, and `onUnknownException` (`Callbacks.cpp:22`, `:44`, `:49`) get `debug::Events& events` after their `debug::State*` parameter. The map of `EventCallbacks::Exception` (`Callbacks.cpp:272`) becomes `HRESULT (debug::State*, debug::Events&, PEXCEPTION_RECORD64, ULONG)`, and the two calls of `:284` and `:287` give the member `events` after `State::getInstance ()`.
- macOS: `Session::pollEvents` gives its member `events` down the call chain.
  - In the five stop handlers, in `drainProcessStdio`, and in `onBreakpointEvent`, the `debug::State` parameter becomes `debug::Events& events`. They read no state.
  - `onStopReason`, `onProcessStateStopped`, `onProcessStateExited`, `onProcessStateRunning`, and `onProcessEvent` keep their state parameter and get `debug::Events& events` after it.
  - The map of `:103` becomes `void (debug::Events&, lldb::SBThread&)`. The map of `:180` becomes `void (debug::State*, debug::Events&, lldb::SBProcess&, lldb::SBEvent&)`.

The argument of each report is the right side of the old assignment, with these casts:
- An exception code: `static_cast<juce::int64> (static_cast<std::uint32_t> (…))`. An exception address: `static_cast<juce::int64> (…)`. `isMach`: the literal `0` or `1` of the table.
- A process id, an exit code, an engine id, a line: `static_cast<int> (…)`.
- A text: the `juce::String` expression of the old line (`Callbacks.cpp:187`, `:348`; `Session_mac.cpp:91-92`).

## Validation Gate

Each step is validated by COUNSELOR before proceeding to the next — against
MANIFESTO.md (BLESSED), NAMES.md, ~/.carol/CODING.md, and the locked PLAN decisions
(no deviation, no scope drift). @Auditor runs ONCE, after the final step, covering
the whole sprint — never per step.

## Steps

### Step 1: Delete
**Scope:** `Source/debug/Callbacks.cpp`; `Source/debug/Session.cpp`; `Source/debug/Session_mac.cpp`; `Source/debug/State.h`; `Source/Whatdbg.h` and `.cpp`.
**Action:**
1. Remove each write to a `debug::State` field from the three native files, at the lines of Part 1, Appendix A, T2, columns W and M. Keep each read.
2. Delete the eight fields of the table from `State.h`.
3. Delete `drainBreakpointLocationResolved` and `drainDebuggeeOutput` from `Whatdbg`.

**Validation:** The build fails at each use of a deleted field. The native writes are statements, so the compiler does not list them: the table of section The design is the list of what Step 4 puts back.

### Step 2: The table
**Scope:** `Source/debug/State.h`; `Source/debug/Session.h`; `Source/debug/Callbacks.h` and `.cpp`.
**Action:** Add the alias `Events` and the member `Session::events`. Give `OutputCallbacks` and `EventCallbacks` the constructor and the reference member.
**Validation:** `Events` holds five signatures. `Session::events` is declared before `outputCallbacks` and `eventCallbacks`.

### Step 3: The entries
**Scope:** `Source/debug/StopManager.h` and `.cpp`; `Source/Whatdbg.h` and `.cpp`.
**Action:** Write the two `registerEvents` functions with the fourteen entries of the table. Apply the table "What changes around the entries".
**Validation:** Each key has one `add`. No entry calls a member of `session`. Each `add` with arguments names its argument list. `Whatdbg.cpp` holds no `hasProcessExited`.

### Step 4: The reports
**Scope:** `Source/debug/Callbacks.cpp`; `Source/debug/Session.cpp`; `Source/debug/Session_mac.cpp`.
**Action:** At each site of Step 1 item 1, call `events.get (key, arguments...)` with the key and the arguments of the table. Change the parameters and the two maps as section The design gives them.
**Validation:** Each reporter cell of the table has one call. An LSP `findReferences` on each field of `State.h` returns no write in the three files. No native function gained a branch. The return value of each Windows callback is unchanged (Appendix A, T3a).

### Step 5: Build and check, macOS
**Scope:** build of whatdbg; the smoke suite.
**Action:** ARCHITECT builds. Run the smoke suite of Part 1 two times.
**Validation:** The 21 sequence checks pass in both runs. Scenario `07_output` passes its two text checks. Scenarios `03_step` and `11_step_in_out` pass: they prove the `Id::step` entry on macOS (difference D5). Scenario `21_pending_breakpoint` passes: it proves the `Id::breakpointLocationFound` entry.

### Step 6: Build and check, Windows
**Scope:** a Windows machine.
**Owner:** ARCHITECT builds, runs, and debugs on the Windows machine. This step blocks no macOS step.
**Action:** Build, and run the suite against the Windows baseline.
**Validation:** The sequence checks pass.

## BLESSED Alignment

- **B — Bound.** `Session` owns the table. `StopManager` and `Whatdbg` fill it one time, at construction.
- **L — Lean.** Fourteen entries replace the write sites of eight Windows functions and twelve macOS functions. Two drains and eight fields go away.
- **E — Explicit.** An engine event has a name and typed arguments.
- **S — SSOT.** One entry is the one rule for an engine event, on both platforms.
- **S — Stateless.** The native bodies hold no state and write none. Three deferred flags are gone.
- **E — Encapsulation.** The debug layer does not include `Whatdbg`. Each owner registers the events of its responsibility.
- **D — Deterministic.** One rule for one event on both platforms.
- LANGUAGE.md: no override applies beyond the C++ / JUCE Lean adaptation.

## Risks / Open Questions

None open.

Facts for the review of the whole PLAN:

- **The order of the messages of one pass stays.** A Windows callback that reports a stop returns `DEBUG_STATUS_BREAK`, which ends the wait (Appendix A, T3a). macOS handles one event for each `pollEvents` call (`Session_mac.cpp:320-333`). So an exit or a location report does not share a pass with a stop drain, except a pending `initialBreak`, whose drain writes nothing before `configurationDone`.
- **Output is the one order that changes** (difference D4). An `output` event now comes before the stop event of its pass. The transcript holds no `output` line (Part 1 Step 2).
- **`Id::step` on macOS clears `isStepPending`.** No macOS code reads `isStepPending` (Appendix A, T2).
- **`Id::pause` clears `isPausePending` for `eStopReasonInterrupt` too.** The old `onInterruptStop` did not clear it when the reason came with no pending pause. The flag is then already false.
- **The exit entry writes stdout inside a Windows callback** in this part. Writing stdout is no engine call. In Part 6 the entry writes the lane.
