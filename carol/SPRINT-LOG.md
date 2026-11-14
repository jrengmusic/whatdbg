# SPRINT-LOG.md
## whatdbg — Windows Host Abstraction Translator for dbgeng

**Project:** whatdbg  
**Repository:** https://github.com/jrengmusic/whatdbg  
**Started:** 2026-03-15

**Purpose:** Long-term context memory across sessions. Tracks completed work, technical debt, and unresolved issues. Written by PRIMARY agents only when ARCHITECT explicitly requests.

---

## 📖 Notation Reference

**[N]** = Sprint Number (e.g., `1`, `2`, `3`...)

**Sprint:** A discrete unit of work completed by one or more agents, ending with ARCHITECT approval ("done", "good", "commit")

---

## ⚠️ CRITICAL RULES

**AGENTS BUILD CODE FOR ARCHITECT TO TEST**
- Agents build/modify code ONLY when ARCHITECT explicitly requests
- ARCHITECT tests and provides feedback
- Agents wait for ARCHITECT approval before proceeding

**AGENTS NEVER RUN GIT COMMANDS**
- Write code changes without running git commands
- Agent runs git ONLY when user explicitly requests
- Never autonomous git operations
- **When committing:** Always stage ALL changes with `git add -A` before commit
  - ❌ DON'T selectively stage files (agents forget/miss files)
  - ✅ DO `git add -A` to capture every modified file

**SPRINT-LOG WRITTEN BY PRIMARY AGENTS ONLY**
- **COUNSELOR** or **SURGEON** write to SPRINT-LOG
- Only when user explicitly says: `"log sprint"`
- No intermediate summary files
- No automatic logging after every task
- Latest sprint at top, keep last 5 entries

**NAMING RULE (CODE VOCABULARY)**
- All identifiers must obey project-specific naming conventions (see NAMING-CONVENTION.md)
- Variable names: semantic + precise (not `temp`, `data`, `x`)
- Function names: verb-noun pattern (initRepository, detectCanonBranch)
- Struct fields: domain-specific terminology (not generic `value`, `item`, `entry`)
- Type names: PascalCase, clear intent (CanonBranchConfig, not BranchData)

**BEFORE CODING: ALWAYS SEARCH EXISTING PATTERNS**
- ❌ NEVER invent new states, enums, or utility functions without checking if they exist
- ✅ Always grep/search the codebase first for existing patterns
- ✅ Check types, constants, and error handling patterns before creating new ones
- **Methodology:** Read → Understand → Find SSOT → Use existing pattern

**TRUST THE LIBRARY, DON'T REINVENT**
- ❌ NEVER create custom helpers for things the library/framework already does
- ✅ Trust the library/framework - it's battle-tested

**FAIL-FAST RULE (CRITICAL)**
- ❌ NEVER silently ignore errors (no error suppression)
- ❌ NEVER use fallback values that mask failures
- ❌ NEVER return empty strings/zero values when operations fail
- ❌ NEVER use early returns
- ✅ ALWAYS check error returns explicitly
- ✅ ALWAYS return errors to caller or log + fail fast

**⚠️ NEVER REMOVE THESE RULES**
- Rules at top of SPRINT-LOG.md are immutable
- If rules need update: ADD new rules, don't erase old ones

---

## Quick Reference

### For Agents

**When user says:** `"log sprint"`

1. **Check:** Did I (PRIMARY agent) complete work this session?
2. **If YES:** Write sprint block to SPRINT-LOG.md (latest first)
3. **Include:** Files modified, changes made, alignment check, technical debt

### For User

**Activate PRIMARY:**
```
"@CAROL.md COUNSELOR: Rock 'n Roll"
"@CAROL.md SURGEON: Rock 'n Roll"
```

**Log completed work:**
```
"log sprint"
```

**Invoke subagent:**
```
"@oracle analyze this"
"@engineer scaffold that"
"@auditor verify this"
```

**Available Agents:**
- **PRIMARY:** COUNSELOR (domain specific strategic analysis), SURGEON (surgical precision problem solving)
- **Subagents:** Pathfinder, Oracle, Engineer, Auditor, Machinist, Librarian

---

<!-- SPRINT HISTORY STARTS BELOW -->
<!-- Latest sprint at top, oldest at bottom -->
<!-- Keep last 5 sprints, rotate older to git history -->

## SPRINT HISTORY

## Sprint 35: Windows Conformance — Audit Resolution, jam::AsyncOutputStream, runInTerminal ✅

**Date:** 2026-11-15
**Duration:** one session, continued across context compactions
**Scope:** `PLAN-windows-conformance-audit.md` Steps 0-20. ARCHITECT: *"OBJECTIVE: @PLAN-audio-architecture-discipline-0-tracker.md windows conformance"*; *"always build with cast, profile with scout"*; *"only /audit wholesale when plan completed and the implementation achieve the accepted criteria."*; *"FULLY EVENT DRIVEN, no wait, no lock at hot path, ISOMORPHIC to mac"*; *"you work on windows, you finish on windows"* (macOS item M1 dropped); /log *"when you're done"*.

### Agents Participated
- COUNSELOR: plan lock, every brief, validation of each return by read, the scenario 22 and step re-hit diagnoses, the audit filter, the documents, this log.
- Pathfinder (haiku): discovery reads, smoke and probe runs, scout runs, the doxygen pass and its follow-up.
- Engineer (sonnet): Steps 1-18 code, the audit fix batches A-E and J, the `kind` → `type` rename, the cast builds, the log rotation.
- Auditor (opus): one wholesale audit — 63 findings, NEEDS_WORK.

### Files Modified
- jam: `jam_core/native/jam_AsyncOutputStream.h` and its Windows and macOS bodies (new) — pipe or FIFO reader, overlapped I/O + thread pool wait on Windows, GCD read source on macOS; `jam_ScopedComInitialiser_windows.h/.cpp` (renamed from `jam_ComApartment_windows.*`); `jam_core.h`, `jam_core.cpp`, `user-modules-info.md`; `jam_terminal/tty/native/jam_TerminalTTY_windows.h/.cpp` reads through `jam::AsyncOutputStream`; `jam_TerminalImageSequence_windows.cpp` (`ComSmartPtr`, new class name). END `Source/plugins/Nexus.cpp` call site.
- `Source/Launcher.h/.cpp` (new) — launcher mode for `runInTerminal`: pid message, didAttach reply, `CREATE_SUSPENDED` child on Windows, `execvp` on macOS. `Source/Main.cpp` — `runLauncher` dispatch on `--comm-file` / `--launch-target`.
- `Source/debug/Session.h`, `Session.cpp` — `jam::ScopedComInitialiser` (`std::optional`, emplaced in `initialize`), `juce::ComSmartPtr`, stdout/stderr through `jam::AsyncOutputStream`, owned process handle from `createSuspendedProcess`, comm channel `createPipe`, wake on every request, `disableBreakpointAtInstruction` before resume and step, `GetSourceEntriesByLine` resolution, native path form in Session, `srv*` symbol path. `Session_mac.cpp` — the same API on macOS (not compiled here).
- `Source/Processor.h/.cpp` — `deferrals` table and `pendingRequests` lane (deferred break-in for `setBreakpoints`), `runInTerminal` reverse request and response routing, `addLaunchErrorResponse`, `isPolling`.
- `Source/debug/StopManager.*`, `Callbacks.*`, `BreakpointManager.*`, `State.h`, `Source/dap/FIFO.h`, `Source/dap/Types.*`, `Source/WHATDBGModel.cpp`, `cast/identifiers.md`, `Source/generated/*`.
- `tests/smoke/` — `dap_client.lua` (runInTerminal client, `getLaunchArguments`), `scenario_terminal.lua` (new, `22_run_in_terminal`), `run_smoke.lua`, `scenario_terminate.lua`, `baseline/windows/` re-recorded. `benchmark/smoke-windows.md` (epochs 11).
- Documents: `PLAN-audio-architecture-discipline-0-tracker.md` (D8-D15, Decisions 5 and 6, thread table, Status), `PLAN-audio-architecture-discipline-1-parity.md` (Part 1 Step 4), `SPEC.md`, `CLAUDE.md`, `Source/HELP.md`, `README.md`, `RFC.md` (Implemented), `PLAN-windows-conformance-audit.md` (new). Doxygen blocks in the headers named under Audit.

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied
- [ ] The macOS code of this sprint (`Session_mac.cpp`, `Launcher.cpp` macOS branch, `jam::AsyncOutputStream` macOS body) is not compiled on this Windows host.

### Problems Solved
- Scenario 22 failed: `Launcher` read `kind` with `getView` where the value needs `getString` (`jam_JsonDocument.h:207-215`). Fixed.
- `Launcher` resumed the target with `ResumeThread`. The engine resumes it at attach (`DEBUG_ATTACH_INVASIVE_RESUME_PROCESS`, `DbgEng.h:1868-1870`), so the call is removed.
- Steps hit the same breakpoint again (scenario 03). `Session::disableBreakpointAtInstruction` runs before resume, stepOver, stepInto and stepOut. `applySteppedBreakpoint` issues GO only when `stopReason` is none and no step is pending. Probe: 0/80, then 0/40.
- `setBreakpoints` while the target runs: silent break-in, deferred requests applied, resume. No wait in a request body.
- Audit: 63 findings. Resolved: W6-W9, W12/13, W15-W17, W19, W21, P6, P7, S2, T1, L1, L3, L5-L7, M2-M4, X1-X3, J1, J3-J6, J8, J9, J14, J16, J17, D1-D12, and the `kind` names (NAMES Rule 6). Rejected, each by a read or a ruling: W1/D3 (wake ruling), W2, W3-W5, W10, W11, W14, W18, W20, W22, P1-P5, P8-P10, L2, L4, J2, J7, J10, J12, J13, M5, S1, B1, N2, X4, W24/P12/I1. M1 dropped by ARCHITECT.

### Verification
- Final `cast cast/spell.md --debug`: exit 0. `tests/smoke`: 22/22 against `baseline/windows/`. Step re-hit probe: 0/40.
- scout (`benchmark/smoke-windows.md`, 11 epochs): epoch median 45.30 s against the 77.43 s baseline (−41.5%); user 1.30 s against 1.63 s; system 1.39 s against 2.02 s.

### State for Continuation
- The macOS code of this sprint is not compiled. Build on macOS and run `tests/smoke` before relying on it.
- Scenario `22_run_in_terminal` has no macOS baseline. Record it with `--record` on macOS.
- The epoch median is 45.30 s. The earlier measurement of this sprint was 29.06 s. The cause of the rise is not investigated.
- `docs/xml` is not regenerated after the doxygen pass. JAM `<leader>bd` is ARCHITECT's.
- The clangd index is stale for the Session files. LSP results on them are not reliable until `compile_commands.json` is regenerated.
- END is not built this sprint. The `jam::ScopedComInitialiser` call site in `Nexus.cpp` compiles on ARCHITECT's build of END.

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 34: Audio Architecture Discipline — Model, Engine Context, Message Loop ✅

**Date:** 2026-11-14
**Duration:** one session, continued across a context compaction
**Scope:** `PLAN-audio-architecture-discipline-{0..7}.md`. ARCHITECT: *"if i give you permission to build can you complete the whole plan?"*; *"just implement whatever you can implement on windows ... implement dont defer. only run audit wholesale, when the whole plan completed. PLAN locked, ARCHITECT ratifies the number and the policy at the lock. DESIGN BY CONTRACT /go no gate"*; *"always build using cast"*; *"backup current binary. then build release install. proceed"*; *"fold into your task reset whatdbg, cast, ggwp, scout version to 0.0.1, and rebuild all ... then proceed your task to completion"*.

### Agents Participated
- COUNSELOR: plan execution, every brief, validation of each return by read, the terminate and message-loop diagnoses, ARCHITECTURE.md / SPEC.md / CLAUDE.md / tracker, the audit filter, this log.
- Pathfinder (haiku): smoke runs (baseline, Parts 2-5, Part 6, ten final runs, post-audit), scout runs, terminate and attach probes, doxygen XML regeneration, the post-audit doxygen pass.
- Engineer (sonnet): Parts 2-6 code, the `JUCEApplicationBase` correction, the audit fixes, every cast build, the cast restore.
- Auditor (opus): one wholesale audit — 43 findings.

### Files Modified
- `Source/WHATDBGModel.h`, `.cpp` (new) — `jam::Model` with SESSION and EXCEPTION rows, 17 parameters; `debug::State` class deleted (`State.h` keeps the enums, `Events`, `getExceptionName`).
- `Source/Processor.h`, `.cpp` (new) — `private jam::AsyncUpdater` engine context: `requests` (`dap::FIFO<jam::JsonDocument>`, 64) and `messages` (`dap::FIFO<std::string>`, 4096) lanes, `commands` and `cancellations` tables, `prepare`/`releaseResources` tells, `process`, the eighteen command bodies moved from `Whatdbg`.
- `Source/Whatdbg.h`, `.cpp` — rewritten: `juce::JUCEApplicationBase` + `juce::ValueTree::Listener`; two `events` entries (`numMessages` drains the lane to `dap::Writer`, `isRunning` sets the return value and quits).
- `Source/Main.cpp` — `--help` through `jam::TerminalPage`; `runAdapter` runs `JUCE_CREATE_APPLICATION_DEFINE (Whatdbg)` + `JUCEApplicationBase::main` with the sidecar path.
- `Source/dap/Reader.*` (`onMessage`, no lane), `Writer.*` (new), `FIFO.h` (new), `Types.*` (`Types.cpp` new; `getRequest`, `nextSeq` on the Model, event builders).
- `Source/debug/VariableManager.*`, `StopManager.*` (new atoms); `Session.h`, `Session.cpp`, `Session_mac.cpp` (`wake`, `cancel`, `resetCancel`, macOS broadcaster, `appendStackFrames (…, addFrame)`), `Callbacks.*` (report through `Session::events`), `BreakpointManager.*`.
- `Source/HELP.md` (new), `cast/files.md` (new), `cast/identifiers.md`, `cast/spell.md`, `project-info.md` (modules, help rows, version 0.0.1).
- `tests/smoke/` — 21 scenarios, transcript baselines, `scenario_inspect.lua`, `fixture_module.cpp`, `fixture_load.cpp`, `build.sh`; `benchmark/` scout manifests and reports.
- jam: `jam_tui/` (new module, `jam::TerminalPage`), `user-modules-info.md`, `cast/spell.md`. scout, ggwp, cast: help through `jam::TerminalPage`; Help.* and style.css deleted; version 0.0.1.
- `ARCHITECTURE.md` (v0.7.0), `SPEC.md`, `CLAUDE.md`, `PLAN-…-0-tracker.md` (Status), `docs/xml` (regenerated; stale `classdebug_1_1_state.xml` removed).

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied
- [ ] Windows code (`Session.cpp`, `Callbacks.cpp`, `Loader.cpp`) is not compiled on this macOS host.
- [ ] Lean 300: `Processor.cpp` is about 670 lines — the eighteen command bodies, placed there by PLAN Part 6.

### Problems Solved
- The message loop returned at once on macOS: `runDispatchLoop` is `[NSApp run]` (`juce_MessageManager_mac.mm:323-351`), and only `JUCEApplicationBase::main` creates `NSApp` (`juce_ApplicationBase.cpp:235-258`). `Whatdbg` became a `JUCEApplicationBase` (sibling `ling/Source/Main.cpp:276-486`).
- After a terminate whose kill the engine reported as a signal stop, the terminate deadline was never checked, and whatdbg stayed alive: `process` polls while a deadline is pending (statements 2 and 5).
- Scenario 02 was not deterministic: the test harness killed the attached target while whatdbg killed it (two owners). The harness now waits for `terminated` before its cleanup kill.
- The frame id was computed in two places: `VariableManager` is the one assigner through `addFrame`.

### Deliberate differences beyond D1-D7
- D8: a terminate of a target that the engine first reports as stopped by the signal now sends `stopped` (exception), then `exited` and `terminated`; before, the transcript ended at `stopped` and whatdbg exited by its 5 s deadline or by the client. Baselines 04, 08, 09, 14, 16 updated. Basis: the zombie fix intent, "the main loop's normal pollEvents path observes the debuggee's exit" (ARCHITECTURE v0.6.0:378-382).
- D9: attach + terminate ends `exited`, `terminated` (5 of 5 probe runs without the second killer). Baseline 02 updated.

### Verification
- macOS smoke: ten runs, all 21 sequence checks pass in each; two more after the audit fixes. Scenario `17_evaluate_fail` fails its two evaluate checks in every run, as before the rewrite (`evaluate notAVariable` gives `success=true`).
- scout with the baseline column (after the scout and cast repairs, whatdbg Release built by the new cast, 7 epochs): `epoch` 4.5390e10 ns, change −27.6%, probability 0.75; `user` 2.3587e9, change +8.7%, probability 0.0005; `system` 1.6543e9, change +5.8%, probability 0.0007 (`benchmark/smoke-report.md`, 14-11-2026 16:14).
- scout, Release, 7 epochs, against the Part 1 baseline (11 epochs), read side by side before the repairs: `epoch` 4.5387e10 ns (±0.12%, min 4.5296e10, max 4.6253e10) vs 6.2698e10 (±0.327%, min 6.1858e10, max 6.5898e10); `user` 2.376e9 (±0.49%, min 2.333e9, max 2.414e9) vs 2.169e9 (±2.07%, min 1.745e9, max 2.270e9); `system` 1.643e9 (±0.84%, min 1.621e9, max 1.665e9) vs 1.563e9 (±1.58%, min 1.335e9, max 1.619e9).
- T1 (18 commands) and T4 (drains) of Part 1 read against `Processor.cpp` and `StopManager.cpp`: match, except D2 (drains run on the next tick) and D8.

### Audit
- Resolved: #3 (reader starts only after a good `prepare`), #5 (`onReleaseResources` clears the deadline), #8 (`parse (std::string&&)`), #9 (`addMessage` moves), #10 (`TerminalPage` moves the fetchers), #13 (one frame-id assigner), #21 (`inline` removed), #22 (`"Locals"` literal per Part 3:81), #26 (tracker Status), #27 (redundant includes), #28 (unnamed parameter), #29 (HELP versions 0.0.1), #30-38 (doxygen pass, 0 warnings), #40-43 (documents).
- Rejected, each by its read: #1 (the Part 1 baselines already end with `stopped exception`), #2/#12/#15/#17 (ARCHITECT's 62b81cb forms, moved verbatim), #4/#6/#7/#14/#16/#18 (PLAN-ratified, or closed by #5), #11 (twice, the clause says "more than twice"), #19 (the page destructor tripped `jam_TextEditor.cpp:1397`), #20 (templates stay in the header), #23 (`execv` does not search PATH, `Main.cpp:156`), #24 (fact for ARCHITECT), #25 (build needs the include rows), #39 (log text, no clause).

### State for Continuation
- Windows: build, the smoke suite against the Windows baseline, the EVE session, and the scout Windows run (Parts 1-7 Windows steps) are ARCHITECT's.
- Part 7 Step 4 (field debugging of a JAM project) is ARCHITECT's.
- `user` and `system` CPU time rose 9.5% and 5.1% against the baseline; `epoch` fell 27.6%.

### cast and scout repaired (ARCHITECT: *"don't leave cast and scout at broken state, why crash? fix it"*)
- cast crashed before main: its generated namespace-scope comment tables copied `Id::comment` during static initialisation, and since cast commit 106685c that identifier is defined in jam's TU (`jam/generated/jam_Core.cpp:50`); cross-TU dynamic initialisation order is unspecified. ARCHITECT chose *"First-use shape in jam"*. jam `cast/code.cast:150-157` adds the `shared-hash-map` shape (a struct over `jam::HashMap` whose constructor holds the entries); cast's spell emits `map::ClangComment` … `map::SqlComment` and `map::CommentSyntax` through it into `HashMaps.h`; each read site holds `static const jam::SharedInstance<map::CommentSyntax> commentSyntax { std::in_place };` (the form of `jam_Format.cpp:850`). New cast: `--help` exit 0, rebuilds itself with no change, builds whatdbg.
- scout could not read back its own report: an empty samples file left the table type empty (`Benchmark.h:193`), and `jam::Format::isNumber` rejected the exponent form that `juce::String (double)` writes (`jam_Format.cpp:828-846`). Fixed: the type defaults to `Id::epoch`; `Format::isNumber` accepts one exponent after today's mantissa rule (`isMantissa`); `runEpochs` validates the baseline with `jam::ConfigDocument::isValid` and fails with a diagnostic; `ProfileReport::getProfile` looks baseline zones up by `jam::Format::toType` (the typing of `ConfigDocument::getValueTree`). `benchmark/smoke-report-baseline.md` and `smoke-report.md` headings set to `## epoch`; `smoke.md` compares against the baseline again.

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 33: DAP JSON Through jam::JsonDocument and jam::JsonWriter ✅

**Date:** 2026-11-14
**Duration:** part of one session. `jam/carol/SPRINT-LOG.md` Sprint 187 is the primary record.
**Scope:** `jam/PLAN-json.md` ruling 3, ARCHITECT: *"All of them"* — "whatdbg's full DAP path: DynamicObject builds replaced by JsonWriter appends, reads by JsonDocument, JSON::toString removed."

### Files Modified
- `Source/dap/Reader.h`, `.cpp` — DAP messages read through `jam::JsonDocument`.
- `Source/dap/Types.h`, `Source/Whatdbg.h`, `.cpp`, `Source/debug/BreakpointManager.h`, `.cpp`, `Source/debug/Session.h`, `.cpp`, `Source/debug/Session_mac.cpp` — responses and events built through `jam::JsonWriter`; `juce::DynamicObject` builds and `JSON::toString` removed.
- `cast/identifiers.md`, `cast/source.cast` (new), `cast/spell.md`, `Source/generated/*` (new) — the identifier lane and its cast output.
- `CMakeLists.txt` — the `juce-plugin-window-size-hook.patch` row.

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied
- [ ] `Session.cpp` (Windows) is not compiled on this macOS host.

### Problems Solved
- whatdbg built DAP JSON through `juce::DynamicObject` and `juce::JSON`. It now uses jam's one JSON module.

### State for Continuation
- Windows build and a DAP run are not done this session.

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 32: Windows Debugging of EVE — Fast Evaluate, Exit on Terminate, One Stop Reason, Audit Sweep ✅

**Date:** 2026-11-12
**Duration:** not measured (one session, continued across a context compaction)
**Scope:** ARCHITECT: *"must fix first [] whatdbg [] cast then after that we could continue to debug eve"*; evaluate hang: *"Fix now"*; audit findings: *"fix whatdbg first"*; finding 18: *"Merge only"*; ledger: *"just delete debt, not relevant anymore"*; build.sh: *"cast build.sh leave it"*; log: *"at whatdbg repo after all done"*.

### Agents Participated
- COUNSELOR: ODE diagnosis from runtime logs, trace specs (`eve-locals-probe`, `eve-step-probe`, `eve-stepout-probe`, `eve-pause-probe`), every Engineer brief, validation of each return by read, audit filter, RFC, doc corrections, this log.
- Pathfinder (haiku): every whatdbg probe run against EVE.exe and log extraction; the doxygen pass.
- Librarian (haiku): DbgEng research — AddSymbol, symbol options, name-lookup cost, TerminateProcesses, WaitForEvent return values.
- Engineer (sonnet): ODE probes (added, then removed), the fixes below, Steps 1 and 2 of the audit sweep, the TU merge, the ARCHITECTURE.md / SPEC.md corrections, the debt clear.
- Auditor (opus): one sprint audit — 26 findings, 20 stale-doc items.

### Files Modified (21 total; 5 deleted)
- `Source/debug/Session.cpp` `evaluateExpression` — evaluate through the frame's scope symbol group `AddSymbol`, with `SYMOPT_NO_PUBLICS` for the lookup and the previous options restored by `juce::ErasedScopeGuard`; success only on `S_OK`. Runtime: a failed `EVE!shell` lookup took 7022 ms with the public table and 44 ms without it. The secondary-client `??` path (`CaptureOutputCallback`, `getOrCreateCaptureClient`, `getExecutedOutput`, `evaluateStringValue`, `.symopt- 100`) is deleted; its `SetExpressionSyntax` was engine-global.
- `Source/debug/Session.cpp` `pollEvents` — returns `void`. `E_UNEXPECTED` (DbgEng: all targets exited, session ended) sets `hasProcessExited` / `exited`: disconnect after `TerminateProcesses` no longer spins 5 s. `S_OK` while `isStepPending` completes the step: `ChangeEngineState` reported `DEBUG_STATUS_BREAK` during `gu` with no stop, so stepOut lost its stop.
- `Source/debug/Session.cpp` — initialize-invariant null checks removed from every method except `shutdown` (Main.cpp runs `run` only after `initialize` succeeds); `getLineStatus` HRESULT lookup (`jam::HashMap`) in `getSourceOffset`; `getStackTrace (maxFrames, firstFrameId)` gives each frame its final id; `getSymbolTypeName` / `getSymbolValue` shared by `getVariableObject` and `evaluateExpression`; `SessionInspection.cpp` merged in.
- `Source/debug/Session_mac.cpp` — mirrors: `pollEvents` void, `getStackTrace` ids, `jam::Function::Map` / `jam::HashMap` tables inside their one user, `lldbStopReason` parameter name, `SessionInspection_mac.cpp` merged in. Not compiled (Windows host).
- `Source/debug/Callbacks.cpp` — `onBreakpoint`: initial break while launching, `pause` while `isPausePending` (a Windows pause had reported `exception`), else exception with code and address; `BREAKPOINT` in `exceptionNames`; `Breakpoint` asserts `bp`; `LoadModule` sets `moduleLoad` except while a step is pending; `ChangeEngineState` does nothing and leaves `GetInterestMask`; tables are `jam::Function::Map` / `jam::HashMap` inside their one user.
- `Source/debug/State.h` — `enum class StopReason` and `stopReason` replace `hasBreakpointHit`, `hasStepCompleted`, `hasPauseCompleted`, `hasNewModuleLoaded`, `hasExceptionStopped`, `InitialBreakPhase`; `hasPendingBreakpoints` removed.
- `Source/Whatdbg.h`, `Source/Whatdbg.cpp` — `commands` and `stops` are `jam::Function::Map`; `processDeferredEvents` runs one drain per `stopReason`; the loop sleeps when it does not poll (it spun while stopped); `getStoppedEvent`, `getExceptionDescription`, `writeMessages` (was `emitResolvedBreakpointEvents`); thread event uses the event thread id; `WhatdbgHandlers.cpp` and `WhatdbgEvents.cpp` merged in.
- `Source/debug/BreakpointManager.h`, `.cpp` — engine ids are `std::optional` (dbgeng assigns id 0, which the 0 sentinel read as unresolved); `getBreakpointStoppedEvent` gives `breakpoint` for a tracked id and `step` otherwise; `onModuleLoad (imageName)`, `onReload`, `onSymbolsLoaded`; `onSetBreakpoints` parses lines once and builds each response once; `isUserBreakpoint`, `onBreakpointHit`, `addReloadedBreakpoints`, `Lines` removed; `BreakpointManagerHandlers.cpp` merged in; `-` replaces an em dash that arrived as mojibake.
- `Source/dap/Types.h` — `getBreakpointChangedEvent`, one builder for both former copies.
- `Source/debug/Session.h`, `Callbacks.h`, `State.h`, `Whatdbg.h`, `BreakpointManager.h`, `Types.h`, and the merged `.cpp` `@file` blocks — doxygen pass; false `resume` "No-op" note removed.
- `ARCHITECTURE.md`, `SPEC.md` — every line that named a removed symbol, a deleted file, or old behaviour now matches the code; the session-split decision quotes ARCHITECT: *"Merge only"*.
- `CLAUDE.md:38` — `**Active Debt:** None.`
- `DEBT.md` — `DEBT-20260902T184427` cleared.
- `RFC-message-loop-model.md` (new) — jam::Model / message-thread redesign, status open, as *"future consideration to plan"*.
- `CMakeLists.txt` (generated) — PCH `-Xclang -fno-pch-timestamp` under Clang, from the cast template.

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied
- [ ] Lean 300: `Whatdbg.cpp` 786, `Session.cpp` 863, `Session_mac.cpp` 792, `BreakpointManager.cpp` 385 lines — ARCHITECT's ruling *"Merge only"* (one object, one `.cpp`, no new types).

### Problems Solved
- `evaluate` at an exception stop blocked about 10 s, then timed out the client: DbgEng searched the public symbol table on a miss.
- Disconnect waited 5 s: no `ExitProcess` arrives after `TerminateProcesses`; `WaitForEvent` returns `E_UNEXPECTED`.
- stepOut reported a stop with `thread=0` and no frames, or hung: a `ChangeEngineState` BREAK status is not a stop.
- Windows pause reported `exception`.
- Audit: findings 1-17, 19, 22, 24-26 resolved; 18 by ARCHITECT's *"Merge only"*. Rejected with the read that refutes them: 20 (engine id and resolved line are two facts, `Session.h` addBreakpointByLocation doc), 21 (the breakpoint registry is the Model; the symbol group cache is a Session materialisation, MANIFESTO E), 23 and the `addDapVariable` part of 13 (NAMES Rule 1 `add`; DAP message building is creation). Sprint 31's rejection of the frame-id overwrite is superseded: Session now gives the final id at creation (finding 14).
- Runtime evidence (EVE Debug, `EVETerminalProcessor.cpp:431`): breakpoint pending then `changed`; `evaluate shell` returns the value, a miss returns the error at once; `next` stops at 432 and 434 with `step`; `pause` stops with `pause`; stepOut no longer reports a false stop; disconnect exits at once; no EVE or whatdbg process survives.

### State for Continuation
- `Session_mac.cpp` (all macOS hunks, the merge included) is not compiled. Build on macOS and run the same probes before relying on it.
- No stepOut probe has returned normally to `prepare`: EVE's assert at `juce_File.cpp:162` (from `EVETerminalProcessor.cpp:438`, stale `cwd = "/c/Users/jreng"` in `~/.config/dev/EVE.settings`) stops every run first. ARCHITECT deletes that file.
- `docs/xml` is stale (lists the deleted files); regenerate before a zero-warning doxygen check.
- The Windows `compile_commands.json` is the macOS one; LSP results on Windows-only files are invalid until it is regenerated.
- `build.sh` still uses vcvarsall; kept by ARCHITECT's ruling.
- Probe specs and logs: the session scratchpad (`eve-*-probe.json`, `whatdbg-*.log`).

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 31: Windows stackTrace Sends the Frames — No Nested Array ✅

**Date:** 2026-11-12
**Duration:** not measured (one session)
**Scope:** ARCHITECT: *"must fix first [] whatdbg [] cast then after that we could continue to debug eve"*; *"if whatdbg fixed: remove instrumentations, /log at whatdbg repo"*.

### Agents Participated
- COUNSELOR: trace spec, read of the stack path (Session → handler → JSON), diagnosis from the logged response body, delegation, validation of each return by read, audit filter, this log.
- Pathfinder (haiku): whatdbg crash traces of EVE.exe with the trace skill driver, binary provenance (SHA-256 of the installed and built `whatdbg.exe`), whatdbg.log extraction.
- Engineer (sonnet): temporary `stackTrace body` log in `onStackTrace` (added, then removed); brace → parentheses fix at six sites; audit fixes.
- Auditor (opus): sprint audit of the two changed files.

### Files Modified (2 total)
- `Source/WhatdbgHandlers.cpp:184` — `juce::Array<juce::var> frames (session.getStackTrace (maxStackFrames));`. The brace form `frames { session.getStackTrace (...) }` selected `juce::Array`'s template `initializer_list` constructor under clang-cl (`juce_Array.h:137-141`), so the array held one `var` that held the real array. Runtime evidence: `"stackFrames": [[{...}]], "totalFrames": 1`. CODING.md: parentheses when the initializer is the same container type.
- `Source/WhatdbgHandlers.cpp:160, 169, 288` — `onSetBreakpoints`, `onThreads`, `onVariables` build the response `var` straight from the call result (`var (Array<var>&&)`, `juce_Variant.h:109`); the named locals `resultBps`, `threads`, `dapVariables` and their copy through `var (const Array<var>&)` (`juce_Variant.h:86`) are gone (audit F1, MANIFESTO E "A second copy of the same complete data").
- `Source/WhatdbgHandlers.cpp:199-200, 251` — `stackFrames` and `scopes` move into the `var`; `totalFrames` is set before the move (audit F2). Every response array uses one form, `juce::var (...)` (audit F4, NAMES Rule 5).
- `Source/WhatdbgHandlers.cpp:271-273` — `rawVars (symbolIndex < 0 ? getLocals (...) : getVariableChildren (...))`, brace → parentheses, same defect class.
- `Source/WhatdbgHandlers.cpp` `onStackTrace` — comment `// Assign unique frame IDs and store threadId mapping` deleted (audit F5, CODING.md COMMENTS). Temporary `#if JUCE_DEBUG` `stackTrace body=` log removed.
- `Source/Whatdbg.cpp:220` — `events (breakpointManager.onModuleLoad ());`, brace → parentheses, same defect class.

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied

### Problems Solved
- Every Windows DAP client got an empty or broken stack: nvim showed no frames; the trace driver failed at `driver.lua:141` (`attempt to concatenate field 'name' (a nil value)`). Cause: the nested array above. After the fix the trace prints 13 frames with locals and ends `outcome=crash`.
- Same nesting removed from `threads`, `variables` (both lanes), `setBreakpoints` and module-load breakpoint events.
- Audit: F1, F2, F4, F5 resolved as above. F3 (`onStackTrace` overwrites the Session frame `id`) rejected: the handler assigns DAP frame identity through its own registry, `state.nextFrameId` / `state.frameIdMap` (MANIFESTO E: "identity is assigned and released through the registry's own API"); the Session index is read first (`:190`) and stored as the frame location (`:193-194`).

### State for Continuation
- The audit fixes (F1, F2, F4, F5) are not built yet. Run `cast cast/spell.md --debug`, then rerun the EVE crash trace and confirm `FRAME` lines and `END outcome=crash`.
- Verified at runtime before the audit fixes: Debug build SHA-256 `9c3c9dc5…` printed all frames.
- `git status` shows 330 entries, including 301 staged deletions (57220 lines: `CMakeLists.txt`, `Source/`, `.gitattributes` and others) for files that exist on disk. Not touched by this sprint; resolve before a commit.
- This log holds more than its "keep last 5" rule; no entry was rotated.

### Debts Paid
- None

### Debts Deferred
- None

