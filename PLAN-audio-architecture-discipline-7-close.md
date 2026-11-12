# PLAN: Audio Architecture Discipline Part 7 — Close: Proof, Timings, Documents

**RFC:** `RFC-message-loop-model.md`. Canon: `PLAN-audio-architecture-discipline-0-tracker.md`, The contract of every part ("identical working state, preferable FASTER and more DETERMINISTIC"); ruling 9.
**Date:** 2026-11-12
**BLESSED Compliance:** verified
**Language Constraints:** C++17 / JUCE 8.0.17 / JAM (LANGUAGE.md, C++ / JUCE)

## Overview

This part proves the three accepted criteria against the baseline of Part 1: the same working state, the timings, and the determinism. It then makes the documents of the project state the new structure.
It changes no behavior.

## What this part does not do

- No source change, except a correction that a failed proof of this part needs. Such a correction follows the Failure Protocol of CAROL.md.
- No doxygen. COUNSELOR delegates the doxygen pass to Pathfinder after the audit (CAROL.md Doxygen).

## Language / Framework Constraints

- MANIFESTO **D**: "The same input must always yield the same output".
- CAROL.md Profiling: scout. Tracker, decision 10: the Verdict lane, two reports read side by side, on each platform.
- CAROL.md Documentation: `ARCHITECTURE.md` and `SPEC.md` are PROJECT DECISIONS. A write to them is gated; this PLAN, when locked, is the approval for the changes that the steps name.
- CAROL.md Debugging: whatdbg over DAP stdio, with the trace skill.

## Dependency & API Inventory

| Need | Existing piece | Where |
|---|---|---|
| The baseline sequences | `tests/smoke/baseline/macos/*.txt`, `tests/smoke/baseline/windows/*.txt` | Part 1 Steps 5 and 6 |
| The baseline timing | `benchmark/smoke-report-baseline.md` and `benchmark/smoke-windows-report-baseline.md`: the rows `epoch`, `user`, `system` | Part 1 Steps 4 and 6 |
| The behavior tables | T1 to T12, T14, T15 | Part 1, Appendix A |
| The owners of the bodies after the rewrite | the eighteen command bodies: `Processor`; the six stop drains: `debug::StopManager`; the registry: `debug::VariableManager`; the builders: `dap::Types`; the engine events: the `events` entries | Parts 4 to 6 |
| The decisions to replace | "Decision: Two-Thread Model (not three, not event-driven)"; "Decision: AbstractFifo for stdin buffering"; "Decision: No JUCE Message Thread" | `ARCHITECTURE.md:317-339` |
| The State field list to replace | the list that ends at `ARCHITECTURE.md:311` | `ARCHITECTURE.md:292-311` |
| The principles to replace | "Two-Thread Model" and "Deferred Events" | `SPEC.md:28-29` |
| The stale build line | "Windows: compiles with MSVC — the Windows code written this sprint has not been compiled" | `SPEC.md:418-419` |
| The stale project facts | the layer order, the key source directories, the doxygen locations, "Critical Path" | `CLAUDE.md`, sections Current State, Layer Order & Key Source Directories, Doxygen Locations |
| The defects that started the rewrite | "sometimes wait took forever, breakpoint could take couple of seconds to break, sometimes it break to anywhere, or breakpoint does not show anything meaningful" | tracker, The contract of every part |
| The measured causes | macOS: a request waits for the poll, up to 1 s; a 10 ms sleep for each pass of a stopped target | tracker, differences D1 and D2 |

## Validation Gate

Each step is validated by COUNSELOR before proceeding to the next — against
MANIFESTO.md (BLESSED), NAMES.md, ~/.carol/CODING.md, and the locked PLAN decisions
(no deviation, no scope drift). @Auditor runs ONCE, after the final step, covering
the whole sprint — never per step.

## Steps

### Step 1: The same working state
**Scope:** the smoke suite; the tables of Part 1.
**Action:**
1. Run the smoke suite ten times on macOS.
2. For each row of table T1 (the 18 commands), read the command body in `Processor.cpp`, and the atom member that it calls, against the row: the `Session` calls, the `BreakpointManager` calls, and the DAP output in order.
3. For each stop row of the drain table of T4, read the drain in `StopManager.cpp` against the row. For the rows of the output, the location, and the exit, read the `events` entry against the row.

**Validation:** The 21 sequence checks pass in each of the ten runs. Each T1 row and each T4 row matches, or the difference is one of D1 to D7 of the tracker. The sprint log lists each row with its result.

### Step 2: Determinism
**Scope:** the ten runs of Step 1.
**Action:** Read the result of the sequence check of each scenario in each of the ten runs.
**Validation:** Each check passes in each run. Ten transcripts that equal one baseline are equal to each other. A scenario that differs is a failure of MANIFESTO **D**: find the principle that it breaks before any change.

### Step 3: The timings
**Scope:** `benchmark/smoke.md`; `benchmark/smoke-report.md`.
**Action:** ARCHITECT builds Release. Pathfinder runs `scout` on `benchmark/smoke.md`.
**Validation:** The sprint log holds, for each of the rows `epoch`, `user`, and `system`, the `value`, `errorPercent`, `min`, and `max` of the new report beside the values of `benchmark/smoke-report-baseline.md`. ARCHITECT reads them against "preferable FASTER".

### Step 4: The defects of the field
**Scope:** a debug session on a JAM project, macOS.
**Action:** ARCHITECT debugs one JAM project with the new build in nvim, with the work that showed the defects: a breakpoint in a module that loads late, a run of steps, a pause, and an evaluate. COUNSELOR reads `whatdbg.log` of a Debug build for each case that ARCHITECT reports.
**Validation:** ARCHITECT states the result for each of the four defects of the tracker quote. A defect that stays is recorded with its log lines; it is a new objective, not a step of this PLAN.

### Step 5: The same proof, Windows
**Scope:** a Windows machine.
**Owner:** ARCHITECT builds, runs, and debugs on the Windows machine. This step blocks no macOS step.
**Action:** Run Steps 1 to 4 on Windows, against the Windows baseline, with the manifest `benchmark/smoke-windows.md`.
**Validation:** The same checks hold on Windows.

### Step 6: `ARCHITECTURE.md`
**Scope:** `ARCHITECTURE.md`.
**Action:**
1. Replace the three decisions of `:317-339` with one decision, "Audio processor shape: engine context, message loop, and Model". Context: the old decisions and the four failure causes that they record. Decision: the thread table, the one-way flow, and the layers of the tracker (section The design; rulings 10 and 11). Rationale: rulings 1 and 9, the answers of decision 9, and the timings of Step 3.
2. Replace the thread model section, the module map, and the data flow with the structure of Part 6: `Whatdbg`, `WHATDBGModel`, `Processor`, `dap::Reader`, `dap::Writer`, `dap::FIFO`, `debug::Session`, `debug::BreakpointManager`, `debug::VariableManager`, `debug::StopManager`.
3. Replace the State field list (`:292-311`) with the parameter table of Part 6 and the homes of tracker decision 1.
4. Add the help page: `jam::TerminalPage` of `jam_tui` (Part 2).

**Validation:** No sentence of the file names `debug::State`, `Whatdbg::run`, a drain on the main thread, or "Two-Thread". Each class that the file names exists in `Source/` or in `jam/`.

### Step 7: `SPEC.md` and `CLAUDE.md`
**Scope:** `SPEC.md`; `CLAUDE.md`.
**Action:**
1. `SPEC.md:28-29`: replace Core Principles 1 and 2 with the thread table of the tracker and the events table of Part 5.
2. `SPEC.md`, the feature sections: change each name of a moved function to its new owner, and each `State::field` to the Model parameter or to its home of tracker decision 1.
3. `SPEC.md:418-419`: set the Windows build line to the result of Step 5.
4. `CLAUDE.md`: write again the sections Current State, Layer Order & Key Source Directories, and Doxygen Locations from the files on disk. Remove the files that do not exist: `WhatdbgHandlers.cpp`, `BreakpointManagerHandlers.cpp`, `SessionInspection.cpp`, `SessionInspection_mac.cpp`. Set the JUCE module list and the JAM module list to the rows of `project-info.md`.

**Validation:** Each file path and each class name of the three documents exists on disk. `SPEC.md` holds the eleven features with no change of a user flow.

### Step 8: Regenerate the indexes
**Scope:** `docs/xml`.
**Action:** Regenerate the doxygen XML with the command of the doxygen-protocol skill.
**Validation:** `docs/xml` holds `Processor`, `WHATDBGModel`, `StopManager`, `VariableManager`, and `Writer`, and no `State` class.

## BLESSED Alignment

- **B — Bound.** Each proof names its owner: COUNSELOR for the reads, ARCHITECT for the builds and the field work.
- **L — Lean.** No new test type. The suite and the scout manifest of Part 1 serve again.
- **E — Explicit.** Each criterion of ARCHITECT has one step and one recorded value.
- **S — SSOT.** The documents state what the code is. A deleted file has no line in them.
- **S — Stateless.** A proof compares outputs. It reads no internal state.
- **E — Encapsulation.** The suite speaks DAP only.
- **D — Deterministic.** Step 2 is the verdict: ten equal transcripts for each scenario.
- LANGUAGE.md: no override applies beyond the C++ / JUCE Lean adaptation.

## Risks / Open Questions

None open.

Facts for the review of the whole PLAN:

- **The suite time holds three requests that arrive while the target runs**: the `pause` of `04_pause` and of `16_pause_continue_breakpoint`, and the `setBreakpoints` of `14_breakpoint_while_running`. The field defects of the tracker quote have no measured cause beyond differences D1 and D2. Step 4 is ARCHITECT's own check.
- **Each platform has its own timing baseline** (tracker, decision 10).
- **The transcript holds no `output` line** (Part 1 Step 2). Scenario `07_output` checks the output text.
