# PLAN: Audio Architecture Discipline Part 6 — The Core: Model, Processor, Message Loop

**RFC:** `RFC-message-loop-model.md`. Canon: `PLAN-audio-architecture-discipline-0-tracker.md`, rulings 1, 2, 3, 5, 6, 9, 10, 11; decisions 1, 2, 3, 5, 8, 9; differences D1, D2, D3, D6, D7.
**Date:** 2026-11-12
**BLESSED Compliance:** verified
**Language Constraints:** C++17 / JUCE 8.0.17 / JAM (LANGUAGE.md, C++ / JUCE)

## Overview

This part gives whatdbg the shape of an audio processor (ruling 10). `debug::State` and the hand-written loop are deleted. `WHATDBGModel` holds the state. `Whatdbg` is main: it owns the Model, the processor, the writer, and the reader, and it listens on the message thread. `Processor` owns the atoms on its own serial context and runs each command and each stop there.
The atoms and the entries exist after Parts 4 and 5. So this part changes three things only: a state field becomes a Model parameter, a stdout write becomes a write to the lane, and the loop becomes `process()`.

## What this part does not do

- No change of an engine call, of a `BreakpointManager` rule, or of a DAP message body.
- No change of the native tests that select an event (Part 5).
- No comment and no doxygen. The doxygen pass comes after the audit.

## Language / Framework Constraints

- Rulings 10 and 11: the shape and the contract of an audio processor, not derived. Main owns the hot path. The processor owns the atoms.
- MANIFESTO **B**: "Threads are bound to their context". No message-thread code calls an engine function. No engine-context code touches the `juce::ValueTree` or stdout.
- MANIFESTO **B**, The Guard Rule: each wait and each order of this part names its threat.
- MANIFESTO **D**: no guard without a named threat. The `commands` table holds the two tells `prepare` and `releaseResources` beside the DAP commands, and no code tests who sent a message.
- `jam_Model.h:13-16`: "Parameter store()/getValue() and Model::storeValue(): any thread, lock-free"; "flush(), timerCallback(), get(): MESSAGE THREAD only"; "createAndAddParameter(), ParameterAttachment ctor/dtor: MESSAGE THREAD, construction/destruction only".
- DbgEng: "Clients are specific to the thread that created them. Calls from other threads fail immediately." `Session::initialize` and `Session::shutdown` run on the engine context.
- CODING.md: no out-parameter; one object, one translation unit.

## Dependency & API Inventory

Reads of 2026-11-12. Whatdbg lines are of commit `7f0d713`; Parts 3 to 5 moved some of them.

| Need | Existing piece | Where |
|---|---|---|
| A state owner with a global lookup | `class ENDModel : public jam::Model, public jam::Instance<ENDModel>`; `jam::Model (jam::Format::toType (Id::end))`; `getOrCreateChildWithName (jam::Format::toType (Id::overlay))`; `createAndAddParameter<…> (row, id, defaultValue)` | `end/Source/end/ENDModel.h:19-38`; `ENDModel.cpp:3-10` |
| A scalar parameter | `storeValue (tag, id, value)`: the type of `value` selects `Parameter<ValueType>` (`:1013-1019`); `getParameter<jam::Parameter<int>> (tag, id)->getValue()` (`:988-1000`); "Bool parameters are stored as int (0/1)" | `jam_Model.h`; `jam_ParameterBase.h:145` |
| `setValue` | `store (v)`, then the listeners, on the calling thread | `jam_ParameterBase.h:168-172` |
| The flush | writes a parameter whose `needsUpdate` flag is set and whose value differs from the tree | `jam_Model.cpp:33-58`, `:291-303` |
| The tag of a row | `terminalTag { jam::Format::toType (Id::terminal) }` | `eve/Source/EVETerminalProcessor.h:344` |
| A reference to the global model as a member | `ENDModel& appModel { *ENDModel::getInstance() };` | `end/Source/config/ConfigModel.h:118` |
| The Model timer stopped | `state.stopTimer();` | `eve/Source/EVETerminalProcessor.cpp:54` |
| A change delivered to the message thread | `std::make_unique<jam::Model::ParameterAttachment> (parameter, [this] (const juce::var&) { ... })`. On another thread the change runs `triggerAsyncUpdate()` of `juce::AsyncUpdater`. | `eve/Source/EVETerminalProcessor.cpp:56-57`; `jam_Model.cpp:220-240` |
| The serial context | `triggerAsyncUpdate()`; `handleAsyncUpdate()`; `handleUpdateNow()`: "Queues a call of `handleAsyncUpdate()` and waits until the context delivers it." A trigger inside the callback queues one more call (`.cpp:7-8`, `:50`). | `jam/jam_core/concurrency/jam_AsyncUpdater.h:33-52`; `.cpp:3-77` |
| Release in the destructor | `EVETerminalProcessor::~EVETerminalProcessor() { releaseResources(); }` | `eve/Source/EVETerminalProcessor.cpp:70-73` |
| The listener | `valueTreePropertyChanged`: `if (events.contains (property)) events.get (property, tree);`; the map `jam::Function::Map<juce::Identifier, jam::Function::pointerCapacity, void (juce::ValueTree&)>` | `end/Source/plugins/Session.cpp:89-93`; `Session.h:213-214` |
| The lane drain from a counter | `terminalEvents.add (Id::numCommands, [this] (const juce::Identifier&, const juce::ValueTree&) { getTerminalProcessor().applyCommands (...); });` | `eve/Source/EVEView.cpp:141-144` |
| The counter write | `state.storeValue (terminalTag, Id::numCommands, …->getValue() + 1);` | `eve/Source/EVETerminalProcessor.cpp:139` |
| A callback member that the owner sets | `tty->onBytesReady = [this] (int numSamples) { ... };` | `eve/Source/EVETerminalProcessor.cpp:61-67` |
| The message loop | `juce::ScopedJuceInitialiser_GUI`; `runDispatchLoop()`; `stopDispatchLoop()`. A message posted before the loop runs stays in the queue (`juce_MessageQueue_mac.h:65-69`). | JUCE `juce_events` |
| The loop to delete | `Whatdbg::run` | `Whatdbg.cpp:63-91` |
| The start of the program | `Whatdbg whatdbg;`, `whatdbg.initialize (sidecarDir)`, `whatdbg.run()`, `exitCode = 1` on failure | `Main.cpp:261-276` |
| The command bodies | the eighteen `on*` functions, `onCommand`, `addSearchPaths`, `getEndModeForExit`, `drainProcessExited`, `drainTerminateTimeout`, `registerEvents`, `addMessage`, `addMessages` | `Whatdbg.cpp` after Part 5 |
| The atoms | `debug::Session`, `debug::BreakpointManager`, `debug::VariableManager`, `debug::StopManager`, `dap::FIFO`, `dap::Writer`, `dap::Reader` | Parts 3 to 5 |
| `shutdown` | Windows: `client->EndSession`, then `control.Reset ()` and the other pointers, then `CoUninitialize` when owned (`Session.cpp:241-269`). macOS: detach or kill, then `SBDebugger::Destroy` and `Terminate` (`Session_mac.cpp:356-380`). | `Source/debug/` |
| The wake API, macOS | `broadcaster("lldb-dap")`; `broadcaster.AddListener (listener, eBroadcastBitStopEventThread);`; `broadcaster.BroadcastEventByType (eBroadcastBitStopEventThread);` | llvm-project `llvmorg-21.1.8`, `lldb/tools/lldb-dap/DAP.cpp:122-124`, `:1284-1286`, `:240-243` |
| An event that is no process event and no breakpoint event is passed over | the two tests of `Session::pollEvents` | `Session_mac.cpp:326-333` |
| The interrupt API, Windows | `SetInterrupt (DEBUG_INTERRUPT_EXIT)`; `GetInterrupt`: "If a user interrupt was issued, it is cleared when this method is called." | learn.microsoft.com |

## The design

### `WHATDBGModel`

`class WHATDBGModel : public jam::Model, public jam::Instance<WHATDBGModel>`, in `Source/WHATDBGModel.h` and `.cpp`, in the form of `ENDModel`.

Public members: `WHATDBGModel ();`, `const juce::Identifier sessionTag { jam::Format::toType (Id::session) };`, and `const juce::Identifier exceptionTag { jam::Format::toType (Id::exception) };`. The base is `jam::Model (jam::Format::toType (Id::whatdbg))`. The constructor makes the two rows with `getOrCreateChildWithName (sessionTag)` and `getOrCreateChildWithName (exceptionTag)`, makes their parameters, and then calls `stopTimer()`.

A `bool`, an `int`, an `int32`, or an enum field is a `jam::Parameter<int>`. A `uint32` or a `uint64` field is a `jam::Parameter<int64_t>`, so that each value of the field has a place.

| Row | Parameter | Type | Default | Replaces |
|---|---|---|---|---|
| SESSION | `Id::executionState` | int | `ExecutionState::idle` | `State.h:91` |
| SESSION | `Id::stopReason` | int | `StopReason::none` | `State.h:97` |
| SESSION | `Id::processExitCode` | int | 0 | `State.h:104` |
| SESSION | `Id::targetProcessId` | int64 | 0 | `State.h:114` |
| SESSION | `Id::terminateDeadlineMs` | int64 | 0 | `State.h:132` |
| SESSION | `Id::isRunning` | int | 1 | `State.h:142` |
| SESSION | `Id::shouldTerminateOnExit` | int | 0 | `State.h:148` |
| SESSION | `Id::isConfigurationDone` | int | 0 | `State.h:154` |
| SESSION | `Id::isStepPending` | int | 0 | `State.h:162` |
| SESSION | `Id::isPausePending` | int | 0 | `State.h:171` |
| SESSION | `Id::breakpointEngineId` | int | 0 | `State.h:181` |
| SESSION | `Id::nextSequenceNumber` | int | 1 | `Types.cpp`, `nextSequenceNumber` |
| SESSION | `Id::numMessages` | int | 0 | new: the counter of the `messages` lane |
| SESSION | `Id::adapterExitCode` | int | 0 | new: the exit code of the adapter, `1` today at `Main.cpp:276` |
| EXCEPTION | `Id::exceptionCode` | int64 | 0 | `State.h:233` |
| EXCEPTION | `Id::exceptionAddress` | int64 | 0 | `State.h:240` |
| EXCEPTION | `Id::isMachException` | int | 0 | `State.h:252` |

`State.h` keeps the enums `ExecutionState`, `StopReason`, `OffsetStatus` and the alias `Events`. `class State` is deleted. `State.h` includes `../WHATDBGModel.h`, so each file of the debug layer sees the Model. `WHATDBGModel.cpp` includes `debug/State.h` for the two enums of its defaults.

The forms, with `model` for the reference and `tag` for `model.sessionTag` or `model.exceptionTag`:
- A read: `model.getParameter<jam::Parameter<int>> (tag, Id::name)->getValue()`. An enum read casts the result with `static_cast`. A code or an address read uses `jam::Parameter<int64_t>`.
- A write: `model.storeValue (tag, Id::name, value)`, with the value cast to `int` or to `int64_t`.
- A write that the message thread must hear at once: `model.getParameter<jam::Parameter<int>> (tag, Id::name)->setValue (value)`. Only `Id::isRunning` and `Id::numMessages` use it.

`dap::nextSeq ()` (`Types.cpp`) becomes: read `Id::nextSequenceNumber` of `WHATDBGModel::getInstance ()`, store the value plus 1, return the value read.

`dap::Types` gets one more builder: `juce::var getRequest (const juce::Identifier& command, juce::var arguments = juce::var ());`. It makes an object with `Id::type` set to `Id::request.toString()`, `Id::command` set to `command.toString()`, and `Id::arguments` set to `arguments` when `arguments` is not void.

### `Session`: `wake`, `cancel`, `resetCancel`

Three new public members of `debug::Session`, each `void … () noexcept` (tracker, decision 5).

| Member | Caller | Windows (`Session.cpp`) | macOS (`Session_mac.cpp`) |
|---|---|---|---|
| `wake` | the reader thread, for each request | no statement | `broadcaster.BroadcastEventByType (wakeEventMask);` |
| `cancel` | the reader thread, for `disconnect` and `terminate`, before the lane write | `control->SetInterrupt (DEBUG_INTERRUPT_EXIT);` | `wake ();` |
| `resetCancel` | the engine context, the first statement of `onDisconnect` | `control->GetInterrupt ();` | no statement |

macOS: `Session` gets the member `lldb::SBBroadcaster broadcaster { "whatdbg" };` after `listener` (`Session.h:609`), and the file gets `static constexpr std::uint32_t wakeEventMask { 1u << 0 };` after `allEventsMask` (`Session_mac.cpp:74`). `Session::initialize` calls `broadcaster.AddListener (listener, wakeEventMask);` after `listener = debugger.GetListener ();` (`Session_mac.cpp:256`). `pollEvents` does not change: a wake event is neither a process event nor a breakpoint event.

The threat that the order of `cancel` answers: an interrupt that lands after `onDisconnect` cleared it would stay registered during `terminateDebuggee`. The reader calls `cancel` before it writes the request, so the clear always comes after the interrupt.

### `Processor`

`class Processor : private jam::AsyncUpdater`, in `Source/Processor.h` and `.cpp`.

Public:
- `Processor ();` It fills `commands` and `cancellations`, and calls `registerEvents ()`.
- `~Processor () override;` Its one statement is `releaseResources ();`, the form of `EVETerminalProcessor.cpp:70-73`.
- `void prepare (const juce::File& sidecarDirectory);` Message thread, before the reader starts. It writes `dap::getRequest (Id::prepare, arguments)` to `requests`, where `arguments` is an object with `Id::path` set to `sidecarDirectory.getFullPathName()`. It then calls `handleUpdateNow ()`. So `Session::initialize` runs on the context, and the caller waits for it.
- `void addRequest (juce::var request);` Reader thread. Its statements, in order:
  1. `const juce::String command { request[Id::command].toString() };`
  2. `if (cancellations.contains (command)) cancellations.get (command);`
  3. `requests.write (std::move (request));`
  4. `triggerAsyncUpdate ();`
  5. `session.wake ();`
- `template <typename Callback> void applyMessages (Callback&& callback)` Message thread. It forwards to `messages.applyPendingData`.

Private members, in this order:
- `WHATDBGModel& model { *WHATDBGModel::getInstance() };`
- `debug::Session session;`
- `debug::BreakpointManager breakpointManager { session };`
- `debug::VariableManager variableManager { session };`
- `debug::StopManager stopManager { session, breakpointManager, variableManager };`
- `static constexpr int requestsCapacity { 64 };` and `dap::FIFO requests { requestsCapacity };`
- `static constexpr int messagesCapacity { 4096 };` and `dap::FIFO messages { messagesCapacity };`
- `commands`, with the type of `Whatdbg.h:75`
- `jam::Function::Map<juce::String, jam::Function::pointerCapacity, void ()> cancellations;` with two entries, `Id::disconnect` and `Id::terminate`. Each one calls `session.cancel ()`.
- `static constexpr std::uint32_t terminateTimeoutMs { 5000 };`, moved from `Whatdbg.h:35`
- `juce::String targetProgram;`

Private functions:
- `void releaseResources ();` It writes `dap::getRequest (Id::releaseResources)` to `requests` and calls `handleUpdateNow ()`. The reader is stopped at this time (section `Whatdbg`), so the lane has one writer.
- `void handleAsyncUpdate () override;` calls `process ()`.
- `void process ();`
- `void addMessage (juce::var message);` calls `messages.write (std::move (message))`, then `setValue` of `Id::numMessages` with its value plus 1.
- `void addMessages (const juce::Array<juce::var>& messages);` one `addMessage` for each element.
- `void onPrepare (const juce::var& message);` calls `session.initialize` with the `juce::File` of `message[Id::arguments][Id::path]`. When it returns `false`, it stores `1` to `Id::adapterExitCode` and sets `Id::isRunning` to `0` with `setValue`.
- `void onReleaseResources (const juce::var& message);` calls `session.shutdown (getEndModeForExit ())`, then stores `ExecutionState::idle` to `Id::executionState`. The threat that the store answers: statement 2 of `process ()` polls a session that is shut down.
- `onCommand`, the eighteen `on*` functions, `addSearchPaths`, `getEndModeForExit`, `drainProcessExited`, `drainTerminateTimeout`, and `registerEvents`, each moved from `Whatdbg` with its name and signature.

`process ()` has five statements:

1. `requests.applyPendingData ([this] (const juce::var& request) { onCommand (request); });`
2. When `Id::executionState` is `running` or `launching`: `session.pollEvents (pollTimeoutMs);`. This is the one bounded wait of the context. `pollTimeoutMs` moves here from `Whatdbg.cpp:17`.
3. `addMessages (stopManager.onStop (targetProgram));`
4. `drainTerminateTimeout ();`
5. When `Id::isRunning` is not 0 and `Id::executionState` is `running` or `launching`: `triggerAsyncUpdate ();`.

While the target is stopped, statement 5 queues nothing, and the context has no work (difference D2). The next `addRequest` starts it.

The moved bodies change in these ways only:

| Old | New |
|---|---|
| `state.field` read or write | the Model read or write |
| `state.isRunning = false` | `setValue (0)` of `Id::isRunning` |
| `state.targetProgram` | the member `targetProgram` |
| `addMessage`: `writer.writeMessage (message);` | the lane write and the counter |
| `onDisconnect` | its first statement is `session.resetCancel ();` |
| the `commands` table | two more entries: `Id::prepare` gives `onPrepare`, and `Id::releaseResources` gives `onReleaseResources` |

`debug::StopManager` gets the member `WHATDBGModel& model { *WHATDBGModel::getInstance() };`, and each `State::getInstance ()->field` of it becomes the Model read or write. The native tests of Part 5 read the Model through `WHATDBGModel::getInstance ()` in place of `State::getInstance ()`. Each native parameter of the type `debug::State*` or `debug::State&` becomes `WHATDBGModel*` or `WHATDBGModel&`: the three functions and the map of `Callbacks.cpp:22-49` and `:272`; `onStopReason`, `onProcessStateStopped`, `onProcessStateExited`, `onProcessStateRunning`, `onProcessEvent`, and the map of `Session_mac.cpp:180`.

### `dap::Reader`

`Reader` loses `requests` and `applyPendingData` (Part 3). It gets the public member `std::function<void (juce::var)> onMessage;`, in the form of `tty->onBytesReady`. The reader thread calls `onMessage (std::move (message))` where it wrote to its lane.

### `Whatdbg`

`class Whatdbg : private juce::ValueTree::Listener`. Message thread, all members. It is main (ruling 11).

Members, in this order:
- `WHATDBGModel model;`
- `Processor processor;`
- `dap::Writer writer;`
- `dap::Reader reader;`
- `jam::Function::Map<juce::Identifier, jam::Function::pointerCapacity, void (const juce::ValueTree&)> events;`
- `std::unique_ptr<jam::Model::ParameterAttachment> numMessagesAttachment;` and `isRunningAttachment`, in the form of `winsizeAttachment` (`EVETerminalProcessor.h:345`)

The members die in the reverse order. So the reader stops before the processor releases the engine, and no thread calls `session.wake ()` or `session.cancel ()` after `shutdown`.

- `Whatdbg ();` It calls `registerEvents ()` and `model.addListener (this)`. It makes the two attachments, on `Id::numMessages` and on `Id::isRunning`, each with the callback `[this] (const juce::var&) { model.flush (); }`. It sets `reader.onMessage = [this] (juce::var message) { processor.addRequest (std::move (message)); };`.
- `~Whatdbg () override;` calls `model.removeListener (this)`.
- `void prepare (const juce::File& sidecarDirectory);` Its one statement is `processor.prepare (sidecarDirectory);`, the form of `EVEProcessor.cpp:87-91`.
- `int run ();` calls `reader.start ()`, then `juce::MessageManager::getInstance()->runDispatchLoop ()`, then returns the value of `Id::adapterExitCode`.
- `void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;` has the body of `end/Source/plugins/Session.cpp:89-93`.
- `void registerEvents ();` adds two entries:
  - `Id::numMessages`: `processor.applyMessages ([this] (const juce::var& message) { writer.writeMessage (message); });`
  - `Id::isRunning`: when `tree.getProperty (Id::isRunning)` is 0, `juce::MessageManager::getInstance()->stopDispatchLoop ();`

`Whatdbg` has no other member: no `state`, no `session`, no atom of the engine, no `commands`, no command body, no drain.

### `Main.cpp`

`runAdapter` (`Main.cpp:238-292`): make `juce::ScopedJuceInitialiser_GUI juceInitialiser;` before `Whatdbg`. Replace lines 261-276 with `Whatdbg whatdbg;`, `whatdbg.prepare (sidecarDir);`, and `exitCode = whatdbg.run ();`.

## Validation Gate

Each step is validated by COUNSELOR before proceeding to the next — against
MANIFESTO.md (BLESSED), NAMES.md, ~/.carol/CODING.md, and the locked PLAN decisions
(no deviation, no scope drift). @Auditor runs ONCE, after the final step, covering
the whole sprint — never per step.

## Steps

### Step 1: Delete
**Scope:** `Source/debug/State.h`; `Source/Whatdbg.h` and `.cpp`; `Source/dap/Reader.h` and `.cpp`; `Source/dap/Types.cpp`.
**Action:**
1. Delete `class State` from `State.h`. Keep the three enums and the alias `Events`.
2. Delete from `Whatdbg`: `run`, `initialize`, `pollTimeoutMs`, `idleSleepMs`, `terminateTimeoutMs`, and the members `state`, `session`, `breakpointManager`, `variableManager`, `stopManager`, and `commands`.
3. Delete from `dap::Reader`: `requests` and `applyPendingData`.
4. Delete `nextSequenceNumber` from `Types.cpp`.

**Validation:** The build fails. The compiler errors are the list of what Steps 2 to 7 change. No error is silenced with a stub.

### Step 2: `WHATDBGModel`
**Scope:** new `Source/WHATDBGModel.h` and `.cpp`; `Source/debug/State.h`; `Source/dap/Types.h` and `.cpp`.
**Action:** Write the class as section `WHATDBGModel` gives it. Change `dap::nextSeq`. Add `dap::getRequest`.
**Validation:** Each row of the parameter table is one `createAndAddParameter` call. The class holds the two tags and no other member. The timer is stopped in the constructor.

### Step 3: `Session::wake`, `cancel`, `resetCancel`
**Scope:** `Source/debug/Session.h`; `Source/debug/Session.cpp`; `Source/debug/Session_mac.cpp`.
**Action:** Write the three members and the macOS broadcaster as the section gives them.
**Validation:** Each body is one statement or none. `pollEvents` is unchanged.

### Step 4: The atoms and the native tests on the Model
**Scope:** `Source/debug/StopManager.h` and `.cpp`; `Source/debug/Callbacks.cpp`; `Source/debug/Session.cpp`; `Source/debug/Session_mac.cpp`.
**Action:** Change each `State::getInstance ()` access to the Model form.
**Validation:** No file of `Source/` names `State::getInstance`. `VariableManager` and `BreakpointManager` do not change.

### Step 5: `Processor`
**Scope:** new `Source/Processor.h` and `.cpp`; `Source/Whatdbg.cpp`.
**Action:**
1. Declare the class as section `Processor` gives it.
2. Move the listed bodies from `Whatdbg.cpp` to `Processor.cpp`, in their current order. Apply the table "The moved bodies change in these ways only", and nothing else.
3. Write `process`, `handleAsyncUpdate`, `prepare`, `releaseResources`, `addRequest`, `applyMessages`, `onPrepare`, `onReleaseResources`, and the destructor.

**Validation:**
- A diff of each moved body against its text after Part 5 shows only the changes of the table.
- No function of `Processor` writes to stdout or touches `model.state`.
- `session` has no caller outside `Processor` and its atoms: an LSP `incomingCalls` on each public member of `Session` is the proof.
- `Processor.cpp` holds no `juce::MessageManager` call and names no lock type. The one framework lock on the context is inside `setValue` (tracker, decision 3).
- `process` has five statements and at most three branches.

### Step 6: `dap::Reader`
**Scope:** `Source/dap/Reader.h` and `.cpp`.
**Action:** Add `onMessage` and call it, as section `dap::Reader` gives it.
**Validation:** `Reader` holds no lane. It includes no header of `debug` and none of `Processor`.

### Step 7: `Whatdbg` and `Main.cpp`
**Scope:** `Source/Whatdbg.h` and `.cpp`; `Source/Main.cpp`.
**Action:** Write `Whatdbg` and change `runAdapter` as sections `Whatdbg` and `Main.cpp` give them.
**Validation:**
- `Whatdbg` reacts to the tree and the property that the event gives (CODING.md, Consume the event payload).
- `Whatdbg` holds no `bool`, no counter, and no copy of a Model value.
- `Whatdbg.cpp` holds no call on `Session` and no `juce::Thread::sleep`.
- No file of `Source/` holds `juce::MessageManager::callAsync` (ruling 9).

### Step 8: Read the end and the failure path
**Scope:** `Source/Processor.cpp`; `Source/Whatdbg.cpp`.
**Action:** Read the two paths against this text.
1. The end. A drain or `onDisconnect` sets `Id::isRunning` to 0 on the context. The attachment posts one message. On the message thread the flush changes the tree, the `Id::isRunning` entry calls `stopDispatchLoop`, `run` returns, and the members of `Whatdbg` die: the attachments, the reader, the writer, the processor. `~Processor` writes the `releaseResources` request and waits. The context shuts the engine down.
2. The failure. `onPrepare` gets `false`. It stores `Id::adapterExitCode` and sets `Id::isRunning` on the context while the message thread waits in `handleUpdateNow`. The attachment posts one message. The message stays in the queue until `runDispatchLoop` runs. The loop then delivers it, and the `Id::isRunning` entry stops the loop from inside it. `run` returns 1.

**Validation:** Each sentence of the two paths has its line in the code. The read is the proof of the failure path: `Session::initialize` returns `debugger.IsValid ()` on macOS (`Session_mac.cpp:258`), and on Windows the sidecar test of `Main.cpp:255` comes before it.

### Step 9: Build and check, macOS
**Scope:** build of whatdbg; the smoke suite; one scout run.
**Action:**
1. ARCHITECT builds Debug. Run the smoke suite of Part 1 three times.
2. ARCHITECT builds Release. Pathfinder runs `scout` on `benchmark/smoke.md` (Part 1 Step 4).

**Validation:**
- The 21 sequence checks pass in each of the three runs. This is the runtime proof of the message loop (tracker, decision 9).
- Scenario `09_terminate_no_zombie` and scenario `10_disconnect_detach` pass: the engine shutdown runs on the context after the reader stops.
- The sprint log holds the rows `epoch`, `user`, and `system` of `benchmark/smoke-report.md` beside the rows of `benchmark/smoke-report-baseline.md`.

### Step 10: Build and check, Windows
**Scope:** a Windows machine.
**Owner:** ARCHITECT builds, runs, and debugs on the Windows machine. This step blocks no macOS step.
**Action:**
1. Build, and run the suite against the Windows baseline.
2. Debug EVE with whatdbg, as in the session of commit `7f0d713`: a breakpoint, a step, an `evaluate` of a name that is not a local, and a terminate.
3. Verify ruling 3 with the debug log. A `terminate` while the target runs: `WaitForEvent` returns `E_PENDING`, and the `terminate` response comes at once. A `disconnect` during an `evaluate`: the `disconnect` response comes, and whatdbg ends.
4. Pathfinder runs `scout` on `benchmark/smoke-windows.md`.

**Validation:** The sequence checks pass. The two checks of item 3 hold. The sprint log holds the rows of `benchmark/smoke-windows-report.md` beside the rows of its baseline.

## BLESSED Alignment

- **B — Bound.** `Whatdbg` owns the Model, the processor, the writer, and the reader. `Processor` owns the atoms and both lanes. The engine is created, used, and shut down on one context, and the reader stops before the shutdown.
- **L — Lean.** `process` is five statements. `Whatdbg` is a listener with two entries. No command body was written again.
- **E — Explicit.** The lifecycle is two tells, `prepare` and `releaseResources`. An engine failure is an exit code in the Model.
- **S — SSOT.** The Model is the one home of each scalar, the sequence number included. `debug::State` does not exist.
- **S — Stateless.** `Whatdbg` holds no state. The native bodies hold none. `Processor` holds one text, `targetProgram` (tracker, decision 1).
- **E — Encapsulation.** Rulings 10 and 11. One way: reader, lane, context, lane and Model, message thread, stdout. The message thread tells the engine through `prepare`, `addRequest`, and `releaseResources` only.
- **D — Deterministic.** One context writes the `messages` lane, so it is the one author of the DAP order. Step 9 proves equal sequences over three runs.
- LANGUAGE.md: no override applies beyond the C++ / JUCE Lean adaptation.

## Risks / Open Questions

None open.

Facts for the review of the whole PLAN:

- **A full `messages` lane loses the message, with an assert** (tracker, decision 8).
- **`ParameterAttachment` holds the last value in a `juce::var`** that the calling thread writes and the message thread passes on (`jam_Model.cpp:223`, `:238`). The callbacks of this part do not read the value.
- **The `requests` lane has three writers in sequence, never two at one time.** `prepare` writes on the message thread before `reader.start ()`. Then only the reader thread writes. `releaseResources` writes on the message thread after the reader stopped.
- **`handleUpdateNow` blocks the caller until the tick ends** (`jam_AsyncUpdater.cpp:62-77`). `prepare` blocks the message thread for the time of `Session::initialize`, one time. `releaseResources` blocks it for the time of `Session::shutdown`, one time.
- **A request named `prepare` or `releaseResources` from a client runs the tell.** No client of the Debug Adapter Protocol sends them.
- **A wake on each request, macOS.** Each wake is one event in the queue of the listener. While the target is stopped they collect, and each later `pollEvents` takes one and returns at once.
- **`Session::shutdown` runs two times.** `onReleaseResources` runs it on the context. `~Session` runs it again on the message thread, where each pointer is already null (tracker, Facts).
- **The doc of `Session` is wrong after this part**: "All methods must be called on the main thread" (`Session.h:63`). The doxygen pass corrects it.
