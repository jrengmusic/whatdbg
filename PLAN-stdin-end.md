# PLAN: stdin-end

**RFC:** none — objective from ARCHITECT prompt: *"if whatdbg has defects must be fixed"*
**Date:** 2026-11-17
**BLESSED Compliance:** verified
**Language Constraints:** C++17 / JUCE + JAM (LANGUAGE.md C++/JUCE: one object, one translation unit; 30 lines per function and 3 branches apply unchanged)

## Overview

whatdbg does not end its run at the end of stdin (Processor.cpp:22-26, :90-93). When the client exits without `disconnect`, the adapter runs on. nvim-dap spawns it `detached = true` (session.lua:1603). PID 26148 showed this. This breaks MANIFESTO **B**: *"No object outlives its owner"* (MANIFESTO.md:45). The plan ends the run at the end of stdin when no terminate is pending. A pending terminate still sends `exited` and `terminated`.

## Context

- Sprint 35 tried "end of stdin sets `isRunning` to 0" (PLAN Step 6.2). It dropped the `terminate` exit events in scenarios 04, 08, 14 and 16. The old behaviour was restored by the ruling *"exact same result at runtime"* (SPRINT-LOG.md:290, ruling quoted at :246). That ruling was the runtime goal of the Sprint 35 transport port.
- Cause of the Sprint 35 loss. Each of those scenarios sends `terminate`, waits only for the response, and then closes stdin (scenario_process.lua:78-80, scenario_runner.lua:13, dap_client.lua:178-181). The exit events come later, through `pollEvents`, while `terminateDeadlineMs` is set (Processor.cpp:570-576, :96-103). An end of stdin that clears `isRunning` at once quits before those events.
- This plan changes the documented contract "the end of stdin does not end the run" (ARCHITECTURE.md:63, :282; Processor.h:14-15, :302-303). It changes runtime behaviour only when stdin ends with no terminate pending.

## Language / Framework Constraints

- One object, one translation unit: the new member body goes to Processor.cpp (CODING.md:1079).
- 30 lines per function: `process` is 29 lines (Processor.cpp:66-94). One added call makes it 30. The new logic lives in its own member.
- Code Hygiene: no comment and no doxygen in the code steps. The doxygen pass runs after the audit.

## Framework Inventory

### 1. API

| Need | Module | API | Source | Step |
|---|---|---|---|---|
| Learn end of stdin | jam_terminal | `jam::StdioServer::onBytesReady` — "the number of bytes read, 0 when none are ready, or -1 at end of stream or on error" | jam_StdioServer.h:93-98; value from the read, jam_StdioServer.cpp:36-39 | 2 |
| -1 on macOS | jam_terminal | `TerminalFIFO::writeFromTerminalTTY (Handle)`, `::read` 0 → -1 | jam_TerminalFIFO_mac.cpp:44-47; jam_TerminalFIFO.h:127-137 | 2 |
| -1 on Windows | jam_terminal | failed `PeekNamedPipe` → -1 | jam_TerminalFIFO_windows.cpp:55-57; jam_AsyncOutputStream_windows.cpp:76-77 | 2 |
| Model row | jam_data_structures | `jam::Model::createAndAddParameter<jam::Parameter<int>>` | jam_Model.h:1033-1056 | 1 |
| Explicit memory order on a parameter | jam_data_structures | `jam::Model::getRawParameterValue` → `getRawValue()`, "Direct access to the underlying atomic for explicit memory-order operations" | jam_Model.h:973-979; jam_ParameterBase.h:182-183 | 2 |
| End the run | jam_data_structures | `jam::Parameter<int>::setValue` on `Id::isRunning` → `ParameterAttachment` → `Whatdbg` quit | jam_ParameterBase.h:167-172; Whatdbg.cpp:8-12, :74-81 | 2 |
| Run the queued frames first | whatdbg | `Processor::applyPendingRequests` | Processor.cpp:132-149 | 2 |
| Pending terminate | whatdbg | `Id::terminateDeadlineMs` != 0 | Processor.cpp:102, :570-576 | 2 |
| Shutdown mode | whatdbg | `Processor::getEndModeForExit` (passive / terminate / detach) | Processor.cpp:383-389; onReleaseResources :402-407 | 2 |
| New identifier | cast | `cast/identifiers.md` row → `Source/generated/Identifiers.h` | identifiers.md:63-70; spell.md:22-23, :125 | 1 |

### 2. Pattern usage

| Pattern | Exemplar | Use sites | Step |
|---|---|---|---|
| SESSION int row with default 0 | `createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::isRunInTerminalSupported, 0);` WHATDBGModel.cpp:24 | WHATDBGModel.cpp:18-28 | 1 |
| Drain member: `applyPendingRequests` first, then a positive check, then the state update | `drainProcessExited` Processor.cpp:337-366 (`applyPendingRequests` at :339) | called at :187, :192 | 2 |
| Drain member called once per pass from `process` | `drainTerminateTimeout ();` Processor.cpp:88 | def :368-381 | 2 |
| `isRunning` ends the run by `setValue (0)` | Processor.cpp:363, :379, :398, :580 | ARCHITECTURE.md:82 | 2 |
| Raw atomic access through the Model | `return &group->get<Parameter<Type>> (id)->getRawValue();` jam_Model.h:976 | only caller in jam XML (classjam_1_1Model.xml:2215) | 2 |
| Readiness callback only notifies, then the engine pass reads | Processor.cpp:22-26 | ARCHITECTURE.md:79 | 2 |

### 3. Names

| Family | Members | Intent | Step |
|---|---|---|---|
| SESSION `is*` rows (`Parameter<int>`, 0/1) | `isRunning` :18, `isConfigurationDone` :20, `isStepPending` :21, `isPausePending` :22, `isBreakInPending` :23, `isRunInTerminalSupported` :24 (WHATDBGModel.cpp) | 0/1 state flag in the Model | 1: `Id::isInputClosed` |
| `cast/identifiers.md` `is*` rows, alphabetical | identifiers.md:63-70 | Identifier vocabulary | 1: row between `isConfigurationDone` (:64) and `isLocalProcess` (:65) |
| Processor `drain*` members | `drainProcessExited` Processor.h:205, `drainTerminateTimeout` Processor.h:211 | One pass check that ends a state and clears `isRunning` | 2: `drainInputClosed` |
| Read-result count | `numRead` in `TerminalFIFO::writeFromDevice` (jam_TerminalFIFO.h:189-193) | Bytes read, or -1 | 2: lambda parameter `numRead` |

Rule -1 check: `isInputClosed` states the flag (Rule 3), joins the `is*` SESSION family (Rule 5), is a Model value (MANIFESTO **S**: state in the Model), and passes Rules 0-8. `drainInputClosed` joins the `drain*` family at Processor.h:205-211 and has the same shape as `drainTerminateTimeout`.

### 4. Synthesis

| Rule | Established form | Exemplar |
|---|---|---|
| "No bail-out guards" / "ALWAYS use nested positive checks" (CODING.md:1057-1058) | positive `if` around the work | Processor.cpp:341-365 |
| "Use C++ alternative tokens" (CODING.md:1060) | `and`, `not` | Processor.cpp:90, :146 |
| "Aggregate (brace) initialization" (CODING.md:1055) | brace init; `int64_t { 0 }` | Processor.cpp:364 |
| "Consume the event payload" (CODING.md:1071) | the handler uses the `int` it receives | Processor.cpp:183-188 (`exitCode`) |
| int64 deadline test | `->getValue () != 0` / `== 0` on `Parameter<int64_t>` | Processor.cpp:102, :370 |

### 5. Absence

| Need | Query | Result |
|---|---|---|
| A reader-side end-of-stream query on `StdioServer`, `AsyncOutputStream`, `TerminalFIFO` | header reads jam_StdioServer.h:26-98, jam_AsyncOutputStream.h:29-76; LSP documentSymbol jam_TerminalFIFO.h | no member reports end of stream; the only channel is the `int` of `onBytesReady` |
| An existing identifier for end of stdin | LSP workspaceSymbol `isInputClosed`, `inputClosed`, `isStdin` | "No symbols found in workspace." |
| An acquire accessor on `Parameter<int>` other than the raw atomic | read jam_ParameterBase.h:138-189 | `getValue` relaxed (:165), `store` relaxed + release on `needsUpdate` (:178-179); only `getRawValue` gives an explicit order (:182-183) |

## Design

1. **Readiness context.** The stdin callback takes `numRead`. When `numRead < 0`, it stores 1 into `Id::isInputClosed` with `std::memory_order_release`, through `getRawParameterValue`. Then it calls `triggerAsyncUpdate` and `session.wake`, as now.
   - Why release and acquire: the ring publish of the last frames comes before the flag store, in the order of the readiness thread. The engine loads the flag with acquire. So `applyPendingRequests` after that load sees every frame that came before the end of stdin. `storeValue` and `getValue` are relaxed (jam_ParameterBase.h:165, :178), so they do not give this order.
2. **Engine context.** `drainInputClosed`, called from `process` after `drainTerminateTimeout`:
   1. Load `Id::isInputClosed` with `std::memory_order_acquire`.
   2. If it is not 0: run `applyPendingRequests`.
   3. Then, if `terminateDeadlineMs` is 0: set `isRunning` to 0 with `setValue`.
   - A pending terminate keeps the run. `drainProcessExited` (:363) or `drainTerminateTimeout` (:379) ends it later and sends the events.
   - A `disconnect` or `terminate` in the queued frames runs in step 2 before the check, through `onDisconnect` (:548-582).
   - Shutdown uses `getEndModeForExit`: detach, unless the client asked to terminate. No new end mode.

## Validation Gate

Each step is validated by COUNSELOR before proceeding to the next — against
MANIFESTO.md (BLESSED), NAMES.md, ~/.carol/CODING.md, and the locked PLAN decisions
(no deviation, no scope drift). @Auditor runs ONCE, after the final step, covering
the whole sprint — never per step.

## Steps

### Step 1: Model row and identifier
**Scope:** `cast/identifiers.md`, `Source/WHATDBGModel.cpp`
**Action:**
1. Add the row `| @id  | isInputClosed                      | `isInputClosed`                      |             |` to `cast/identifiers.md`, between `isConfigurationDone` (:64) and `isLocalProcess` (:65), in the column widths of :63-70.
2. Add `createAndAddParameter<jam::Parameter<int>> (sessionRow, Id::isInputClosed, 0);` to the `WHATDBGModel` constructor, after the `isRunInTerminalSupported` row (:24).
3. Do not edit `Source/generated/Identifiers.h`. The cast run in Step 3 generates it.
**Inventory:** API "Model row", "New identifier"; Pattern "SESSION int row"; Names "SESSION `is*` rows", "`cast/identifiers.md` `is*` rows".
**Validation:** row text and position match the family; constructor line matches WHATDBGModel.cpp:24 form; no other file changed; no comment.

### Step 2: end the run at the end of stdin
**Scope:** `Source/Processor.h`, `Source/Processor.cpp`
**Action:**
1. Processor.cpp:22-26: change the lambda to `[this] (int numRead)`. Before `triggerAsyncUpdate ()`, add a positive check: when `numRead < 0`, call `model.getRawParameterValue<int> (model.sessionTag, Id::isInputClosed)->store (1, std::memory_order_release);`. Read the `getRawParameterValue` declaration first (jam_Model.h, the function around :965-979). Use its exact template signature.
2. Processor.h: declare `void drainInputClosed ();` directly after `drainTerminateTimeout` (:211). No doxygen.
3. Processor.cpp: define `drainInputClosed` directly after `drainTerminateTimeout` (:368-381):
   - outer positive check: `model.getRawParameterValue<int> (model.sessionTag, Id::isInputClosed)->load (std::memory_order_acquire) != 0`
   - inside: `applyPendingRequests ();`, then a positive check `model.getParameter<jam::Parameter<int64_t>> (model.sessionTag, Id::terminateDeadlineMs)->getValue () == 0` around `model.getParameter<jam::Parameter<int>> (model.sessionTag, Id::isRunning)->setValue (0);`
4. Processor.cpp `process`: add `drainInputClosed ();` on the line after `drainTerminateTimeout ();` (:88).
**Inventory:** all API rows of Step 2; Patterns "Drain member …", "`isRunning` ends the run", "Raw atomic access", "Readiness callback only notifies"; Names "Processor `drain*` members", "Read-result count"; Synthesis: all rows.
**Validation:** the names are exactly `numRead`, `drainInputClosed`, `Id::isInputClosed`; memory orders are release (store) and acquire (load); positive nesting, `and`/`not`, no bail-out; `process` is 30 lines or fewer; `drainInputClosed` follows the shape of :337-366; no comment, no doxygen; no other file changed.

### Step 3: build and Windows smoke
**Scope:** build output; `benchmark/smoke-windows-report.md` (scout output)
**Action:** run `scout <absolute path of benchmark/smoke-windows.md>` from the MSVC x64 shell. Its `## toolchain` row is `cast ../cast/spell.md --no-sign` (smoke-windows.md:17-23), which regenerates `Identifiers.h`, builds Release unsigned, and copies the binary to `~/.local/bin/whatdbg.exe` (CMakeLists.txt:463-474). `smoke.cmd` then runs `tests/smoke` (smoke.cmd:7).
**Inventory:** API "New identifier".
**Validation:** build succeeds; `Source/generated/Identifiers.h` declares `isInputClosed`; `tests/smoke` pass=22 fail=0 against `baseline/windows/`, scenarios 04, 08, 14, 16 included.

### Step 4: verify the orphan case
**Scope:** scratchpad scripts only
**Action:** with `nvim --headless --clean -l`, against `~/.local/bin/whatdbg.exe`:
1. Spawn the adapter, send `initialize`, close stdin. The adapter process exits within 3 s.
2. Spawn the adapter, send `initialize`, `launch` of `tests/smoke/build/fixture_wait.exe`, `configurationDone`, close stdin. The adapter exits within 3 s. Record the fixture pid, then stop the fixture with `taskkill /PID`.
3. Re-run the reuse3.lua check (three nvim-dap sessions in a row): each session ends, and no `whatdbg.exe` from the run is left.
**Validation:** each check reports exit within the bound; `tasklist` shows no `whatdbg.exe` started by the run.

### Step 5: documents
**Scope:** `ARCHITECTURE.md`, `SPEC.md`
**Action:**
1. ARCHITECTURE.md:63 and :282: replace "The end of stdin does not end the run" with the new rule: the end of stdin ends the run when no terminate is pending; a pending terminate still sends its `exited` and `terminated` events.
2. ARCHITECTURE.md:79: add that the readiness context stores `Id::isInputClosed` with release order at the end of stdin.
3. ARCHITECTURE.md End list (:279): add `drainInputClosed` to the setters of `Id::isRunning`.
4. ARCHITECTURE.md SESSION table (:293-305): add `| SESSION | `Id::isInputClosed` | int | 0 |`.
5. SPEC.md:34: add the `Id::isInputClosed` store to what the stdin readiness context owns.
**Validation:** each changed sentence matches the code of Steps 1-2 at file:line; STE writing rules.

### Step 6: audit, then the doxygen pass
**Action:**
1. @Auditor sweeps the sprint once.
2. COUNSELOR validates each finding against the cited file:line and a CONTRACT clause. Each valid finding is fixed in this sprint.
3. @Pathfinder runs the doxygen pass: `drainInputClosed` (Processor.h), the constructor doc Processor.h:14-15, the `stdio` member doc Processor.h:300-303. Then it regenerates `docs/xml`.
4. COUNSELOR reads each block against the code.

## BLESSED Alignment

- **B:** the adapter no longer outlives its client. Shutdown keeps the existing `getEndModeForExit` and `onReleaseResources` path.
- **L:** one new member of 8-10 lines; `process` stays at 30 lines or fewer; no new branch chain.
- **E:** the end of stdin is an explicit Model flag, set where the read result arrives. The memory order is explicit.
- **S (SSOT):** the flag lives only in the Model. No `Processor` member mirrors it.
- **S (Stateless):** `Processor` holds no new field.
- **E (Encapsulation):** the readiness context only stores one Model atomic and notifies. The engine context decides. Data flows one way: readiness context → Model → engine context → `isRunning` → message thread.
- **D:** a pending terminate always ends through `drainProcessExited` or `drainTerminateTimeout`. A closed input with no terminate pending always ends the run.

## Risks / Open Questions

None. This plan replaces the Sprint 35 runtime rule "the end of stdin does not end the run" (SPRINT-LOG.md:290). ARCHITECT approved this plan on 2026-11-17; the approval is the ruling.
