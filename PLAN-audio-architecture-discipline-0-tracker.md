# PLAN: whatdbg on the Audio Architecture Discipline — Tracker and Canon

**RFC:** `RFC-message-loop-model.md`
**Date:** 2026-11-12
**BLESSED Compliance:** verified
**Language Constraints:** C++17 / JUCE 8.0.17 / JAM (LANGUAGE.md, C++ / JUCE)

## Overview

This file is the index, the status tracker, and the canon of the rewrite.
The rewrite gives whatdbg the shape and the contract of an audio processor. `Whatdbg` is main and owns the hot path. `Processor` owns the atoms on one serial engine context. The state is a `jam::Model` on the JUCE message thread, with registered callbacks. This is the form that EVE uses.
The current whatdbg is the parity target. The numbered files together are one PLAN, in parts.

## Status

| Part | Plan | Subject | Status | Depends on |
|---|---|---|---|---|
| 0 | this file | Tracker, canon, inventory, decisions | Written. Not locked. | |
| 1 | `PLAN-audio-architecture-discipline-1-parity.md` | The parity baseline: the behavior tables, the smoke suite extended to them, the baseline sequences and timings | Written. Not locked. | 0 |
| 2 | `PLAN-audio-architecture-discipline-2-help.md` | The JAM module `jam_tui` and `jam::TerminalPage`. cast, ggwp, scout, and whatdbg print their help through it. The module rows of whatdbg. | Written. Not locked. | 1 |
| 3 | `PLAN-audio-architecture-discipline-3-foundation.md` | The lexicon (`cast/identifiers.md`), `dap::Types` as one object with one `.cpp`, and `dap::FIFO`, the one lane type | Written. Not locked. | 2 |
| 4 | `PLAN-audio-architecture-discipline-4-atoms.md` | The atoms, under the old loop: `dap::Writer`, `debug::VariableManager`, `debug::StopManager`, and the event builders in `dap::Types` | Written. Not locked. | 3 |
| 5 | `PLAN-audio-architecture-discipline-5-events.md` | One `events` table for the engine events of both platforms. The native bodies report. The entries act. | Written. Not locked. | 4 |
| 6 | `PLAN-audio-architecture-discipline-6-core.md` | `WHATDBGModel`, `Processor` on the engine context, the message loop, `Whatdbg` as main. `debug::State` and the hand-written loop are deleted. | Written. Not locked. | 5 |
| 7 | `PLAN-audio-architecture-discipline-7-close.md` | The parity proof, the timings against the baseline, and the documents | Written. Not locked. | 6 |

Each numbered part is one sprint. On macOS, each part ends with a build and a full run of the smoke suite, and no part leaves the project in a state that does not build. On Windows, ARCHITECT runs the Windows step of each part, in part order. The Windows step of a part needs the Windows baseline of Part 1 Step 6.

## The contract of every part

ARCHITECT, verbatim:

- *"write comprehensive executable, trackable multi part PLAN to rewrite whatdbg as isomorphic to jam audio architecture discipline with lock free hot path and message thread fully event driven state machine."*
- *"we have all the example of working pattern. whatdbg itself is one of the example of working pattern that actually work, and current state is the target parity working feature. so this is not green field. we know what works, we what architecture better, we only make what works even better with cleaner architecture."*
- *"the accepted criteria is identical working state, preferable FASTER and more DETERMINISTIC."*
- *"after dogfooding and using it to build ALL jam project, we just found out that whatdbg still carry defects, sometimes wait took forever, breakpoint could take couple of seconds to break, sometimes it break to anywhere, or breakpoint does not show anything meaningful, so the iteration is slow, we just committed couple of those issue on latest commit on windows, mac has not yet tested. we should just write better architecture."*
- *"i think current whatdbg already developed more than smoke test covered? we have very reliable debugger now"*
- *"audio arguabaly had unforgivable constraints, hence by design always more performant and determinstic if we seamlessly adhere to its contract."*
- *"i'm aware we rely some implementation on library we dont have full control of, like windows with dgbeng or mac with lldb. but still this is isomoprhic how one SSOT code with juce, with jam, work with each plugin sdk no? we dont fight them, we integrate seamlessly, we make it deterministic"*

Rules that follow, for each part:

1. **Parity first.** A part changes where the code runs, who owns it, and how it is told. It does not change what the code does to the engine. Each engine call, each DAP message, and each message order of Part 1 stays, except the differences that section Deliberate differences lists.
2. **Move, then tell.** A body moves to the atom that owns its responsibility. A step that changes a signature states the old text and the new text. The proof of each move is the smoke sequences of Part 1 (ruling 13).
3. **Delete first** (CAROL.md Refactor-Rewrite Discipline). The old owner of a responsibility is removed in the first step of the part that replaces it.
4. **Each file:line comes from a Read.** The doxygen XML of whatdbg is stale: `docs/xml/` holds `_breakpoint_manager_handlers_8cpp.xml` and `_whatdbg_handlers_8cpp.xml`, and those sources do not exist. The LSP index is stale also. Part 1 Step 1 regenerates both.
5. **Windows.** Each part writes both platform bodies from the cited facts. ARCHITECT builds, runs, and debugs on the Windows machine.

## Language / Framework Constraints

- LANGUAGE.md, C++ / JUCE: one object is one header and one `.cpp`. The 300-line threshold is a smell detector for the count of responsibilities.
- CODING.md: no anonymous namespace, no `namespace detail`, no out-parameter, no fake-carrier struct, no local struct, no forward declaration, no `friend`, no `==` on strings, no non-owning pointer member.
- CODING.md, placement: "Non-template free function | The object's one `.cpp`".
- CODING.md, Uniform signature in the type: a map whose entries have one signature declares it.
- CODING.md, Consume the event payload: a listener reacts to the tree and the property that the event gives.
- MANIFESTO **B**: "Threads are bound to their context". MANIFESTO anti-pattern: "Cross-thread direct call".
- MANIFESTO **D**: "Pessimistic and defensive programming are the same violation from opposite sides."
- MANIFESTO, Deadline threads: applies to "A thread with a hard deadline". The engine has no hard deadline: the loop polls it with a timeout and sleeps when idle (`Whatdbg.cpp:76-84`). The engine context can allocate.
- CAROL.md Code Hygiene: no comment and no doxygen in a sprint. The doxygen pass is Pathfinder's, after the audit.
- CAROL.md Build: ARCHITECT builds, or COUNSELOR on ARCHITECT's explicit request.
- CAROL.md Profiling: "Measurement of code uses `scout` — never a hand-written harness."

## Canon: rulings, verbatim

### From `RFC-message-loop-model.md`

1. On the thread owner of the engine (RFC:134): *"crossplatform. lldb, dbgeng should be at its own hot path lock free path this is isomporhic to audio thread, which how we design eve, end with audio architecture discipline, the traffic orchestrator state machine with jam::Model is at message thread"*.
2. On event latency (RFC:136), ARCHITECT selected *"Send at once"*: DAP `stopped` and `output` events do not wait for a flush tick.
3. On a blocked engine call (RFC:138), ARCHITECT selected *"Yes, interrupt"*: a DAP `disconnect` or a client timeout calls `SetInterrupt`.
4. On the evaluate hang (RFC:142), ARCHITECT selected *"Fix now"*: it was fixed in the old structure (commit `7f0d713`).
5. On the request path (RFC:15): *"we could still design hot path lock free if we need hard deadline no, other thread call could either synchronous or async with scoped lock"*.

### Session 2026-11-12

6. On the model: *"lock-free hot path (audio thread); ValueTree (Model) on message thread; registrable callbacks; fully event driven, no stray variables, no manual flag checking"*.
7. On the method: *"DONT ASSUME, FUCK YOUR INFERENCE. read the actual modules, how they were designed, how they were being used at working project"*.
8. On the evidence: *"what i am saying is not opinion, fact with measurements."* The measurement is `eve/comparison.md:18-27`.
9. On the old record: the decisions "Two-Thread Model (not three, not event-driven)" and "No JUCE Message Thread" (`ARCHITECTURE.md:317-339`) are replaced by this PLAN. Section Decisions, decision 9, answers each failure cause that the record names.
10. On the shape: *"would it be better if whatdbg main `orchestrator` designed like AudioProcessor? with prepare, process, parameterChanged, etc? isnt the contract identical?"* Then: *"exactly. the shape, the principle, the contract, not derived. those layers are ENCAPSULATION."*
11. On the layers: *"EVEProcessor is `main`, owner of all hot path"* and *"EVETerminalProcessor own the unit, the atom of each individual object that only responsible for ONE responsibility"*.
12. On the help page: *"fold into plan help page extraction with HELP.md from cast, ggwp, scout, as SSOT module at jam_terminal"*; *"we need to start building tui modules, jam_tui/"*; *"help page would be first one, whatdbg help should use that"*. On its type: *"jam::TerminalPage, reminder as juce::Component the native juce contract API must holds, no poking terminal at all. this is just juce::Component that uses Terminal graphics as its LLGC"*.
13. On the depth of the decomposition, ARCHITECT selected *"Full atoms now"*: the registry atom, the stops atom, and the event builders in `dap::Types`. The parity proof of the moved bodies is the smoke sequences.
14. On the audit: *"fix all. if you are still unsure, research, deep dive, synthesize solution form facts, leave no room for intrepretation."*

## RFC map

`RFC-message-loop-model.md` has ten open questions (RFC:134-143). Each one has its place here.

| RFC question | Place in this PLAN |
|---|---|
| 1. Thread owner of the engine | Ruling 1. Part 6, `Processor`. |
| 2. State in the Model | Decision 1. |
| 3. Event latency | Ruling 2. Decision 3. |
| 4. Synchronous or async for each request | Each request is async: the reader writes the `requests` lane, and the engine context applies it (Part 6, `addRequest`). Basis: ruling 5, and EVE `addInput` (`EVETerminalProcessor.cpp:265-283`). No request is synchronous. |
| 5. Cancel of a blocked engine call | Ruling 3. Difference D6. Part 6, `Session::cancel`. |
| 6. Calls during `WaitForEvent` | The PLAN makes no engine call from a second thread while the wait runs, except `SetInterrupt`, which "can be called at any time and from any thread" (SetInterrupt page). The runtime test of RFC:139 has no subject. |
| 7. macOS threading | Ruling 1. Part 6. |
| 8. New names | Section Names. The lock is the approval. |
| 9. The evaluate defect | Ruling 4. Done. |
| 10. "hot path lock free" on macOS | Decision 3. |

## The design, in one table

EVE is the working example. Each row gives the EVE piece and the whatdbg piece that takes its form.

| Role | EVE (read) | whatdbg (this PLAN) |
|---|---|---|
| Main, owner of the hot path | `EVEProcessor : juce::AudioProcessor` owns the Model and the terminal processor (`EVEProcessor.h:4-5`, `:126`, `:136`) | `Whatdbg` owns `WHATDBGModel`, `Processor`, `dap::Writer`, and `dap::Reader`. It has the shape of an audio processor. It derives from none. |
| Prepare | `prepareToPlay` tells `terminalProcessor.prepare()` (`EVEProcessor.cpp:87-91`) | `Whatdbg::prepare` tells `processor.prepare` |
| Release | `~EVETerminalProcessor` calls `releaseResources()` (`EVETerminalProcessor.cpp:70-73`) | `~Processor` calls `releaseResources()` |
| Owner of the atoms, hot context | `EVETerminalProcessor : private jam::AsyncUpdater` (`EVETerminalProcessor.h:30-33`); a serial GCD queue on macOS, a one-thread pool on Windows (`jam_AsyncUpdater.cpp:13-33`) | `Processor : private jam::AsyncUpdater` |
| The atoms | `tty`, `terminalProcessor`, `terminalBuffer`, `swap`, the lanes (`EVETerminalProcessor.h:344-352`) | `debug::Session`, `debug::BreakpointManager`, `debug::VariableManager`, `debug::StopManager`, two `dap::FIFO` lanes |
| The work of one tick | `process()` (`EVETerminalProcessor.cpp:474-484`) | `Processor::process()` |
| Synchronization with the context | `prepare (winsize, …)` calls `handleUpdateNow()` (`EVETerminalProcessor.cpp:457-467`) | `prepare` and `releaseResources` call `handleUpdateNow()` |
| The one bounded wait | `tty.clock.wait()`, one vblank (`jam_TerminalClock.cpp:43-47`) | `Session::pollEvents (pollTimeoutMs)`, as today (`Whatdbg.cpp:17`, `:79`) |
| Requests in | `addInput` writes the `input` lane, then `device.requestProcess()` (`EVETerminalProcessor.cpp:265-283`) | `Processor::addRequest` writes the `requests` lane, then `triggerAsyncUpdate()` |
| State out | `events` entries call `state.storeValue` (`EVETerminalProcessor.cpp:123-148`) | `events` entries call `model.storeValue` |
| Events out, none lost | the lane and a counter: `commands` and `numCommands` (`EVETerminalProcessor.cpp:128-141`); "The lane carries every event of a write" (`EVETerminalProcessor.h:241`) | the `messages` lane and `numMessages` |
| Listener on the message thread | `EVEView::valueTreePropertyChanged` → `terminalEvents.get (property, tree)`, four entries (`EVEView.cpp:131-163`) | `Whatdbg::valueTreePropertyChanged` → `events.get (property, tree)`, two entries |
| The lane drain | `numCommands` → `applyCommands` (`EVEView.cpp:141-144`) | `numMessages` → `processor.applyMessages` → `writer.writeMessage` |
| The end of the program | `shellExited` → quit (`EVEView.cpp:156-163`) | `isRunning` → `stopDispatchLoop` |
| The state owner | `ENDModel : jam::Model, jam::Instance<ENDModel>` (`end/Source/end/ENDModel.h:19-21`) | `WHATDBGModel : jam::Model, jam::Instance<WHATDBGModel>` |

The audio contract, clause by clause:

| Clause | Audio contract | whatdbg |
|---|---|---|
| Prepare before process | `prepareToPlay`: "Called before playback starts, to let the processor prepare itself" (`juce_AudioProcessor.h:121`) | `prepare` runs `session.initialize` before the reader starts |
| Process on the hot thread, no UI | "called by the audio thread, so any kind of interaction with the UI is absolutely out of the question" (`juce_AudioProcessor.h:211-213`) | `process` runs on the engine context. It writes no stdout. |
| Inputs | atomic parameters and an event buffer | `store` / `getValue`: "any thread, lock-free" (`jam_Model.h:14`), and the `requests` lane |
| Parameter change | `ParameterBase::Listener::parameterValueChanged`: "JUCE analog: AudioProcessorParameter::Listener" (`jam_ParameterBase.h:48-61`) | `numMessages` and `isRunning` reach the message thread through `jam::Model::ParameterAttachment` (`jam_Model.h:237-245`) |
| Thread of prepare and release | the caller's thread, with the context stopped (`jam_VirtualDevice.cpp:37-60`) | the engine context. DbgEng: "Clients are specific to the thread that created them. Calls from other threads fail immediately." `handleUpdateNow()` runs the callback on the serial context while the caller waits (`jam_AsyncUpdater.cpp:65`, `:69`). |

Threads after the rewrite:

| Thread | Owns | Never does |
|---|---|---|
| Message thread (the main thread, `runDispatchLoop`) | `Whatdbg`, `WHATDBGModel`, `dap::Writer`, stdout | an engine call |
| Engine context (`Processor`) | `Session`, `BreakpointManager`, `VariableManager`, `StopManager`, the `commands` table | a write to stdout; a touch of the `juce::ValueTree` |
| Reader thread (`dap::Reader`) | stdin | anything else, except `session.wake ()` and `session.cancel ()` (decision 5) |

The data flows one way: stdin → Reader → `requests` lane → engine context → `messages` lane and Model parameters → message thread → stdout.

## Deliberate differences from the parity baseline

| # | Old behavior | New behavior | Basis | Part |
|---|---|---|---|---|
| D1 | macOS: a request that arrives while the target runs waits for the poll to end, up to 1 s (`Session_mac.cpp:316-320`) | macOS: `Session::wake()` ends the wait at once. Windows: the wait keeps its 50 ms bound (`Whatdbg.cpp:17`), as today. | `Listener.cpp:174-176`, `:269-271`. Decision 5. | 6 |
| D2 | A stopped target costs a 10 ms sleep for each pass (`Whatdbg.cpp:83`) | The context has no work and no timer while the target is stopped. A request starts it. | `jam_AsyncUpdater.cpp:48-60` | 6 |
| D3 | A full request queue drops the request with a debug log only (`Reader.cpp:131-137`). The output text has no limit (`Callbacks.cpp:187`). | A full lane, `requests` or `messages`, asserts and does not write the message, in the EVE form (`EVETerminalProcessor.cpp:132-134`). | Decision 8 | 3, 6 |
| D4 | Debuggee output is collected in a string and sent after the stop event of the same pass (`Whatdbg.cpp:266-280`) | Each output report is one `output` event, sent when the engine reports it | Ruling 6. `EVETerminalProcessor.h:241`. | 5 |
| D5 | macOS: a step stop always writes `stopReason = step` (`Session_mac.cpp:42`) | A step stop writes `step` only when `stopReason` is `none`, the Windows rule (`Session.cpp:232-233`) | One entry for both platforms (Part 5). On macOS a step stop meets another pending reason only while `initialBreak` waits for `configurationDone`. | 5 |
| D6 | A `disconnect` or a `terminate` waits for the engine as each request does | The reader calls `session.cancel ()` before it writes a `disconnect` or a `terminate` to the lane | Ruling 3 | 6 |
| D7 | A request that arrives after the end is applied only when it is in the queue during the pass of the end (`Whatdbg.cpp:73-74`) | A request that arrives after the end and before the reader stops is applied. The engine is still valid: `shutdown` runs in `releaseResources`, after the reader stops. | `Session.cpp:261`: `shutdown` resets `control`. | 6 |

## Decisions of this PLAN, each with its basis

**Decision 1. The home of each `debug::State` field** (`State.h`; Part 1, T2).

| Field | Home | Basis |
|---|---|---|
| `executionState`, `stopReason`, `processExitCode`, `targetProcessId`, `terminateDeadlineMs`, `isRunning`, `shouldTerminateOnExit`, `isConfigurationDone`, `isStepPending`, `isPausePending`, `breakpointEngineId`, `exceptionCode`, `exceptionAddress`, `isMachException` | `jam::Parameter<int>` or `jam::Parameter<int64_t>` in `WHATDBGModel` | MANIFESTO **S**: "State belongs exclusively to the Model." "store()/getValue() … any thread, lock-free" (`jam_Model.h:14`). |
| `nextVariablesRef`, `variablesRefMap`, `nextFrameId`, `frameIdMap`, `lastScopesThreadId` | Members of `debug::VariableManager` | One registry, one owner: "identity is assigned and released through the registry's own API" (MANIFESTO **E**, Complete at Creation). The sibling is `debug::BreakpointManager`, which holds `breakpoints` and `nextDapId` (`BreakpointManager.h:211`, `:214`). |
| `lastLoadedImageName` | Member of `debug::StopManager` | It is the payload of the module-load stop. One entry writes it, and one drain reads it (Part 1, T2). |
| `targetProgram` | Member of `Processor`, given to `StopManager::onStop` at the call | `jam::ParameterText::getValue` changes `front` (`jam_ParameterText.cpp:54-60`), and the Model flush calls it on the message thread. A second reader thread is a data race (MANIFESTO anti-pattern: "A plain (non-atomic) read that races a write"). |
| `hasDebuggeeOutput`, `debuggeeOutputText`, `debuggeeOutputCategory` | Deleted (difference D4) | Decision 6 |
| `hasProcessExited`, `hasBreakpointLocationsResolved`, `resolvedBreakpointEngineId`, `resolvedBreakpointLine` | Deleted | Decision 6 |
| `dap::nextSequenceNumber` (`Types.h:27`, a process-wide variable) | `jam::Parameter<int>` in `WHATDBGModel` | Ruling 6: "no stray variables". |

**Decision 2. The hot context is `jam::AsyncUpdater`, with no `jam::VirtualDevice`.** `EVETerminalProcessor` is itself `private jam::AsyncUpdater` (`EVETerminalProcessor.h:30-33`), and `jam::AsyncUpdater` is in `jam_core` (`jam_core.h:118`). `jam::VirtualDevice` calls `audioDeviceAboutToStart` and `audioDeviceStopped` on the caller's thread (`jam_VirtualDevice.cpp:37-60`). DbgEng needs them on the context. MANIFESTO **L**, YAGNI.

**Decision 3. A message reaches the message thread at once through `Parameter::setValue` and `jam::Model::ParameterAttachment`. This is the answer to RFC question 10.**
- Ruling 2 is "Send at once". The other JAM lane is the flush timer, at 10, 60, or 120 Hz (`jam_Model.h:8`), and ruling 2 rejects it.
- The attachment is the established any-thread lane: "listener bridge delivering parameter changes to a callback on the message thread" (`jam_Model.h:10`); "`parameterChanged` — the any-thread lane (fires synchronously on the writing thread)" (`end/ARCHITECTURE.md:921-923`).
- The code of whatdbg takes no lock on the engine context. The lanes and the atomics are lock-free.
- Two framework locks are in the path of one `setValue`. `sendValueChangedMessageToListeners` holds `listenerLock`, a `juce::CriticalSection` (`jam_ParameterBase.h:100`, `:131`). The message thread takes it only when an attachment is made or destroyed (`:82`, `:90`), so the two threads do not wait on it while the program runs. The JUCE message queue holds its own `CriticalSection` in `post` (`juce_MessageQueue_mac.h:65-72`). JUCE has no other way to wake its message loop from another thread.
- The engine libraries hold their own locks: "individual SB calls are protected by a mutex" (RFC:89). ARCHITECT: "we dont fight them, we integrate seamlessly".
- EVE's hot context calls `storeValue` only, and the view pulls with `state.flush()` (`EVETerminalProcessor.cpp:126`, `:139`, `:242`). The pull has the latency that ruling 2 rejects.
- Only `numMessages` and `isRunning` use `setValue`. Each other parameter uses `store`.

**Decision 4. The DAP words become `Id::` rows in Part 3.** The files that hold them are rewritten in this PLAN, and the literals break MANIFESTO **E** ("No magic values") and **S** ("A hardcoded value appearing more than once"): "seq" has 21 sites (Part 1, T14). CAROL.md, Definitive Correctness Foundation item 5.

**Decision 5. `Session::wake` and `Session::cancel` are two tells, called from the reader thread.**
- macOS, `wake`: the listener keeps an event that arrives while no thread waits, and the next wait returns it (`Listener.cpp:174-176`, `:269-271`). lldb-dap wakes its event thread the same way, from another thread (`lldb-dap/DAP.cpp:240-243`, `:1284-1293`). So a wake on each request is safe.
- Windows, `wake`: the body has no statement. Windows keeps what works today: the 50 ms bound of the wait (`Whatdbg.cpp:17`; `Session.cpp:218`). `SetInterrupt (DEBUG_INTERRUPT_EXIT)` is the call of `cancel`, not of `wake`: "If there is currently a WaitForEvent call running, the engine will force it to return. … Otherwise, when the target is suspended, register a user interrupt." A wake for each request would register a user interrupt each time a request arrives while the target is stopped. The form of a platform body with no action is `Session::loadModuleSymbols` on macOS (Part 1, T5).
- `cancel`, ruling 3: the reader calls it only for `disconnect` and `terminate`, and before it writes the request to the lane. Windows: `control->SetInterrupt (DEBUG_INTERRUPT_EXIT)`. macOS: `wake ()`. The plan names no LLDB call that cancels a blocked SB call; on macOS a blocked SB call holds the context, as it holds the loop today.
- `resetCancel`: `onDisconnect` calls it first. Windows: `control->GetInterrupt ()`: "If a user interrupt was issued, it is cleared when this method is called." and "Calling SetInterrupt also causes a user interrupt." (GetInterrupt page). The order is fixed: the reader cancels, then writes the request; the context applies the request, and its first statement clears the interrupt. So `terminateDebuggee` and `shutdown` run with no interrupt registered.
- MANIFESTO anti-pattern "Cross-thread direct call": these two tells are the engine's own any-thread calls. `SetInterrupt`: "This method can be called at any time and from any thread."

**Decision 6. The flags (ruling 6: "no manual flag checking"; RFC:94: "so no loop reads and clears flags").**

| Flag | Fate | Basis |
|---|---|---|
| `hasDebuggeeOutput` | Deleted. The output entry adds the `output` event (D4). | The entry runs on the thread that owns the lane. The drain made no engine call (`Whatdbg.cpp:195-208`). |
| `hasProcessExited` | Deleted. The exit entries call `drainProcessExited`, which acts when `executionState` is not `exited`. | The drain made no engine call (`Whatdbg.cpp:228-249`). `executionState` is the state machine. |
| `hasBreakpointLocationsResolved` | Deleted. The entry calls `breakpointManager.onBreakpointLocationFound`. | The drain made no engine call (`Whatdbg.cpp:182-193`). |
| `stopReason` | Stays. It is the one deferred stop. | The Windows callbacks run inside `WaitForEvent`, and the drains make engine calls. `WaitForEvent`: "it cannot be called from the event callbacks". The current structure applies the stop after the wait returns (`Whatdbg.cpp:266-274`), and parity keeps it. |
| `isStepPending`, `isPausePending` | Stay, as Model parameters. A request sets them; the event that they classify reads them. No loop reads them. | They are calculation inputs of the native tests (`Callbacks.cpp:28`, `:344`; `Session.cpp:227`; `Session_mac.cpp:114`). DbgEng reports a pause as a breakpoint exception (`Callbacks.cpp:22-42`). MANIFESTO **S**: "calculation inputs, always a deterministic reflection of the Model". |
| `isConfigurationDone` | Stays, as a Model parameter. | It is a state of the protocol. The stop drain reads it one time for each tick, and a tick is an event. |
| `shouldTerminateOnExit` | Stays, as a Model parameter. | `getEndModeForExit` reads it one time (`Whatdbg.cpp:306`). |
| `isRunning` | Stays, as a Model parameter. The message thread hears it. | The form is `shellExited` (`EVEView.cpp:156-163`). |

**Decision 7. The message thread reacts to two parameters, and this is the EVE form.** `EVEView` has four entries (`EVEView.cpp:141-163`); the terminal logic runs on the hot context (`EVETerminalProcessor.cpp:482`). In whatdbg, the Model and the listener are on the message thread (ruling 1), and the engine logic is in the atoms that `Processor` owns (ruling 11). MANIFESTO **E**: "The Model holds the state. The View composes. The Processor holds the business logic."

**Decision 8. The lanes.** `requestsCapacity` is 64, the value of `dap::Reader::fifoCapacity` (`Reader.h:23`). `messagesCapacity` is 4096. No fact of the code gives this number. It is 4096 slots of one `juce::var`, allocated one time. A full lane asserts and does not write (D3), as EVE does; a Release build then drops the message with no signal. ARCHITECT ratifies the number and the policy at the lock.

**Decision 9. The four failure causes of the old record** (`ARCHITECTURE.md:319`).

| Cause in the record | Answer |
|---|---|
| "COM thread isolation issues" | The engine context is one thread on Windows: minimum 1, maximum 1 (`jam_AsyncUpdater.cpp:22-28`). Each engine call, `initialize` and `shutdown` included, runs on it. |
| "callback flooding" | `juce::AsyncUpdater::triggerAsyncUpdate` posts one message while one is pending (`juce_AsyncUpdater.cpp:80-81`). One flush drains each message of the lane. |
| "message thread starvation" | The message thread makes no engine call and no engine wait. It writes stdout only. |
| "timing bugs" | One context writes the `messages` lane, so it is the one author of the DAP order. |

The message loop is the loop of each JUCE application: `JUCEApplicationBase::main` calls `runDispatchLoop()` (`juce_ApplicationBase.cpp:277`), and EVE and END run on it. whatdbg calls the same function after `juce::ScopedJuceInitialiser_GUI`. On macOS it is `[NSApp run]` behind one test of `quitMessagePosted` (`juce_MessageManager_mac.mm:323-351`). Parts 2 to 5 keep the hand-written loop; Part 6 changes to the message loop.

**Decision 10. The timings use the Verdict lane of scout** (`profile/SKILL.md:25`): the rows `epoch`, `user`, and `system`, with nothing rebuilt. scout starts its command with `execvp` on macOS (`jam_ChildProcess_posix.cpp:57-60`) and with `CreateProcessW` on Windows (`jam_ChildProcess_windows.cpp:27-29`), which runs a `.cmd` file through `cmd.exe` (MSRC blog, MS14-019: "CreateProcess uses the cmd.exe automatically to run the input .cmd or .bat."). So each platform has its launcher file and its manifest: `smoke.sh` with `smoke.md`, and `smoke.cmd` with `smoke-windows.md` (Part 1 Step 4). Sandbox run of 2026-11-12 on macOS: scout runs the script, accepts an empty samples file, and writes the three rows with the median, `errorPercent`, `min`, `max`, and `samples`. With an empty samples file the report has no table type, and a `baseline` in the manifest adds no `change` column. So COUNSELOR reads the two reports side by side.

## Dependency & API Inventory

Each row is an existing piece. The full read is in the part that uses the piece.

| Need | Existing piece | Where |
|---|---|---|
| A serial context with a coalescing trigger | `jam::AsyncUpdater`: `triggerAsyncUpdate()` "Any thread"; `handleAsyncUpdate()`; `handleUpdateNow()`; `cancelPendingUpdate()` | `jam/jam_core/concurrency/jam_AsyncUpdater.h:23-76`; `.cpp:3-91` |
| A state owner | `jam::Model`: `createAndAddParameter`, `storeValue`, `getParameter`, `flush()` ("MESSAGE THREAD only"), `addListener` | `jam/jam_data_structures/model/jam_Model.h:13-16`, `:822`, `:928`, `:988-1057` |
| An atomic value | `jam::Parameter<int>`, `jam::Parameter<int64_t>`: `store`, `setValue`, `getValue`. "Bool parameters are stored as int (0/1)". | `jam_ParameterBase.h:145-247` |
| A change delivered on the message thread | `jam::Model::ParameterAttachment` | `jam_Model.h:246-282`; `jam_Model.cpp:201-240` |
| A global lookup | `jam::Instance<ObjectType>`: `getInstance()` "lock-free atomic read, visible from any thread" | `jam/jam_core/utils/jam_Instance.h:10-32`, `:113` |
| A dispatch table | `jam::Function::Map`: one signature (`:185-249`), several signatures (`:96-173`). The callable receives the key first. | `jam/jam_core/function_map/jam_Function.h` |
| A lane | `juce::AbstractFifo` over a sized `std::vector<juce::var>`, as `dap::Reader` holds it today. JAM has no generic object queue (`jam/docs/xml/index.xml`: `Mailbox`, `SpectrumFIFO`, `TerminalFIFO` only). | `Source/dap/Reader.h:23`, `:81-84`; `Reader.cpp:46-55`, `:120-138` |
| The lane API names | `getFreeSpace`, `write`, `applyPendingData` | `jam/jam_terminal/processor/jam_TerminalFIFO.h:29-111` |
| The message loop | `juce::ScopedJuceInitialiser_GUI`; `runDispatchLoop()`; `stopDispatchLoop()` | JUCE `juce_events/messages/juce_Initialisation.h:81-91`; `juce_MessageManager.h:84`, `:93` |
| The lexicon | `cast/identifiers.md`, generated as `Source/generated/Identifiers.h` in `namespace Id` | `scout/cast/identifiers.md:1-40`; `scout/cast/spell.md:22-23`, `:146`, `:153` |
| The help page of a console program | `printHelp`, the same 35 lines in three projects | `cast/Source/Help.cpp:3-35`; `ggwp/Source/Help.cpp`; `scout/Source/Help.cpp` |
| The interrupt on Windows | `IDebugControl::SetInterrupt`, `GetInterrupt`; `WaitForEvent` returns `E_PENDING`: "An exit interrupt was issued." | learn.microsoft.com, `nf-dbgeng-idebugcontrol-setinterrupt`, `-getinterrupt`, `-waitforevent` |
| The engine thread rule on Windows | "Clients are specific to the thread that created them. Calls from other threads fail immediately." | learn.microsoft.com, `nf-dbgeng-idebugclient-createclient` |
| The wake on macOS | `SBBroadcaster (const char* name)` (`:20`), `BroadcastEventByType` (`:34`), `AddListener` (`:41`) | `Resources/macos/include/lldb/API/SBBroadcaster.h`; llvm-project `llvmorg-21.1.8`, `lldb/source/Utility/Listener.cpp:174-176`, `:269-271` |

### Names

Each name comes from an established family (NAMES.md Rule 5), from ARCHITECT, or from the document that the row cites. The lock is the approval of the rows marked new.

| Name | Source |
|---|---|
| `WHATDBGModel` | `ENDModel`: the project name and `Model` (`end/Source/end/ENDModel.h:19`) |
| `Processor` | MANIFESTO: "The Processor holds the business logic"; `EVETerminalProcessor` |
| `prepare`, `process`, `releaseResources` | `EVETerminalProcessor::prepare` (`.cpp:347`), `process` (`:474`), `releaseResources` (`:367`) |
| `addRequest`, `applyMessages` | `addInput` (`.cpp:265`) and `applyCommands` (`.cpp:178`): the verb and the lane |
| `requests`, `messages`, `numMessages` | DAP's own words; `commands`, `gridEvents` (`EVETerminalProcessor.h:351-352`); `numCommands` (`eve/cast/identifiers.md:22`) |
| `addMessage`, `addMessages` | the verb `add` and the lane `messages` |
| `onPrepare`, `onReleaseResources`, `Id::prepare`, `Id::releaseResources` | the tell, and the `on*` family of the `commands` table (`Whatdbg.cpp:26-43`) |
| `dap::getRequest` | the `dap::get*` builders: `getResponse`, `getEvent` (`Types.h:49`, `:100`) |
| `dap::getOutputEvent` | `getExceptionOutputEvent` (`Whatdbg.cpp:765`) |
| `dap::FIFO` | `jam::TerminalFIFO`: the domain and `FIFO`. The namespace `dap` gives the domain. |
| `dap::Writer` | the sibling of `dap::Reader` (`Reader.h:20`) |
| `debug::VariableManager` | the sibling of `debug::BreakpointManager`. NAMES Rule 6: `manager` is permitted "where the managed domain is in the name". Its `on*` members mirror the commands, as `BreakpointManager::onSetBreakpoints` does. |
| `debug::StopManager`, `onStop` | the same family; `debug::StopReason`; the `stops` table (`Whatdbg.h:82`). New. |
| `debug::Events`, `events`, `registerEvents` | `jam::TerminalProcessor::Events` (`jam_TerminalSwap.h:34`); `EVETerminalProcessor::registerEvents` (`.cpp:123`) |
| `Id::breakpointLocationFound` | `BreakpointManager::onBreakpointLocationFound` (`BreakpointManager.h:125`) |
| `Id::exited`, `Id::terminated`, each other event key | DAP words and `debug::StopReason` enumerators (Part 5) |
| `standardOutput`, `standardError`, `continueCommand`, `requestSeq` | `isStandardOutput`, `isStandardError` (`Session_mac.cpp:176-177`); the noun of the `commands` table; `request_seq` with no underscore |
| `Session::wake` | the one-word tells of `Session`: `resume`, `interrupt` (`Session.h:129`, `:315`). New. |
| `Session::cancel`, `resetCancel` | RFC:138: "Cancel of a blocked engine call"; `resetSymbolGroupCache` (`Session.h:497`). New. |
| `cancellations` | NAMES Rule 8: a plain noun for what the table holds. New. |
| `wakeEventMask`, `broadcaster` | `allEventsMask` (`Session_mac.cpp:74`); `lldb::SBBroadcaster broadcaster` (`lldb-dap/DAP.h:99`) |
| `onMessage` | `tty->onBytesReady` (`EVETerminalProcessor.cpp:61`) |
| `sessionTag`, `exceptionTag` | `terminalTag` (`EVETerminalProcessor.h:344`) |
| `Id::adapterExitCode` | `runAdapter` and its local `exitCode` (`Main.cpp:238`, `:240`). `Id::exitCode` stays the DAP word for the exit code of the debuggee (`Whatdbg.cpp:240`). |
| `Id::nextSequenceNumber` | `dap::nextSequenceNumber` (`Types.h:27`) |
| Each other `Id::` row of the Model | the name of the `debug::State` field that it replaces, verbatim (Part 1, T2) |
| `jam_tui`, `jam::TerminalPage` | ARCHITECT, ruling 12 |
| `TerminalPage::writeToStream` | `TerminalBuffer::writeToStream` (`jam_TerminalBuffer.h:104`) |

## Validation Gate

Each step is validated by COUNSELOR before proceeding to the next — against
MANIFESTO.md (BLESSED), NAMES.md, ~/.carol/CODING.md, and the locked PLAN decisions
(no deviation, no scope drift). @Auditor runs ONCE, after the final step, covering
the whole sprint — never per step.

Each numbered part is one sprint: COUNSELOR executes its steps, @Auditor runs once at its end, and the sprint is logged. The lock is one, for the whole PLAN.

## Steps

### Step 1: Lock
**Scope:** the eight plan files.
**Action:** ARCHITECT locks the PLAN as one. The Step Gate of CAROL.md governs from the lock.
**Validation:** The Status table reads "Locked" with the date in each row.

### Step 2: Track
**Scope:** this file, section Status.
**Action:** Set the status of a part when its sprint is logged.
**Validation:** The Status table matches `carol/SPRINT-LOG.md`.

## BLESSED Alignment

- **B — Bound.** Each thread owns its objects (section The design, Threads). `Processor` owns the engine from `prepare` to `releaseResources`. The reader stops before the engine shuts down.
- **L — Lean.** `Whatdbg.cpp` is 786 lines today, with six responsibilities. After the rewrite each responsibility has one owner: `Whatdbg`, `Processor`, `dap::Writer`, `debug::VariableManager`, `debug::StopManager`, and the builders of `dap::Types`.
- **E — Explicit.** Each DAP word and each Model name is a generated `Id::`. A full lane is an assert.
- **S — SSOT.** One `events` table serves both platforms. One Model holds the state. One module holds the help page. `debug::State` is deleted.
- **S — Stateless.** The native bodies report and hold no state. `Whatdbg` holds no flag: it reacts to the Model.
- **E — Encapsulation.** Rulings 10 and 11. The data flows one way. The message thread tells the engine through `prepare`, `addRequest`, and `releaseResources` only.
- **D — Deterministic.** One context is the one author of the order of the DAP messages. Part 7 proves equal message sequences for equal scenarios.
- LANGUAGE.md: no override applies beyond the C++ / JUCE Lean adaptation.

## Risks / Open Questions

None open.

### Facts for the review of the whole PLAN

- **The GCD queue is not one fixed thread.** A serial queue runs one block at a time, on a thread that the system selects. On Windows the pool has one thread (`jam_AsyncUpdater.cpp:21-33`), so the DbgEng client lives on the thread that created it.
- **`E_PENDING` is not an exit.** `Session::pollEvents` on Windows reads only `E_UNEXPECTED` and `S_OK` (`Session.cpp:222-237`).
- **The engine context blocks in an engine call.** A long `evaluate` holds the context, and requests wait in the lane, as they wait for the loop today. Ruling 3 covers `disconnect` and `terminate`: `cancel` interrupts the engine. Part 6 Step 10 verifies it on the Windows machine.
- **Module cost.** The help page brings thirteen JAM modules into a console program (Part 2). Part 2 records the binary size before and after.
- **The help page writes SGR bytes when stdout is no terminal.** Run of `scout --help` to a file, 2026-11-12: 408 of 418 lines hold an escape byte. An agent that reads `whatdbg --help` gets these bytes (CAROL.md Debugging names that command).
- **The doc of `Session::shutdown` is wrong for Windows.** `Session.h:150-153` says that a second call "is a no-op". `Session.cpp:257-262` resets the pointers without a guard. The second call is harmless: each pointer is already null.
