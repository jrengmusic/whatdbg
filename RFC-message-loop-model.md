# RFC — whatdbg on a JUCE message loop with jam::Model and registered callbacks
Date: 2026-11-12
Status: open
Author: COUNSELOR
Targets: whatdbg (`Source/Main.cpp`, `Source/Whatdbg.cpp`, `Source/debug/State.h`, `Source/debug/Callbacks.cpp`, `Source/debug/Session_mac.cpp`)

## Problem Statement

ARCHITECT asked for this RFC as a future consideration for a plan:

> "i was wondering if whatdbg would be more deterministic if it was re-design from scratch using jam::Model and listener with registrable callbacks just like other more heavy event driven jam project like eve, instead of manually handling events and callback diverge in each platform"

> "so whatdbg cant use message thread nor juce event loop?"

> "we could still design hot path lock free if we need hard deadline no, other thread call could either synchronous or async with scoped lock"

> "write RFC on whatdbg repo as future consideration to plan"

The questions for the plan are these:

1. Can whatdbg run on a JUCE message loop?
2. Can its session state move into a `jam::Model` with listeners and registered callbacks?
3. Which thread owns the debugger engine in that design?

## Research Summary

### Current structure of whatdbg (read this session)

1. whatdbg is a plain console program. `int main` calls `runAdapter ()` (`Source/Main.cpp:294-308`).
2. `runAdapter` constructs `Whatdbg` and calls `whatdbg.run ()` on the main thread (`Source/Main.cpp:261-269`).
3. `Whatdbg::run` is one hand-written loop (`Source/Whatdbg.cpp:64-85`). Each pass does these steps in order:
   1. Drain the DAP request queue (`reader.tryPop`, `:67-68`).
   2. Poll the engine with `session.pollEvents (pollTimeoutMs)` while the state is `running` or `launching` (`:71-75`).
   3. Call `processDeferredEvents ()` (`:78`).
   4. Sleep `idleSleepMs` while the state is `idle` (`:81-84`).
4. A search of `Source` found no `MessageManager`, `runDispatchLoop`, `ScopedJuceInitialiser` or `JUCEApplication`. whatdbg does not start the JUCE message loop.
5. DAP requests already dispatch through a lookup table of callbacks, for example `{ "evaluate", [this] (const juce::var& m) { onEvaluate (m); } }` (`Source/Whatdbg.cpp:40-44`).
6. `State` is the session state. It is a `jam::Instance<State>` (`Source/debug/State.h:60`). Its doc says: "COM callbacks (EventCallbacks) write deferred-event flags; the Whatdbg main loop reads and clears them in processDeferredEvents(). All access must occur on the main thread unless a field is explicitly noted otherwise." (`State.h:53-55`).
7. `State` still holds manual flags written by platform code and read by the loop:
   - `isRunning` (`State.h:120`)
   - `shouldTerminateOnExit` (`:126`)
   - `isConfigurationDone` (`:132`)
   - `isStepPending` (`:138`)
   - `isPausePending` (`:146`)
   - `hasBreakpointLocationsResolved` (`:170`)
   - `hasProcessExited` (`:191`)
8. `stopReason` (`State.h:75`, enum at `:21-30`) has writers in two platform files. A count search for `stopReason` gives 7 matches in `Source/debug/Callbacks.cpp` (Windows) and 12 in `Source/debug/Session_mac.cpp` (macOS). This is the platform divergence that ARCHITECT names.

### Defects found this session that come from the current structure

1. The stop-state flags drifted apart. These flags were deleted: `hasBreakpointHit`, `hasExceptionStopped`, `hasStepCompleted`, `hasPauseCompleted`, `hasNewModuleLoaded`, `hasPendingBreakpoints` and `InitialBreakPhase`. The single `StopReason` field replaced them. Three bugs came from them:
   - Engine breakpoint ID 0 was read as "not set".
   - `drainModuleLoaded` resumed stops that it did not own.
   - `drainInitialBreak` resumed a later breakpoint stop.
2. Engine state was changed outside its owner. The deleted `evaluateStringValue` called `SetExpressionSyntax (DEBUG_EXPR_CPLUSPLUS)` (old `SessionInspection.cpp:229`). Microsoft documents: "The expression syntax is a global setting within the engine, so setting the expression syntax will affect all clients." (learn.microsoft.com, `nf-dbgeng-idebugcontrol3-setexpressionsyntax`). After this call, the text command `.symopt- 100` read `100` as decimal. The run log shows the symbol options `0x30237` at stop 1 and `0x30213` at stop 2.
3. One blocking engine call stops the whole adapter. Run on EVE (trace spec `eve-locals-probe.json`, Debug whatdbg):
   - The evaluate of `shell` in frame `juce::File::parseAbsolutePath`, where `shell` is not a local, took 10.3 s. Measured: `execute begin t=30059184`, error banner at `t=30066038`, `Couldn't resolve error at 'shell'` at `t=30069498` (scratchpad `whatdbg-symdebug.log:224-257`).
   - After the change to `IDebugSymbolGroup2::AddSymbol` (`SessionInspection.cpp:190-209`), the same evaluate got no reply before the client disconnect (whatdbg log:186-187).
   - During the call, the single loop handles no request, the disconnect included.

### jam::Model (read this session)

1. `jam::Model` is "APVTS-analog state owner, owns the juce::ValueTree by value" (`jam/jam_data_structures/model/jam_Model.h:3`).
2. It derives from `juce::Timer` and `juce::ValueTree::Listener` (`jam_Model.h:38-41`).
3. Thread ownership, quoted (`jam_Model.h:13-16`):
   - "Parameter store()/getValue() and Model::storeValue(): any thread, lock-free"
   - "flush(), timerCallback(), get(): MESSAGE THREAD only"
   - "createAndAddParameter(), ParameterAttachment ctor/dtor: MESSAGE THREAD, construction/destruction only"
4. A flush timer moves values to the tree: "first tick at 10 Hz, then 120 Hz while a tick flushed a parameter and 60 Hz otherwise" (`jam_Model.h:8`).
5. `ParameterAttachment` is a "listener bridge delivering parameter changes to a callback on the message thread" (`jam_Model.h:10`).
6. EVE uses this pattern:
   - `EVETerminalProcessor` owns a terminal `jam::Model` (`eve/Source/EVETerminalProcessor.h:15`, member at `:342`), with `jam::Model::ParameterAttachment` members (`:345`).
   - `EVEView::registerTerminalEvents` dispatches the Model changes (`eve/Source/EVEView.h:396`).

### DbgEng threading (Librarian, learn.microsoft.com)

1. "In general, a client's methods may be called only from the thread in which the client was created. Typically, methods called from the wrong thread will fail immediately. The notable exception to this rule is the method CreateClient; this method may be called from any thread, and returns a new client that can be used in the thread from which it was called." (`using-client-objects`)
2. "The engine uses this same thread whenever it makes a call to a callback instance registered with the client. If the thread is in use, the engine will queue the calls it needs to make." (`using-callback-objects`)
3. `WaitForEvent`: "The method can be called only from the thread that started the debugger session." and "This method is not re-entrant." (`nf-dbgeng-idebugcontrol-waitforevent`)
4. `SetInterrupt`: "This method can be called at any time and from any thread." With `DEBUG_INTERRUPT_EXIT`: "If there is currently a WaitForEvent call running, the engine will force it to return." (`nf-dbgeng-idebugcontrol-setinterrupt`)
5. `ExitDispatch`: "This method is reentrant and may be called from any thread." (`nf-dbgeng-idebugclient-exitdispatch`)
6. Not documented: whether calls on other clients can run while `WaitForEvent` waits. Only a runtime test can show this.

### LLDB threading (Librarian; no official rule)

1. The SB headers and the Python reference state no thread rule for `SBListener::WaitForEvent` or `SBFrame::EvaluateExpression`.
2. Greg Clayton, lldb-dev 2019: "The API is thread safe, but you can run into interesting issues if you don't thread things right." and "You don't want one thread stepping and another thread resuming." (mail-archive msg07378)
3. Jim Ingham, lldb-dev 2019: "The main problem with trying to run a debug session multithreaded is coordinating execution control." (mail-archive msg07226)
4. LLVM Discourse RFC 2025: individual SB calls "are protected by a mutex, but lack atomicity as a whole." (`rfc-exposing-the-target-api-lock-through-the-sb-api/85215`)
5. These are developer statements. They are not normative documentation.

## Principles and Rationale

1. **S (Stateless) and E (Encapsulation).** MANIFESTO lists "Manual boolean to track subordinate state" as an anti-pattern. The flags in `State.h:120-191` are that pattern. A `jam::Model` with listeners puts each state in one owner. Listeners then react to the change, so no loop reads and clears flags.
2. **S (SSOT).** Each platform writes `stopReason` from its own callback code (`Callbacks.cpp`, `Session_mac.cpp`). One Model and one set of registered callbacks give both platforms one transition path. The platform code then only reports engine events.
3. **E (Explicit), "On established patterns".** MANIFESTO says: "If the architecture uses listeners, use listeners." JAM has the Model and listener pattern (`jam_Model.h`), and EVE uses it (`EVETerminalProcessor.h:342`). whatdbg is a JAM project, but it has its own loop and flags.
4. **B (Bound), engine ownership.** DbgEng binds a client to its creating thread (DbgEng item 1). So one thread owns every engine call. The design must name that thread.
5. **Deadline threads.** MANIFESTO: "A thread with a hard deadline, such as the audio thread or a clock thread, does not allocate, lock, wait, or make a blocking call." whatdbg has no hard-deadline thread in the code read: the loop polls with a timeout and sleeps when idle (`Whatdbg.cpp:71-84`). ARCHITECT: "we could still design hot path lock free if we need hard deadline no, other thread call could either synchronous or async with scoped lock". A scoped lock or a synchronous call between whatdbg threads therefore does not violate the contract.
6. **D (Deterministic).** Defect 3 shows that one blocking engine call stops all request handling. `SetInterrupt` is callable from any thread (DbgEng item 4). A design with a second thread can interrupt a blocked call. The single loop cannot do this, because the loop is the blocked thread.

### Considered and rejected

1. **Keep the loop and add more flags.** Rejected. This repeats the "Manual boolean to track subordinate state" anti-pattern that produced defect 1.
2. **Call DbgEng from more than one thread without an owner.** Rejected. DbgEng item 1: "methods called from the wrong thread will fail immediately."

### Candidate thread designs (no recommendation; ARCHITECT selects)

1. **The message thread owns the session.**
   - The message thread creates the client.
   - A `juce::Timer` calls `WaitForEvent` with a short timeout.
   - The `jam::Model` and the engine share one thread, so no handoff is needed.
   - Each wait blocks the message thread for its timeout.
   - Basis: DbgEng items 1 to 3, and `jam_Model.h:15`.
2. **A dedicated debugger thread owns the session.**
   - The debugger thread makes every engine call and runs `WaitForEvent`.
   - The message thread owns the `jam::Model`.
   - Engine results reach the Model through `Parameter` stores, "any thread, lock-free" (`jam_Model.h:14`).
   - Requests reach the engine through a synchronous call or an async call with a scoped lock (ARCHITECT, quoted above).
   - `SetInterrupt` from the message thread can break a blocked call.
   - Basis: DbgEng items 1 and 4, and `jam_Model.h:14`.

## Scaffold

None.

## BLESSED Compliance Checklist
- [ ] Bounds  - [ ] Lean  - [ ] Explicit  - [ ] SSOT
- [ ] Stateless  - [ ] Encapsulation  - [ ] Deterministic

Each box stays open until a PLAN names the design. The Principles section maps each pillar to the current defects.

## Open Questions

1. **Thread owner of the engine.** Answered. ARCHITECT: "crossplatform. lldb, dbgeng should be at its own hot path lock free path this is isomporhic to audio thread, which how we design eve, end with audio architecture discipline, the traffic orchestrator state machine with jam::Model is at message thread". This selects candidate design 2 on both platforms. The engine thread follows the deadline-thread discipline of an audio thread. The jam::Model state machine is on the message thread.
2. **State in the Model.** Which fields of `State.h` become Model values: `executionState`, `stopReason`, exception data, breakpoint resolution, exit code? Which fields stay transient? ARCHITECT settles it. COUNSELOR presents the field list from `State.h` at plan time.
3. **Event latency.** Answered. The Model flush timer runs at 10, 60 or 120 Hz (`jam_Model.h:8`). ARCHITECT selected "Send at once": DAP `stopped` and `output` events do not wait for a flush tick.
4. **Synchronous or async per request.** Which DAP requests wait for the engine thread, and which are queued? ARCHITECT: "other thread call could either synchronous or async with scoped lock". The per-request split is ARCHITECT's.
5. **Cancel of a blocked engine call.** Answered. ARCHITECT selected "Yes, interrupt": a DAP `disconnect` or a client timeout calls `SetInterrupt` (DbgEng item 4), so a blocked engine call returns.
6. **Calls during `WaitForEvent`.** It is not documented whether calls on other clients can run while `WaitForEvent` waits (DbgEng item 6). A runtime test settles it before candidate design 2 is planned. COUNSELOR runs the test on ARCHITECT's go.
7. **macOS threading.** Answered by question 1: "crossplatform. lldb, dbgeng should be at its own hot path". The macOS session uses the same owner-thread design as Windows. Refer to question 10 for the open detail.
8. **New names.** Any new type, thread or callback name is a new domain term (NAMES.md Rule -1). ARCHITECT approves it.
9. **Scope relative to the open evaluate defect.** Answered. ARCHITECT selected "Fix now": the evaluate hang (defect 3) is fixed in the current structure, before this redesign.
10. **Engine thread discipline on macOS.** ARCHITECT's answer to question 1 puts lldb on the same lock-free engine thread as dbgeng. LLDB has no official thread rule (LLDB items 1 to 5). The plan must state how the macOS session meets "hot path lock free". ARCHITECT settles it at plan time.

## Handoff Notes

1. This RFC is a future consideration. ARCHITECT: "write RFC on whatdbg repo as future consideration to plan". No plan exists yet.
2. The current sprint already replaced the stop flags with `StopReason` (`State.h:21-30`, `:75`). It also replaced the Windows console-text evaluate with the frame scope symbol group (`SessionInspection.cpp:190-209`).
3. The evaluate defect is open at the time of writing. A Debug build with temporary `ODE:` timing logs around `AddSymbol` exists (`whatdbg.exe` 2026-11-12 08:20:18). Its probe is on hold.
4. The macOS sibling evaluates with `frame.EvaluateExpression` (`SessionInspection_mac.cpp:162-184`). ARCHITECT.md states the goal of whatdbg as "one tool, identical DX" on both platforms.
5. The earlier RFC in this repo, `RFC.md` (terminal attachment), is a separate objective. This RFC does not replace it.
