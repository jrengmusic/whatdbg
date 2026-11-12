# PLAN: Audio Architecture Discipline Part 1 — The Parity Baseline

**RFC:** `RFC-message-loop-model.md`. Canon: `PLAN-audio-architecture-discipline-0-tracker.md`, The contract of every part.
**Date:** 2026-11-12
**BLESSED Compliance:** verified
**Language Constraints:** C++17 / JUCE 8.0.17 / JAM (LANGUAGE.md, C++ / JUCE); Lua for `tests/smoke`

## Overview

This part fixes the target of the rewrite before any source file changes.
It holds the current behavior of whatdbg as tables (Appendix A, read from commit `7f0d713`). It extends the smoke suite to the behavior that the suite does not cover. It records the message sequence of each scenario and the run time of the suite as the baseline.
Parts 2 to 7 prove parity against these three: the tables, the sequences, and the timings.

## What this part does not do

- No change of a file in `Source/`.
- No correction of a defect. A scenario that fails on the current build is recorded as it is, with its message sequence.

## Language / Framework Constraints

- MANIFESTO **D**: "Unit tests prove it formally — same input, bit-identical output". The transcript of a scenario is that output.
- MANIFESTO **S**: "Check if functionality already exists before creating new implementations". `DapClient` already keeps each message in arrival order in `self.log` (`dap_client.lua:39`, `:72`).
- CAROL.md Profiling: "Measurement of code uses `scout` — never a hand-written harness." No scenario measures a time.
- CODING.md, No identifier latitude in delegation: each new name of this part is in the text below.
- CAROL.md Build: ARCHITECT builds.

## Dependency & API Inventory

Reads of 2026-11-12.

| Need | Existing piece | Where |
|---|---|---|
| The suite entry | `run_smoke.lua`, run as `nvim --headless -l` with `<whatdbg> <fixture> <fixture_wait> <fixture.cpp> <fixture_crash>`; `exitTimeoutMs = 10000`; `breakpointMarker = "SMOKE_BREAKPOINT_LINE"` | `tests/smoke/run_smoke.lua:8-17`, `:39-50`, `:70` |
| The marker search | `findBreakpointLine (sourcePath, marker)`: a plain substring search, `line:find (marker, 1, true)`. It keeps the last match. | `run_smoke.lua:19-35` |
| The DAP client | `DapClient.spawn` (`:35`), `:drainMessages` (`:62`), `:send` (`:76`), `:waitFor` (`:91`), `:waitForResponse` (`:110`), `:waitForEvent` (`:116`), `:close` (`:122`); the members `messages` and `log` | `tests/smoke/dap_client.lua` |
| The report | `Report.new (name)`, `:pass`, `:info`, `:fail`, `:check`, `:print`; the field `name` | `tests/smoke/report.lua:4-38` |
| The scenario wrapper | `ScenarioRunner.runScenario (report, scenarioBody)`: a `pcall` of the body. It has no client: each body makes its own with `DapClient.spawn (binaryPath, {})`. | `tests/smoke/scenario_runner.lua:3-11`; `scenario_breakpoint.lua:40` |
| The run to a breakpoint | `runToStoppedBreakpoint (client, report, launchArguments, fixtureSourcePath, breakpointLine)` | `tests/smoke/scenario_breakpoint.lua:5-34` |
| The run to a started target | `runToLaunched (client, report, launchArguments)` | `tests/smoke/scenario_process.lua:7-22` |
| The ten scenarios | `01_launch_bp_continue` to `10_disconnect_detach` | Appendix A, T12 |
| The fixtures | `fixture.cpp` (51 lines), `fixture_wait.cpp` (14 lines), `fixture_crash.cpp` | `tests/smoke/` |
| The launch gives no argument to the debuggee | "DAP `launch` request `args` are never forwarded to the debuggee" | `carol/SMOKE-20260902T151350Z.md:105` |
| scout: the command | The `command` value is one path. scout runs `[command, manifest path]` in the manifest folder and needs exit code 0. | `scout/Source/Processor.cpp:66`; `Benchmark.cpp:109-136` |
| scout: the samples file | The file must exist after the epoch. scout checks no content. The rows `epoch`, `user`, and `system` go to the report when the file exists. | `scout/Source/Benchmark.cpp:145-152`, `:212-239` |
| scout: a script and an empty samples file | Sandbox run of 2026-11-12, three epochs: exit 0; the report holds `epoch`, `user`, and `system`, each with `value`, `errorPercent`, `min`, `max`, and `samples`. | tracker, decision 10 |
| scout: the launch on Windows | scout builds the line `"<full path of command>" "<manifest>"` and calls `CreateProcessW` with a null application name. Microsoft: "Command (.cmd) and batch (.bat) files can be directly provided as input to the CreateProcess as if it is an executable." and "CreateProcess uses the cmd.exe automatically to run the input .cmd or .bat." | `jam/jam_subprocess/subprocess/native/jam_ChildProcess_windows.cpp:27-29`, `:147-162`; MSRC blog, "MS14-019 – Fixing a binary hijacking via .cmd or .bat file" |
| The epochs of the skill template | `epochs` 11 | `profile/SKILL.md:146` |
| The installed binary | The post-build command copies the artefact to `$ENV{HOME}/.local/bin/${PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}` on each build. It has no platform test. The command name `whatdbg` is on PATH: "`whatdbg --help` gives the protocol". | `CMakeLists.txt:424-432`; `~/.carol/CAROL.md`, Debugging |
| The behavior tables | T1 to T12, T14, T15 | Appendix A |

Behavior that no scenario covers today (Appendix A, T1, T4, T6, T12; `SPEC.md`):

| # | Behavior | Where it is in the code |
|---|---|---|
| G1 | `stepIn` and `stepOut`; on Windows the step-out stop comes as a breakpoint with no registry entry | `Whatdbg.cpp:653-679`; `BreakpointManager.cpp:131-160`; `SPEC.md:144-145` |
| G2 | The children of a struct through `variables` with a child reference | `Whatdbg.cpp:564-624` |
| G3 | `stackTrace` of a thread that is not the event thread; `scopes` and `variables` after it | `Whatdbg.cpp:486-511`, `:536-562`, `:586-599` |
| G4 | A breakpoint that is set while the target runs | `Whatdbg.cpp:457-475`; `BreakpointManager.cpp:346-357` |
| G5 | A breakpoint that is removed, and a breakpoint on a line with no code (the 4-line window) | `BreakpointManager.cpp:32-100`, `:196-225` |
| G6 | `pause`, then `continue`, then a breakpoint hit | `Whatdbg.cpp:626-637`, `:681-690`; `SPEC.md:414` |
| G7 | An `evaluate` that fails: the error `Could not evaluate: <expr>` | `Whatdbg.cpp:728-729` |
| G8 | An `evaluate` and a `variables` in a frame that is not frame 0 | `Whatdbg.cpp:513-534`, `:706-731` |
| G9 | The order of the events after `configurationDone`: `process`, then `thread` started | `Whatdbg.cpp:282-301` |
| G10 | An unknown command: the error `Unknown command` | `Whatdbg.cpp:115` |
| G11 | A pending breakpoint that a later module load resolves: the `breakpoint` changed event | `BreakpointManager.cpp:333-344`, `:359-384`; `Session_mac.cpp:213-234` |

The time of a request that arrives while the target runs (tracker, difference D1) has no scenario of its own. Scenario `04_pause` sends that request, and the run time of the suite holds it (Step 4).

## Validation Gate

Each step is validated by COUNSELOR before proceeding to the next — against
MANIFESTO.md (BLESSED), NAMES.md, ~/.carol/CODING.md, and the locked PLAN decisions
(no deviation, no scope drift). @Auditor runs ONCE, after the final step, covering
the whole sprint — never per step.

## Steps

### Step 1: Make the tools current
**Scope:** `docs/xml`; the compile database that clangd reads.
**Action:**
1. Regenerate the doxygen XML with the command of the doxygen-protocol skill.
2. ARCHITECT builds whatdbg Debug (`cast cast/spell.md --debug`), so that `compile_commands.json` names the files of commit `7f0d713`.

**Validation:** `docs/xml/` holds no `_breakpoint_manager_handlers_8cpp.xml` and no `_whatdbg_handlers_8cpp.xml`. An LSP `findReferences` on `executionState` (`Source/debug/State.h:91`) returns no position in `Source/WhatdbgHandlers.cpp`.

### Step 2: The runner owns the client, and the transcript
**Scope:** `tests/smoke/scenario_runner.lua`; `tests/smoke/dap_client.lua`; `tests/smoke/run_smoke.lua`; `tests/smoke/scenario_breakpoint.lua`; `tests/smoke/scenario_process.lua`; `tests/smoke/scenario_terminate.lua`; new folder `tests/smoke/baseline/`.
**Action:**
1. `ScenarioRunner.runScenario (report, binaryPath, scenarioBody)`. The runner needs `DapClient` (`require ("dap_client")`). It does these steps in order:
   1. `local client = DapClient.spawn (binaryPath, {})`
   2. `pcall (scenarioBody, client)`; a failure calls `report:fail ("scenario error: " .. tostring (err))`, as today.
   3. `client:close ()`
   4. The transcript check of item 4.
   5. `return report`
2. Each of the ten scenario bodies takes `client` as its one parameter. Remove from each body its `DapClient.spawn (binaryPath, {})` line and its `client:close ()` line. The sites: `scenario_breakpoint.lua:40`, `:62`, `:70`, `:99`, `:117`, `:153`, `:161`, `:180`; `scenario_process.lua:33`, `:55`, `:64`, `:82`, `:90`, `:116`, `:124`, `:153`, `:161`, `:180`; `scenario_terminate.lua:31`, `:60`. Each `ScenarioRunner.runScenario (report, function ()` becomes `ScenarioRunner.runScenario (report, binaryPath, function (client)`.
3. Add `DapClient:getTranscript ()`. It reads `self.log` and returns one string. Each line ends with `\n`.
   - For each message of `self.log`, in order:
     - a response: `response <command> <success>`, and ` <message>` after it when `success` is false;
     - an event that is not `output`: `event <event>`, and ` <body.reason>` after it when the body holds a `reason`;
     - an `output` event: no line. The count of the `output` events follows the read size of the pipe (`Session_mac.cpp:85-97`), and today one event can hold stdout text and stderr text under one category (`Session_mac.cpp:91-94`). Scenario `07_output` checks the output text.
   - The string holds no `seq`, no `request_seq`, no thread id, no process id, no address, no path, and no output text.
4. The transcript check. `ScenarioRunner` gets two fields that `run_smoke.lua` sets before the first scenario: `ScenarioRunner.isRecording` (boolean) and `ScenarioRunner.baselineDirectory` (string).
   - The file is `ScenarioRunner.baselineDirectory .. "/" .. report.name .. ".txt"`.
   - When `isRecording` is true: write the transcript to the file, and call `report:info ("baseline recorded")`.
   - When `isRecording` is false and the file exists: `report:check ("message sequence equals baseline", transcript == baselineText)`. When they differ, also `report:info ("expected:\n" .. baselineText)` and `report:info ("actual:\n" .. transcript)`.
   - When `isRecording` is false and the file does not exist: `report:fail ("baseline not found: " .. path)`.
5. `run_smoke.lua`: `ScenarioRunner = require ("scenario_runner")`. `ScenarioRunner.isRecording = (arg[6] == "--record")`. The platform name is `"windows"` when `vim.fn.has ("win32") == 1`, and `"macos"` when it is not. `ScenarioRunner.baselineDirectory = scriptDirectory .. "/baseline/" .. platformName`. Create that folder with `vim.fn.mkdir (path, "p")`. Set these before the `reports` table (`run_smoke.lua:39`).

**Validation:** No scenario body holds `DapClient.spawn` or `client:close`. A run with `--record` writes ten files in `tests/smoke/baseline/macos/`. A second run with no `--record` passes the ten new checks. The check of each scenario `01` to `10` is unchanged.

### Step 3: The fixtures and the eleven scenarios
**Scope:** `tests/smoke/fixture.cpp`; `tests/smoke/fixture_wait.cpp`; new `tests/smoke/fixture_module.cpp`; new `tests/smoke/fixture_load.cpp`; new `tests/smoke/build.sh`; new `tests/smoke/scenario_inspect.lua`; `tests/smoke/scenario_breakpoint.lua`; `tests/smoke/scenario_process.lua`; `tests/smoke/run_smoke.lua`.
**Action:**
1. The markers. No new marker holds the text `SMOKE_BREAKPOINT_LINE`: the search keeps the last plain match (`run_smoke.lua:24-29`), so a marker that holds it moves the breakpoint of scenarios `01` to `10`. The new markers are `SMOKE_INNER_LINE`, `SMOKE_EMPTY_LINE`, `SMOKE_SECOND_LINE`, and `SMOKE_MODULE_LINE`.
2. `fixture.cpp`. Lines 28-31 and the five locals of lines 33-37 do not change.
   - After line 8, add `static constexpr int workerSleepMs { 20 };`, `static std::atomic<bool> shouldWorkerRun { true };`, and the struct `struct FixturePoint { int x; int y; };`. Add `#include <atomic>` after line 1.
   - After `computeSum` (line 18), add `static void runWorker ()`. Its body is one loop: `while (shouldWorkerRun.load ()) std::this_thread::sleep_for (std::chrono::milliseconds (workerSleepMs));`.
   - Line 15 becomes `total += value; // SMOKE_INNER_LINE`.
   - After line 37, add `FixturePoint point { 3, 4 };` and `std::thread worker { runWorker };`.
   - After line 40, add these three lines in order: `// SMOKE_EMPTY_LINE`; `const int secondSum { computeSum (numbers) };`; `std::printf ("second=%d point=%d,%d\n", secondSum, point.x, point.y); // SMOKE_SECOND_LINE`.
   - Before `return 0;` (line 50), add `shouldWorkerRun.store (false);` and `worker.join ();`.
3. `fixture_wait.cpp`. Lines 10-11 become: `int ticks { 0 };`, then `while (true)` with a brace body of two statements: the sleep of line 11, then `++ticks; // SMOKE_BREAKPOINT_LINE`. `fixture_crash.cpp` does not change.
4. `fixture_module.cpp`: a shared library with one exported function, `extern "C" void runModule ()`. On Windows the declaration has `__declspec (dllexport)`, inside `#ifdef _WIN32`. Its body is `std::printf ("module loaded\n"); // SMOKE_MODULE_LINE`, then `std::fflush (stdout);`.
5. `fixture_load.cpp`: `static constexpr int preLoadDelayMs { 500 };`. `main` prints `fixture_load started`, flushes, sleeps `preLoadDelayMs`, loads the library, calls `runModule`, and returns 0. The load is the one platform seam:
   - `#ifdef _WIN32`: `LoadLibraryA ("fixture_module.dll")` and `GetProcAddress (library, "runModule")`. The loader searches the folder of the application first.
   - otherwise: `dlopen ("@executable_path/libfixture_module.dylib", RTLD_NOW)` and `dlsym (library, "runModule")`.
   - The function pointer type is `void (*) ()`. The cast from the loader's return type is `reinterpret_cast` (CODING.md: function pointer and `void*`).
6. `tests/smoke/build.sh`. No file of the repository builds the fixtures today. The script holds the Windows environment block of the root `build.sh:9-26`, unchanged. It then builds into `tests/smoke/build/`, with debug information and no optimisation:
   - macOS: `c++ -std=c++17 -g -O0 <source> -o build/<name>` for `fixture`, `fixture_wait`, `fixture_crash`, and `fixture_load`; `c++ -std=c++17 -g -O0 -dynamiclib fixture_module.cpp -o build/libfixture_module.dylib`.
   - Windows: `cl /std:c++17 /Zi /Od /EHsc <source> /Fe:build\<name>.exe /Fo:build\obj\ /Fd:build\` for the four programs; the same with `/LD` and `/Fe:build\fixture_module.dll` for the library.
7. `run_smoke.lua`: after line 37, find the lines of the new markers with `findBreakpointLine`. `fixtureDirectory` is `vim.fn.fnamemodify (fixtureSourcePath, ":h")`. `buildDirectory` is `vim.fn.fnamemodify (fixturePath, ":h")`.
   - `innerLine`, `emptyLine`, `secondLine`: `fixtureSourcePath` with `SMOKE_INNER_LINE`, `SMOKE_EMPTY_LINE`, `SMOKE_SECOND_LINE`.
   - `waitSourcePath = fixtureDirectory .. "/fixture_wait.cpp"`; `waitLine`: marker `SMOKE_BREAKPOINT_LINE`.
   - `moduleSourcePath = fixtureDirectory .. "/fixture_module.cpp"`; `moduleLine`: marker `SMOKE_MODULE_LINE`.
   - `fixtureLoadPath = buildDirectory .. "/fixture_load"`, with `.exe` after it on Windows.
8. Add the eleven scenarios to the `reports` table after line 49, in this order. Each scenario has the form of `scenarioLaunchBpContinue` (`scenario_breakpoint.lua:36-64`): `Report.new`, then `ScenarioRunner.runScenario`. "Stop at the marker" is `runToStoppedBreakpoint (client, report, { program = fixturePath }, fixtureSourcePath, breakpointLine)`. "End" is `continue`, then a wait for `exited` with `exitTimeoutMs`. Each listed check is one `report:check`. The parameters of each scenario function are `binaryPath`, then each value of `run_smoke.lua` that its row names, in the order of first use, then `exitTimeoutMs` when its row waits with it.

| Report name | Function, file | Steps and checks |
|---|---|---|
| `11_step_in_out` | `scenarioStepInOut`, `scenario_breakpoint.lua` | Stop at the marker. `next`; `stopped` reason is `step`. `stepIn`; `stopped` reason is `step`; `stackTrace`: the name of frame 1 holds `computeSum`. `stepOut`; `stopped` reason is `step`; `stackTrace`: the name of frame 1 holds `main`. End. |
| `12_struct_children` | `scenarioStructChildren`, `scenario_inspect.lua` | Stop at the marker. `stackTrace`, `scopes`, `variables` as `scenarioVariables` does (`scenario_breakpoint.lua:121-134`). The variable `point` is present and its `variablesReference` is above 0. `variables` with that reference: `x` is present and its value holds `3`; `y` is present and its value holds `4`. End. |
| `13_thread_frames` | `scenarioThreadFrames`, `scenario_inspect.lua` | Stop at the marker. `threads`: 2 or more threads. Take the first thread whose `id` is not `stoppedEvent.body.threadId`. `stackTrace` with that id: success, 1 or more frames. `scopes` with the id of its frame 1: success. `variables` with the reference of scope 1: success. End. |
| `14_breakpoint_while_running` | `scenarioBreakpointWhileRunning`, `scenario_breakpoint.lua` | `runToLaunched (client, report, { program = fixtureWaitPath })`. Wait for `thread`. `setBreakpoints` on `waitSourcePath`, `waitLine`: success. `stopped` within `exitTimeoutMs`; reason is `breakpoint`. `terminate`; wait for its response. |
| `15_breakpoint_remove_and_window` | `scenarioBreakpointRemoveAndWindow`, `scenario_breakpoint.lua` | Stop at the marker. `setBreakpoints` on `fixtureSourcePath` with the one line `emptyLine`: success, and `body.breakpoints[1].verified` is true. `continue`; `stopped` reason is `breakpoint`. `setBreakpoints` on `fixtureSourcePath` with an empty list: success, and `#body.breakpoints` is 0. End. |
| `16_pause_continue_breakpoint` | `scenarioPauseContinueBreakpoint`, `scenario_breakpoint.lua` | `runToLaunched` with `fixtureWaitPath`. Wait for `thread`. `pause`; `stopped` reason is `pause`. `setBreakpoints` on `waitSourcePath`, `waitLine`: success. `continue`; `stopped` reason is `breakpoint`. `terminate`; wait for its response. |
| `17_evaluate_fail` | `scenarioEvaluateFail`, `scenario_inspect.lua` | Stop at the marker. `stackTrace`. `evaluate` of `notAVariable` in frame 1: `success` is false, and `message` holds `Could not evaluate`. End. |
| `18_inner_frame` | `scenarioInnerFrame`, `scenario_inspect.lua` | `runToStoppedBreakpoint` with `innerLine` in place of `breakpointLine`. `stackTrace`: 2 or more frames; the name of frame 1 holds `computeSum`; the name of frame 2 holds `main`. `scopes` with the id of frame 2, then `variables`: `counter` is present and its value holds `42`. `evaluate` of `counter` with the id of frame 2: the result holds `42`. `setBreakpoints` on `fixtureSourcePath` with an empty list: success. End. |
| `19_configuration_done_order` | `scenarioConfigurationDoneOrder`, `scenario_process.lua` | `runToLaunched (client, report, { program = fixturePath })`. Wait for `process`, then for `thread`. In `client.log`, the index of the `configurationDone` response is below the index of the `process` event, and that index is below the index of the `thread` event. Wait for `exited` with `exitTimeoutMs`. |
| `20_unknown_command` | `scenarioUnknownCommand`, `scenario_process.lua` | `initialize`; wait for its response and for `initialized`. Send `notACommand`; its response has `success` false and `message` equal to `Unknown command`. `disconnect`; its response has `success` true. |
| `21_pending_breakpoint` | `scenarioPendingBreakpoint`, `scenario_breakpoint.lua` | `initialize`, `initialized`, `launch` of `fixtureLoadPath`: success. `setBreakpoints` on `moduleSourcePath`, `moduleLine`: success, and `body.breakpoints[1].verified` is false. `configurationDone`. Wait for the event `breakpoint` with `exitTimeoutMs`; `body.reason` is `changed`. `stopped` reason is `breakpoint`. End. |

**Validation:** Each new scenario runs on the current build. Its result, pass or fail, is recorded with `--record`. The checks of scenarios `01` to `10` are unchanged, and the breakpoint line of scenarios `01` to `10` is the line of `SMOKE_BREAKPOINT_LINE` in `fixture.cpp`. No scenario reads a clock.

### Step 4: The baseline timing
**Scope:** new `benchmark/smoke.md` and `benchmark/smoke.sh`; new `benchmark/smoke-windows.md` and `benchmark/smoke.cmd`; the reports.
**Action:**
1. Write `benchmark/smoke.sh`, executable. `$1` is the manifest path. It does these steps:
   1. `root` is the parent folder of the folder of `$0`. `build` is `$root/tests/smoke/build`.
   2. Run `nvim --headless -l "$root/tests/smoke/run_smoke.lua" whatdbg "$build/fixture" "$build/fixture_wait" "$root/tests/smoke/fixture.cpp" "$build/fixture_crash"`.
   3. Keep the exit code. Make the empty file `smoke-samples.md` in the folder of `$1`. Exit with the kept code.
2. Write `benchmark/smoke.md` with one `## run` table in the grammar of `profile/SKILL.md:140-155`: `epochs` 11; `command` `smoke.sh`; `samples` `smoke-samples.md`; `output` `smoke-report.md`; `baseline` empty. The manifest has no `## toolchain` table and no `## sample` table.
3. Write `benchmark/smoke.cmd`, the Windows launcher, with the same three steps: `%~dp0..` is the root; the binary is the command name `whatdbg`, as in `smoke.sh`; each fixture path has `.exe`; `type nul > "%~dp1smoke-samples.md"` makes the empty file; `exit /b` gives the kept code. Write `benchmark/smoke-windows.md`: the same `## run` table, with `command` `smoke.cmd` and `output` `smoke-windows-report.md`.
4. ARCHITECT builds Release, so that the installed `whatdbg` is the Release binary.
5. Pathfinder runs `scout <absolute path of benchmark/smoke.md>` from the project root, by the protocol of the profile skill. Copy the report to `benchmark/smoke-report-baseline.md`. On the Windows machine the manifest is `benchmark/smoke-windows.md`, and the copy is `benchmark/smoke-windows-report-baseline.md` (Step 6).

**Validation:** scout exits with 0. The report holds the rows `epoch`, `user`, and `system`. The sprint log holds the `value`, `errorPercent`, `min`, and `max` of each row. These are the baseline of Part 7.

### Step 5: Record, macOS
**Scope:** `tests/smoke/baseline/macos/`.
**Action:** Run the suite with `--record` on macOS, then two times with no `--record`.
**Validation:** Both runs pass the sequence check of each scenario. The 21 files are the macOS baseline. A scenario whose sequence differs between the two runs is a finding: the sprint log records the two texts, and the step stops there (CAROL.md Failure Protocol).

### Step 6: Record, Windows
**Scope:** a Windows machine; `tests/smoke/baseline/windows/`.
**Owner:** ARCHITECT builds, runs, and debugs on the Windows machine. This step blocks no macOS step.
**Action:** Run Step 5 on Windows. Then run Step 4 item 5 with the Windows manifest. Scenario `08` checks `EXC_BAD_ACCESS`, and scenarios `09` and `10` use `pgrep` and `ps` (`scenario_process.lua:120-155`, `:172-175`; `scenario_terminate.lua:6-25`): record their results as they are.
**Validation:** The 21 files of `tests/smoke/baseline/windows/` exist. `benchmark/smoke-windows-report-baseline.md` exists. The sprint log names each scenario that fails on Windows.

## BLESSED Alignment

- **B — Bound.** The runner owns the client from spawn to close. The suite owns its fixtures and its baseline files.
- **L — Lean.** One transcript function. One scenario for each gap. No new message store: the transcript reads `self.log`.
- **E — Explicit.** A sequence that differs fails a check with the two texts. A missing baseline fails.
- **S — SSOT.** The baseline files are the one statement of the message order. Appendix A is the one statement of the engine calls.
- **S — Stateless.** A transcript line holds no run-specific value.
- **E — Encapsulation.** The suite speaks DAP only. It reads no internal state of whatdbg.
- **D — Deterministic.** Two runs give equal transcripts (Step 5). The `output` events are not in the sequence (Step 2 item 3).
- LANGUAGE.md: no override applies beyond the C++ / JUCE Lean adaptation.

## Risks / Open Questions

None open.

Facts for the review of the whole PLAN:

- **The suite last changed on 2026-09-03** (`329dcaa`). Commit `7f0d713` changed 23 files and no file of `tests/smoke`.
- **The ten scenarios passed on macOS only** (`SPEC.md:431-434`).
- **The spawn order of scenario `02_attach` changes.** Today the body starts `fixture_wait` before whatdbg (`scenario_process.lua:28-33`). The runner now starts whatdbg first. The attach request still follows the 500 ms wait.
- **`run_smoke.lua` always exits with 0.** It prints the summary and has no exit call (`run_smoke.lua:70`). So `smoke.sh` gives scout the code 0 also when a scenario fails. Step 5 is the pass check; Step 4 measures the time.

---

# Appendix A — The current behavior of whatdbg, commit `7f0d713`

Pathfinder read each named file in full on 2026-11-12. Each file:line is from that read.

Repository: /Users/jreng/Documents/Poems/dev/whatdbg
HEAD: 7f0d713 (branch main). The files of `Source/` and of `tests/smoke/` are unchanged from that commit. The working tree changes other files: `.clangd`, `.project`, `CMakeLists.txt`, `build-liblldb.sh`, `cast/cmake.cast`, `gh.cmake`, `project-info.md`, three files of `.claude/commands/`, and `docs/`.
The tables are T1 to T12, T14, and T15. No table has the number T13.
Line numbers: working tree at read time. "file:line" = Read output line numbers.

## File inventory (wc -l)

| Path | Lines |
|---|---|
| Source/Main.cpp | 308 |
| Source/Whatdbg.cpp | 786 |
| Source/Whatdbg.h | 391 |
| Source/dap/Reader.cpp | 168 |
| Source/dap/Reader.h | 89 |
| Source/dap/Types.h | 208 |
| Source/debug/BreakpointManager.cpp | 386 |
| Source/debug/BreakpointManager.h | 222 |
| Source/debug/Callbacks.cpp | 384 |
| Source/debug/Callbacks.h | 341 |
| Source/debug/Loader.cpp | 66 |
| Source/debug/Loader.h | 68 |
| Source/debug/PrettyPrint.h | 166 |
| Source/debug/Session.cpp | 866 |
| Source/debug/Session.h | 629 |
| Source/debug/SessionPrettyPrint.cpp | 376 |
| Source/debug/SessionPrettyPrint_mac.cpp | 107 |
| Source/debug/Session_mac.cpp | 795 |
| Source/debug/State.h | 336 |
| Source/generated/ProjectInfo.h | 43 |
| Source/.DS_Store | not counted (binary) |
| cast/.cast-format | 7 |
| cast/cmake.cast | 420 |
| cast/installer.cast | 171 |
| cast/signing.md | 23 |
| cast/spell.md | 102 |
| cast/installer/mac/distribution.xml | 36 |
| cast/installer/win/installer.nsi | 132 |
| cast/installer/resources/background-dark.png | 143 (binary; newline count only) |
| cast/installer/resources/background.png | 139 (binary; newline count only) |
| cast/installer/resources/header.bmp | 0 (binary; newline count only) |
| cast/installer/resources/welcome.bmp | 51 (binary; newline count only) |

tests/smoke: build/ (binaries, dSYM), dap_client.lua, fixture.cpp, fixture_crash.cpp, fixture_wait.cpp, report.lua, run_smoke.lua, scenario_breakpoint.lua, scenario_process.lua, scenario_runner.lua, scenario_terminate.lua.

## T1. DAP commands

Dispatch table: Whatdbg.cpp:26-43 (18 keys, `commands.add`). Constructor 23-51. onCommand 93-118: non-"request" messages are ignored (Whatdbg.cpp:97); unknown command returns error response "Unknown command" (Whatdbg.cpp:115).
"requires stopped": no handler checks `executionState` except onDisconnect (Whatdbg.cpp:437-438). Session.h notes "Must be called while the target is stopped" for getStackTrace (Session.h:381), getLocals (404), getVariableChildren (423), evaluateExpression (442-443), getThreads (461). The handlers do not check it.

| # | Key (Whatdbg.cpp) | Handler body | Session calls | BreakpointManager calls | State reads | State writes | DAP output, in order | Requires stopped |
|---|---|---|---|---|---|---|---|---|
| 1 | initialize (26) | onInitialize 334-339 | none | none | none | none | response "initialize" body=getCapabilities (337); event "initialized" (338) | no |
| 2 | launch (27) | onLaunch 341-365 | launch (351); on success addSearchPaths (355) -> appendSymbolPath / appendSourcePath (367-383) | none | none | on success: executionState=launching (357), targetProgram=program (358) | success: response "launch" (359). Failure: error "Failed to launch process" (363). No event here; "process" comes later (see drainInitialBreak row) | no |
| 3 | attach (28) | onAttach 385-417 | attach(pid) (395); on success addSearchPaths (399) | none | none | on success: targetProcessId=pid (401); Windows executionState=launching (404); macOS executionState=stopped (406), stopReason=initialBreak (407) | success: response "attach" (410), event "process" name="pid N", startMethod "attach" (411). Failure: error "Failed to attach" (415) | no |
| 4 | configurationDone (29) | onConfigurationDone 419-425 | none | none | none | isConfigurationDone=true (422) | response "configurationDone" (424) | no |
| 5 | disconnect (30) | onDisconnect(msg,false) 427-455 | terminateDebuggee(targetProcessId) (442), only if shouldTerminate and alive | none | executionState (437-438), targetProcessId (442) | shouldTerminateOnExit (433); terminate path: executionState=running (448), terminateDeadlineMs=now+5000 (449); else isRunning=false (453) | response "disconnect" (435) is written BEFORE the terminate action (442) | no (alive check at 437-438) |
| 6 | terminate (31) | same onDisconnect(msg,true) | same | none | same | same, with isTerminate true | response uses request "command" = "terminate" (430, 435) | no |
| 7 | setBreakpoints (32) | onSetBreakpoints 457-475 | via BPM: getOffsetStatus, getOffset, addBreakpointByLocation, removeBreakpoint, forceReloadAllSymbols | onSetBreakpoints (472), onReload (474) | none | none | response "setBreakpoints" body.breakpoints (473); then events from onReload, if any (474) | no |
| 8 | threads (33) | onThreads 477-484 | getThreads (482) | none | none | none | response "threads" body.threads (483) | no (Session.h:461 says must be stopped) |
| 9 | stackTrace (34) | onStackTrace 486-511 | setCurrentThreadBySystemId if threadId>0 (494); getStackTrace(50, nextFrameId) (497) | none | nextFrameId (497, 501, 505) | frameIdMap insert (501); nextFrameId += frame count (505) | response "stackTrace" body totalFrames, stackFrames (510) | no |
| 10 | scopes (35) | onScopes 536-562 | selectFrameFromId (538) -> setCurrentThreadBySystemId (529) | none | frameIdMap via selectFrameFromId (518-520) | lastScopesThreadId (545, if >0); nextVariablesRef++ (548); variablesRefMap insert (549) | response "scopes" with one scope name "Locals", expensive false (561) | no |
| 11 | variables (36) | onVariables 586-599 | setCurrentThreadBySystemId if lastScopesThreadId>0 (593); getDapVariables -> getLocals (576) or getVariableChildren (577) | none | lastScopesThreadId (591-593); variablesRefMap (568-570) | via addDapVariable (601-624): nextVariablesRef++ (612); variablesRefMap insert (613) for expandable children | response "variables" (598) | no |
| 12 | continue (37) | onContinue 626-637 | resume via resumeExecution (630 -> 162) | none | none | executionState=running (163); isStepPending=false (631); isPausePending=false (632) | response "continue" body allThreadsContinued=true (636) | no |
| 13 | next (38) | onNext 639-651 | stepOver (643) | none | none | executionState=running (644); isStepPending=true (645) | response "next" (650) | no |
| 14 | stepIn (39) | onStepIn 653-665 | stepInto (657) | none | none | executionState=running (658); isStepPending=true (659) | response "stepIn" (664) | no |
| 15 | stepOut (40) | onStepOut 667-679 | stepOut (671) | none | none | executionState=running (672); isStepPending=true (673) | response "stepOut" (678) | no |
| 16 | pause (41) | onPause 681-690 | interrupt(targetProcessId) (685) | none | targetProcessId (685) | executionState=running (686); isPausePending=true (687) | response "pause" (689) | no |
| 17 | evaluate (42) | onEvaluate 706-731 | selectFrameFromId (713) -> setCurrentThreadBySystemId; evaluateExpression (716) | none | frameIdMap via selectFrameFromId | none | success: response "evaluate" body result, variablesReference 0 (724). Empty result: error "Could not evaluate: <expr>" (728-729) | no |
| 18 | exceptionInfo (43) | onExceptionInfo 692-704 | none | none | exceptionCode, isMachException (696); exceptionCode, exceptionAddress via getExceptionDescription (700; 745-746) | none | response "exceptionInfo" body exceptionId, description, breakMode "unhandled" (703) | no |

Event order after configurationDone (run by the drain on the next loop pass; see T4):
1. drainInitialBreak (Whatdbg.cpp:120-127) when stopReason == initialBreak and isConfigurationDone.
2. resumeAfterInitialBreak (282-301): writeMessages(onReload) (284); event "process" (name=targetProgram, systemProcessId, isLocalProcess true, startMethod "launch") only when targetProgram is non-empty (286-289); resumeExecution (291); event "thread" reason "started", threadId = session.getEventThreadSystemId() (293-296).
For attach the "process" event is sent by onAttach (411), so resumeAfterInitialBreak sends no process event.

## T2. debug::State fields (State.h)

Writers and readers are from the Read outputs. The last column is the LSP findReferences count. Platform tag: S = shared, W = Windows only, M = macOS only.

| Field | Type | State.h line | Writers (file:line, platform) | Readers (file:line) | LSP findReferences count |
|---|---|---|---|---|---|
| executionState | ExecutionState | 91 | Whatdbg.cpp:163 resumeExecution(S); 245 drainProcessExited(S); 261 drainTerminateTimeout(S); 357 onLaunch(S); 404 onAttach(W); 406 onAttach(M); 448 onDisconnect(S); 644 onNext(S); 658 onStepIn(S); 672 onStepOut(S); 686 onPause(S). Session.cpp:225 pollEvents E_UNEXPECTED(W); 230 pollEvents step(W). Callbacks.cpp:40 onBreakpoint(W); 62 onUnknownException(W); 254 Breakpoint(W); 327 ExitProcess(W). Session_mac.cpp:36 onBreakpointStop(M); 43 onStepStop(M); 50 onInterruptStop(M); 60 onSignalStop(M); 70 onExceptionStop(M); 143 onProcessStateStopped(M); 160 onProcessStateExited(M); 169 onProcessStateRunning(M) | Whatdbg.cpp:76-77 run(S); 305 getEndModeForExit(S); 437-438 onDisconnect(S). Callbacks.cpp:24 onBreakpoint(W). Session.cpp: none. Session_mac.cpp:140 onProcessStateStopped(M) | 28 |
| stopReason | StopReason | 97 | Whatdbg.cpp:124 drainInitialBreak(S, clear); 131 drainBreakpointHit(S, clear); 139 drainStepCompleted(S, clear); 151 drainPauseCompleted(S, clear); 176 drainModuleLoaded(S, clear); 212 drainExceptionStopped(S, clear); 407 onAttach(M, initialBreak). Callbacks.cpp:26 onBreakpoint initialBreak(W); 31 onBreakpoint pause(W); 35 onBreakpoint exception(W); 59 onUnknownException exception(W); 252 Breakpoint(W); 347 LoadModule(W). Session.cpp:232-233 pollEvents step if none set(W). Session_mac.cpp:33 onBreakpointStop(M); 42 onStepStop(M); 49 onInterruptStop(M); 55 onSignalStop(M); 65 onExceptionStop(M); 142 onProcessStateStopped initialBreak(M) | Whatdbg.cpp:268 processDeferredEvents (stops.find) (S); 272-273 (S). Session.cpp:232 (W, read before writing step) | 15 |
| processExitCode | int | 104 | Callbacks.cpp:328 ExitProcess(W); Session_mac.cpp:158 onProcessStateExited(M) | Whatdbg.cpp:234 log (S, JUCE_DEBUG); 240 drainProcessExited(S) | 4 |
| targetProcessId | uint32 | 114 | Whatdbg.cpp:401 onAttach(S, after success); Callbacks.cpp:310 CreateProcess(W); Session_mac.cpp:278-279 launch(M) | Whatdbg.cpp:289 resumeAfterInitialBreak(S); 442 onDisconnect(S); 685 onPause(S) | 9 |
| targetProgram | String | 123 | Whatdbg.cpp:358 onLaunch(S, after success) | Whatdbg.cpp:286 resumeAfterInitialBreak (isLaunchSession) (S); 289 (S) | 5 |
| terminateDeadlineMs | uint32 | 132 | Whatdbg.cpp:247 drainProcessExited -> 0 (S); 260 drainTerminateTimeout -> 0 (S); 449 onDisconnect = now+5000 (S) | Whatdbg.cpp:253-254 drainTerminateTimeout (S) | 7 |
| isRunning | bool | 142 | Whatdbg.cpp:246 drainProcessExited false (S); 262 drainTerminateTimeout false (S); 453 onDisconnect false (S) | Whatdbg.cpp:70 run loop (S) | 6 |
| shouldTerminateOnExit | bool | 148 | Whatdbg.cpp:433 onDisconnect (S) | Whatdbg.cpp:306 getEndModeForExit (S); 440 onDisconnect (S) | 6 |
| isConfigurationDone | bool | 154 | Whatdbg.cpp:422 onConfigurationDone (S) | Whatdbg.cpp:122 drainInitialBreak (S) | 4 |
| isStepPending | bool | 162 | Whatdbg.cpp:631 onContinue false (S); 645 onNext true (S); 659 onStepIn true (S); 673 onStepOut true (S); Session.cpp:229 pollEvents false (W) | Session.cpp:227 pollEvents (W); Callbacks.cpp:344 LoadModule (W) | 9 |
| isPausePending | bool | 171 | Whatdbg.cpp:632 onContinue false (S); 687 onPause true (S); Callbacks.cpp:30 onBreakpoint false (W); Session_mac.cpp:116 onStopReason false (M) | Callbacks.cpp:28 onBreakpoint (W); Session_mac.cpp:114 onStopReason (M) | 7 |
| breakpointEngineId | int32 | 181 | Callbacks.cpp:253 Breakpoint (W); Session_mac.cpp:34 onBreakpointStop (M) | Whatdbg.cpp:133 drainBreakpointHit (S) | 3 |
| lastLoadedImageName | String | 189 | Callbacks.cpp:348 LoadModule (W) | Whatdbg.cpp:178 drainModuleLoaded (S) | 2 |
| hasBreakpointLocationsResolved | bool | 197 | Session_mac.cpp:231 onBreakpointEvent (M); Whatdbg.cpp:188 drainBreakpointLocationResolved clear (S) | Whatdbg.cpp:186 (S) | 4 |
| resolvedBreakpointEngineId | int32 | 204 | Session_mac.cpp:229 onBreakpointEvent (M) | Whatdbg.cpp:190 drainBreakpointLocationResolved (S) | 3 |
| resolvedBreakpointLine | uint32 | 210 | Session_mac.cpp:230 onBreakpointEvent (M) | Whatdbg.cpp:191 (S) | 3 |
| hasProcessExited | bool | 219 | Callbacks.cpp:329 ExitProcess (W); Session.cpp:224 pollEvents E_UNEXPECTED (W); Session_mac.cpp:159 onProcessStateExited (M); Whatdbg.cpp:237 drainProcessExited clear (S) | Whatdbg.cpp:231 drainProcessExited (S) | 4 |
| exceptionCode | uint32 | 233 | Callbacks.cpp:36 onBreakpoint (W); 60 onUnknownException (W); Session_mac.cpp:57 onSignalStop (M); 67 onExceptionStop (M) | Whatdbg.cpp:214 drainExceptionStopped (S); 696 onExceptionInfo (S); 745 getExceptionDescription (S) | 8 |
| exceptionAddress | uint64 | 240 | Callbacks.cpp:37 onBreakpoint (W); 61 onUnknownException (W); Session_mac.cpp:59 onSignalStop (M); 69 onExceptionStop (M) | Whatdbg.cpp:746 getExceptionDescription (S) | 5 |
| isMachException | bool | 252 | Session_mac.cpp:56 onSignalStop false (M); 66 onExceptionStop true (M). Windows never writes it (default false) | Whatdbg.cpp:214 (S); 696 (S) | 6 |
| hasDebuggeeOutput | bool | 260 | Callbacks.cpp:188 Output2 (W); Session_mac.cpp:94 drainProcessStdio (M); Whatdbg.cpp:199 drainDebuggeeOutput clear (S) | Whatdbg.cpp:197 drainDebuggeeOutput (S) | 4 |
| debuggeeOutputText | String | 267 | Callbacks.cpp:187 Output2 += (W); Session_mac.cpp:91 drainProcessStdio += (M); Whatdbg.cpp:206 clear (S) | Whatdbg.cpp:203 body output (S) | 4 |
| debuggeeOutputCategory | String (default "console") | 275 | Session_mac.cpp:93 drainProcessStdio "stdout"/"stderr" (M). Windows never writes it | Whatdbg.cpp:202 body category (S) | 3 |
| nextVariablesRef | int | 283 | Whatdbg.cpp:326 resetVariablesState = 1 (S); 548 onScopes ++ (S); 612 addDapVariable ++ (S) | same lines (S) | 6 |
| variablesRefMap | unordered_map<int, pair<int,int>> | 291 | Whatdbg.cpp:327 clear (S); 549 onScopes insert (S); 613 addDapVariable insert (S) | Whatdbg.cpp:568, 570 getDapVariables (S) | 10 |
| nextFrameId | int | 299 | Whatdbg.cpp:328 reset to 1 (S); 505 onStackTrace += count (S) | Whatdbg.cpp:497 (S); 501 (S) | 6 |
| frameIdMap | unordered_map<int, pair<uint32,int>> | 306 | Whatdbg.cpp:329 clear (S); 501 onStackTrace insert_or_assign (S) | Whatdbg.cpp:518, 520 selectFrameFromId (S) | 8 |
| lastScopesThreadId | uint32 | 313 | Whatdbg.cpp:330 reset 0 (S); 545 onScopes (S) | Whatdbg.cpp:591, 593 onVariables (S) | 8 |

Count: 28 fields. No State field is read or written by BreakpointManager.cpp (verified by full read). The Session.cpp/Session_mac.cpp State accesses go through State::getInstance() (Session.cpp:220; Session_mac.cpp:278, 324).

## T3. Engine events

### T3a. Windows callbacks (Callbacks.cpp, Callbacks.h). dbgeng calls these during WaitForEvent (Session.cpp:218). None of them calls a Session method. Engine calls made from inside a callback are listed.

| Method | file:line | State writes | Engine calls made | Return value |
|---|---|---|---|---|
| OutputCallbacks::AddRef / Release | Callbacks.cpp:114-117, 119-122 | none | none | ++refCount / --refCount |
| OutputCallbacks::QueryInterface | Callbacks.cpp:124-149 | none | none | S_OK for IUnknown, IDebugOutputCallbacks, IDebugOutputCallbacks2; else E_NOINTERFACE |
| OutputCallbacks::Output (legacy) | Callbacks.cpp:155-161 | none (JUCE_DEBUG log only) | none | S_OK |
| OutputCallbacks::GetInterestMask | Callbacks.cpp:167-171 | none | none | *mask = DEBUG_OUTCBI_ANY_FORMAT; S_OK |
| OutputCallbacks::Output2 | Callbacks.cpp:173-193 | debuggeeOutputText += text (187); hasDebuggeeOutput = true (188). Only when which == DEBUG_OUTCB_TEXT, text != nullptr, and (arg & DEBUG_OUTPUT_DEBUGGEE) != 0 | none | S_OK always |
| EventCallbacks::AddRef / Release | Callbacks.cpp:199-202, 204-207 | none | none | refCount |
| EventCallbacks::QueryInterface | Callbacks.cpp:209-228 | none | none | S_OK for IUnknown, IDebugEventCallbacks; else E_NOINTERFACE |
| EventCallbacks::GetInterestMask | Callbacks.cpp:234-242 | none | none | mask = BREAKPOINT | EXCEPTION | CREATE_PROCESS | EXIT_PROCESS | LOAD_MODULE; S_OK |
| EventCallbacks::Breakpoint | Callbacks.cpp:244-261 | stopReason = breakpoint (252); breakpointEngineId (253); executionState = stopped (254) | IDebugBreakpoint::GetId (250) | DEBUG_STATUS_BREAK (260) |
| EventCallbacks::Exception | Callbacks.cpp:263-288 | none directly; dispatches (below) | none | result of the dispatched handler |
| - onBreakpoint (static) | Callbacks.cpp:22-42 | launching: stopReason = initialBreak (26). else isPausePending: isPausePending = false (30), stopReason = pause (31). else stopReason = exception (35), exceptionCode (36), exceptionAddress (37). Then executionState = stopped (40) | none | DEBUG_STATUS_BREAK (41) |
| - onThreadName (static) | Callbacks.cpp:44-47 | none | none | DEBUG_STATUS_GO_NOT_HANDLED (46) |
| - onUnknownException (static) | Callbacks.cpp:49-66 | firstChance != 0: none (55). Second chance: stopReason = exception (59), exceptionCode (60), exceptionAddress (61), executionState = stopped (62) | none | firstChance: GO_NOT_HANDLED (55); second chance: DEBUG_STATUS_BREAK (initial value, 51) |
| EventCallbacks::CreateThread / ExitThread | Callbacks.cpp:290-293, 295-298 | none | none | DEBUG_STATUS_NO_CHANGE (not in interest mask, never called) |
| EventCallbacks::CreateProcess | Callbacks.cpp:300-319 | targetProcessId = GetProcessId(handle) (310) | GetProcessId (308) | DEBUG_STATUS_NO_CHANGE (318) |
| EventCallbacks::ExitProcess | Callbacks.cpp:321-331 | executionState = exited (327); processExitCode (328); hasProcessExited = true (329) | none | DEBUG_STATUS_NO_CHANGE (330) |
| EventCallbacks::LoadModule | Callbacks.cpp:333-350 | if isStepPending: none (345). Else stopReason = moduleLoad (347); lastLoadedImageName = imageName (348) | none | isStepPending: NO_CHANGE (345); else DEBUG_STATUS_BREAK (349) |
| EventCallbacks::UnloadModule | Callbacks.cpp:352-355 | none | none | DEBUG_STATUS_NO_CHANGE |
| EventCallbacks::SystemError | Callbacks.cpp:357-360 | none | none | DEBUG_STATUS_NO_CHANGE |
| EventCallbacks::SessionStatus | Callbacks.cpp:362-365 | none | none | S_OK |
| EventCallbacks::ChangeDebuggeeState | Callbacks.cpp:367-370 | none | none | S_OK |
| EventCallbacks::ChangeEngineState | Callbacks.cpp:372-375 | none | none | S_OK |
| EventCallbacks::ChangeSymbolState | Callbacks.cpp:377-380 | none | none | S_OK |
| getExceptionName (Windows) | Callbacks.cpp:71-108 | none | none | "0x<hex>" default; table at 75-95 (18 entries, including CPP_EXCEPTION 0xE06D7363) |

msvcThreadNameException = 0x406D1388 (Callbacks.cpp:16). Exception dispatch map (Callbacks.cpp:271-277): EXCEPTION_BREAKPOINT -> onBreakpoint; msvcThreadNameException -> onThreadName; any other code -> onUnknownException.

Session::pollEvents, Windows, verbatim (Session.cpp:216-239):

```
void Session::pollEvents (std::uint32_t timeoutMs) noexcept
{
    const HRESULT hr { control->WaitForEvent (0, static_cast<ULONG> (timeoutMs)) };

    auto* debugState { State::getInstance () };

    if (hr == E_UNEXPECTED)
    {
        debugState->hasProcessExited = true;
        debugState->executionState   = ExecutionState::exited;
    }
    else if (hr == S_OK and debugState->isStepPending)
    {
        debugState->isStepPending  = false;
        debugState->executionState = ExecutionState::stopped;

        if (debugState->stopReason == StopReason::none)
            debugState->stopReason = StopReason::step;

#if JUCE_DEBUG
        jam::debug::Log::write ("WHATDBG: step completed");
#endif
    }
}
```

### T3b. macOS handlers reached from Session::pollEvents (Session_mac.cpp)

Chain: pollEvents (310) -> listener.WaitForEvent (320). If process event -> onProcessEvent (328, defined 172). Else if breakpoint event -> onBreakpointEvent (332, defined 213).

| Handler | file:line | State writes | Calls |
|---|---|---|---|
| onProcessEvent | 172-211 | none directly. Calls drainProcessStdio for STDOUT bit (195) and STDERR bit (200); then processStates map (179-189): eStateStopped, eStateCrashed, eStateSuspended -> onProcessStateStopped; eStateExited -> onProcessStateExited; eStateRunning, eStateStepping -> onProcessStateRunning | SBProcess::GetStateFromEvent (203) |
| drainProcessStdio | 76-98 | debuggeeOutputText += (91); debuggeeOutputCategory = "stderr"/"stdout" (93); hasDebuggeeOutput = true (94) | SBProcess::GetSTDERR (86) / GetSTDOUT (87), loop while bytesRead > 0 |
| onProcessStateStopped | 131-150 | if not GetRestartedFromEvent (136) and executionState == launching: stopReason = initialBreak (142), executionState = stopped (143). Else: onStopReason(*state, thread, thread.GetStopReason()) (147) | SBProcess::GetRestartedFromEvent (136); GetSelectedThread (138); GetStopReason (147) |
| onStopReason | 100-129 | if isPausePending: isPausePending = false (116), then onInterruptStop (117). Else stopReasons map (102-112): eStopReasonBreakpoint -> onBreakpointStop; eStopReasonTrace and eStopReasonPlanComplete -> onStepStop; eStopReasonInterrupt -> onInterruptStop; eStopReasonSignal -> onSignalStop; eStopReasonException -> onExceptionStop. Any other reason: no write | none |
| onBreakpointStop | 31-37 | stopReason = breakpoint (33); breakpointEngineId = GetStopReasonDataAtIndex(0) (34-35); executionState = stopped (36) | SBThread::GetStopReasonDataAtIndex |
| onStepStop | 39-44 | stopReason = step (42); executionState = stopped (43) | none |
| onInterruptStop | 46-51 | stopReason = pause (49); executionState = stopped (50) | none |
| onSignalStop | 53-61 | stopReason = exception (55); isMachException = false (56); exceptionCode = GetStopReasonDataAtIndex(0) (57-58); exceptionAddress = GetSelectedFrame().GetPC() (59); executionState = stopped (60) | SBThread::GetSelectedFrame, SBFrame::GetPC |
| onExceptionStop | 63-71 | same as onSignalStop, with isMachException = true (66) | same |
| onProcessStateExited | 152-161 | processExitCode = GetExitStatus() (158); hasProcessExited = true (159); executionState = exited (160) | SBProcess::GetExitStatus |
| onProcessStateRunning | 163-170 | executionState = running (169) | none |
| onBreakpointEvent | 213-234 | if eventType is LocationsResolved or LocationsAdded and bp valid with GetNumLocations() > 0: resolvedBreakpointEngineId = bp.GetID() (229); resolvedBreakpointLine = line or 0 (230); hasBreakpointLocationsResolved = true (231) | SBBreakpoint::GetBreakpointEventTypeFromEvent, GetBreakpointFromEvent, GetLocationAtIndex, GetAddress().GetLineEntry() |

Session::pollEvents, macOS, verbatim (Session_mac.cpp:310-335):

```
void Session::pollEvents (std::uint32_t timeoutMs) noexcept
{
    // SBListener::WaitForEvent only accepts whole-second timeouts. Round the
    // caller's millisecond timeout up so the wait actually blocks instead of
    // busy-spinning at timeoutSeconds == 0.
    static constexpr std::uint32_t millisecondsPerSecond { 1000 };
    const std::uint32_t timeoutSeconds {
        (timeoutMs + millisecondsPerSecond - 1) / millisecondsPerSecond };

    lldb::SBEvent event;
    const bool hasEvent { listener.WaitForEvent (timeoutSeconds, event) };

    if (hasEvent)
    {
        auto* debugState { State::getInstance () };

        if (lldb::SBProcess::EventIsProcessEvent (event))
        {
            onProcessEvent (debugState, process, event);
        }
        else if (lldb::SBBreakpoint::EventIsBreakpointEvent (event))
        {
            onBreakpointEvent (debugState, event);
        }
    }
}
```

## T4. Main loop, drains, stops table (Whatdbg.cpp)

Stops table, verbatim (Whatdbg.cpp:45-50; constructor 23-51). There is no entry for StopReason::none.

```
    stops.add (debug::StopReason::initialBreak, [this] (const debug::StopReason&) { drainInitialBreak (); });
    stops.add (debug::StopReason::moduleLoad,   [this] (const debug::StopReason&) { drainModuleLoaded (); });
    stops.add (debug::StopReason::breakpoint,   [this] (const debug::StopReason&) { drainBreakpointHit (); });
    stops.add (debug::StopReason::step,         [this] (const debug::StopReason&) { drainStepCompleted (); });
    stops.add (debug::StopReason::pause,        [this] (const debug::StopReason&) { drainPauseCompleted (); });
    stops.add (debug::StopReason::exception,    [this] (const debug::StopReason&) { drainExceptionStopped (); });
```

Whatdbg::run, verbatim (Whatdbg.cpp:63-91):

```
void Whatdbg::run ()
{
    reader.start ();
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] main loop started");
#endif

    while (state.isRunning)
    {
        // 1. Drain FIFO — process all pending DAP commands
        for (juce::var message { reader.tryPop () }; not message.isVoid (); message = reader.tryPop ())
            onCommand (message);

        if (state.executionState == debug::ExecutionState::running
            or state.executionState == debug::ExecutionState::launching)
        {
            session.pollEvents (pollTimeoutMs);
        }
        else
        {
            juce::Thread::sleep (idleSleepMs);
        }

        // 2. Process deferred events
        processDeferredEvents ();
    }

    session.shutdown (getEndModeForExit ());
}
```

processDeferredEvents, verbatim (Whatdbg.cpp:266-280):

```
void Whatdbg::processDeferredEvents ()
{
    const auto stop { stops.find (state.stopReason) };

    if (stop != stops.end ())
    {
        const auto& [stopReason, drain] { *stop };
        drain (stopReason);
    }

    drainBreakpointLocationResolved ();
    drainDebuggeeOutput ();
    drainProcessExited ();
    drainTerminateTimeout ();
}
```

Each drain, verbatim (Whatdbg.cpp:120-264):

```
void Whatdbg::drainInitialBreak ()
{
    if (state.isConfigurationDone)
    {
        state.stopReason = debug::StopReason::none;
        resumeAfterInitialBreak ();
    }
}

void Whatdbg::drainBreakpointHit ()
{
    state.stopReason = debug::StopReason::none;

    writeMessage (breakpointManager.getBreakpointStoppedEvent (state.breakpointEngineId, session.getEventThreadSystemId ()));
    resetVariablesState ();
}

void Whatdbg::drainStepCompleted ()
{
    state.stopReason = debug::StopReason::none;

    writeMessage (getStoppedEvent ("step"));
    resetVariablesState ();
    writeMessages (breakpointManager.onReload ());
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] step completed, emitted stopped event");
#endif
}

void Whatdbg::drainPauseCompleted ()
{
    state.stopReason = debug::StopReason::none;

    writeMessage (getStoppedEvent ("pause"));
    resetVariablesState ();
#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] pause completed, emitted stopped event");
#endif
}

void Whatdbg::drainModuleLoaded ()
{
    state.stopReason = debug::StopReason::none;

    writeMessages (breakpointManager.onModuleLoad (state.lastLoadedImageName));
    session.resume ();
}

void Whatdbg::drainBreakpointLocationResolved ()
{
    // Breakpoint location resolved asynchronously by liblldb (target loaded
    // a new module that resolved a previously-pending BP location).
    if (state.hasBreakpointLocationsResolved)
    {
        state.hasBreakpointLocationsResolved = false;

        writeMessages (breakpointManager.onBreakpointLocationFound (state.resolvedBreakpointEngineId,
                                                                    static_cast<std::uint16_t> (state.resolvedBreakpointLine)));
    }
}

void Whatdbg::drainDebuggeeOutput ()
{
    if (state.hasDebuggeeOutput)
    {
        state.hasDebuggeeOutput = false;

        DynObj body { new juce::DynamicObject () };
        body->setProperty ("category", state.debuggeeOutputCategory);
        body->setProperty ("output",   state.debuggeeOutputText);

        writeMessage (dap::getEvent ("output", juce::var (body)));
        state.debuggeeOutputText.clear ();
    }
}

void Whatdbg::drainExceptionStopped ()
{
    state.stopReason = debug::StopReason::none;

    const juce::String exceptionName { debug::getExceptionName (state.exceptionCode, state.isMachException) };
    const juce::String description   { getExceptionDescription () };
    const int threadId { static_cast<int> (session.getEventThreadSystemId ()) };

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] exception stopped: " + exceptionName + " " + description
                             + " (threadId=" + juce::String (threadId) + ")");
#endif

    writeMessage (getExceptionStoppedEvent (exceptionName, description, threadId));
    writeMessage (getExceptionOutputEvent (exceptionName, description));
    resetVariablesState ();
}

void Whatdbg::drainProcessExited ()
{
    // Process exit
    if (state.hasProcessExited)
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] emitting exited (code=" + juce::String (state.processExitCode)
                                 + ") + terminated events");
#endif
        state.hasProcessExited = false;

        DynObj exitBody { new juce::DynamicObject () };
        exitBody->setProperty ("exitCode", state.processExitCode);
        writeMessage (dap::getEvent ("exited", juce::var (exitBody)));

        writeMessage (dap::getEvent ("terminated"));

        state.executionState = debug::ExecutionState::exited;
        state.isRunning = false;
        state.terminateDeadlineMs = 0;
    }
}

void Whatdbg::drainTerminateTimeout ()
{
    if (state.terminateDeadlineMs != 0
        and juce::Time::getMillisecondCounter () >= state.terminateDeadlineMs)
    {
#if JUCE_DEBUG
        jam::debug::Log::write ("[Whatdbg] terminate timed out after "
                                 + juce::String (terminateTimeoutMs) + "ms, forcing exit");
#endif
        state.terminateDeadlineMs = 0;
        state.executionState = debug::ExecutionState::exited;
        state.isRunning = false;
    }
}
```

resumeAfterInitialBreak, verbatim (Whatdbg.cpp:282-301), called by drainInitialBreak:

```
void Whatdbg::resumeAfterInitialBreak ()
{
    writeMessages (breakpointManager.onReload ());

    const bool isLaunchSession { state.targetProgram.isNotEmpty () };

    if (isLaunchSession)
        writeMessage (getProcessEvent (state.targetProgram, state.targetProcessId, "launch"));

    resumeExecution ();

    DynObj threadBody { new juce::DynamicObject () };
    threadBody->setProperty ("reason",   "started");
    threadBody->setProperty ("threadId", static_cast<int> (session.getEventThreadSystemId ()));
    writeMessage (dap::getEvent ("thread", juce::var (threadBody)));

#if JUCE_DEBUG
    jam::debug::Log::write ("[Whatdbg] resumed after initial break");
#endif
}
```

Drain table:

| Drain (Whatdbg.cpp) | Trigger | DAP written, in order | Cleared | Session calls | BreakpointManager calls |
|---|---|---|---|---|---|
| drainInitialBreak 120-127 | stopReason == initialBreak (stops table). Runs only if isConfigurationDone; otherwise stopReason stays initialBreak and the drain runs again on every loop pass (no else branch) | via resumeAfterInitialBreak: BPM onReload events (284); "process" launch event if targetProgram set (289); "thread" started (296) | stopReason (124) | getEventThreadSystemId (295); resume (291) via resumeExecution (160-164) | onReload (284) |
| drainBreakpointHit 129-135 | stopReason == breakpoint | "stopped" from getBreakpointStoppedEvent(breakpointEngineId, threadId) (133) | stopReason (131); resetVariablesState (134) | getEventThreadSystemId (133); resetSymbolGroupCache (331, via resetVariablesState) | getBreakpointStoppedEvent (133) |
| drainStepCompleted 137-147 | stopReason == step | "stopped" reason "step" via getStoppedEvent (141); then BPM onReload events (143) | stopReason (139); variables state (142) | getEventThreadSystemId (737 via getStoppedEvent); resetSymbolGroupCache (331) | onReload (143) |
| drainPauseCompleted 149-158 | stopReason == pause | "stopped" reason "pause" via getStoppedEvent (153) | stopReason (151); variables state (154) | getEventThreadSystemId (737); resetSymbolGroupCache (331) | none |
| drainModuleLoaded 174-180 | stopReason == moduleLoad | BPM onModuleLoad events (178), in order | stopReason (176) | resume (179), direct call, not via resumeExecution: executionState unchanged | onModuleLoad (178) |
| drainBreakpointLocationResolved 182-193 (not a stop drain; always run) | hasBreakpointLocationsResolved | BPM onBreakpointLocationFound events (190) | hasBreakpointLocationsResolved (188) | none | onBreakpointLocationFound (190) |
| drainDebuggeeOutput 195-208 (always run) | hasDebuggeeOutput | "output" event body {category, output} (205) | hasDebuggeeOutput (199); debuggeeOutputText (206). Category kept | none | none |
| drainExceptionStopped 210-226 | stopReason == exception | "stopped" reason exception, text, description, threadId, allThreadsStopped (223); then "output" category stderr (224) | stopReason (212); variables state (225). exceptionCode and exceptionAddress persist | getEventThreadSystemId (216); resetSymbolGroupCache (331) | none |
| drainProcessExited 228-249 (always run) | hasProcessExited | "exited" body exitCode (241); "terminated" (243) | hasProcessExited (237); executionState = exited (245); isRunning = false (246); terminateDeadlineMs = 0 (247) | none | none |
| drainTerminateTimeout 251-264 (always run) | terminateDeadlineMs != 0 and now >= deadline | none | terminateDeadlineMs (260); executionState = exited (261); isRunning = false (262) | none | none |

Poll gate in run(): session.pollEvents(50) when executionState is running or launching (Whatdbg.cpp:76-80, 17); otherwise juce::Thread::sleep(10) (81-84, 18). The FIFO is drained fully before the poll (Whatdbg.cpp:73-74).

## T5. Session public methods (Session.h)

Windows = Session.cpp (whole file inside #if JUCE_WINDOWS). macOS = Session_mac.cpp (whole file inside #if JUCE_MAC). Callers from Read; LSP incomingCalls result in brackets.

| Method (Session.h decl) | Windows engine calls (Session.cpp) | macOS engine calls (Session_mac.cpp) | Timed wait | Callers (Read) [LSP] |
|---|---|---|---|---|
| initialize (89) | CoInitializeEx (40); loader.load (46); loader.createDebugClient (50); QueryInterface x4 (59-76); SetOutputCallbacks (87); SetOutputMask (91); AddSymbolOptions (97); SetCodeLevel (98); AddEngineOptions (99); SetEventCallbacks (100) | SBDebugger::Initialize (245); Create (246); SetAsync (247); fopen /dev/null (252); SetInputFile (254); GetListener (256); IsValid (258) | none | Whatdbg.cpp:60 [1] |
| launch (107) | CreateProcess2 (160) | CreateTarget (263); StartListeningForEvents (267); SetLaunchFlags eLaunchFlagDebug|eLaunchFlagStopAtEntry (270); Launch (274); writes State::targetProcessId (278-279) | none | Whatdbg.cpp:351 [1] |
| attach (122) | AttachProcess (191) | CreateTarget("") (288); StartListeningForEvents (292); AttachToProcessWithID (295) | none | Whatdbg.cpp:395 [1] |
| resume (129) | SetExecutionStatus(GO) (213) | Continue (307) | none | Whatdbg.cpp:162, 179; Session_mac.cpp:352, 370 [4] |
| pollEvents (146) | WaitForEvent(0, timeoutMs) (218) | SBListener::WaitForEvent(timeoutSeconds, event) (320); EventIsProcessEvent (326); EventIsBreakpointEvent (330) | YES: Windows up to timeoutMs (50 ms per Whatdbg.cpp:17); macOS rounds up to whole seconds (315-317), so 50 ms waits 1 s | Whatdbg.cpp:79 [1] |
| shutdown (170) | EndSession (252); SetEventCallbacks(null) (253); SetOutputCallbacks(null) (254); Reset COM pointers (257-262); CoUninitialize if owned (266) | Detach (366) for detach; Signal(SIGKILL) (369) + resume (370) for terminate; none for passive; SBDebugger::Destroy (376); Terminate (377) | none | Whatdbg.cpp:90; Session_mac.cpp:238 (~Session) [2]. Windows ~Session (Session.cpp:33) and init-failure (123) not in LSP |
| getOffsetStatus (191) | GetOffsetByLine (419, 426); GetLineByOffset (435) via getSourceOffset (414-450) | getLineEntry (453-488): GetNumModules (461); GetModuleAtIndex (465); GetNumCompileUnits (466); GetCompileUnitAtIndex (470); FindLineEntryIndex (472); GetLineEntryAtIndex (476) | none | BreakpointManager.cpp:48 [1] |
| getOffset (204) | same as getOffsetStatus (464) | getLineEntry (501); GetStartAddress (504); GetLoadAddress (504) | none | BreakpointManager.cpp:61 [1] |
| addBreakpoint (217) | AddBreakpoint2 (475); SetOffset (479); AddFlags ENABLED (480); GetId (482) | BreakpointCreateByAddress (408) | none | BreakpointManager.cpp:108 [1]; Session.cpp:506 (Windows, Read) not in LSP |
| addBreakpointByLocation (235) | getOffsetStatus (501); getOffset (505); addBreakpoint (506); BreakpointLocation::pack(engineId, line) (509) | BreakpointCreateByLocation (420); GetID (425); GetNumLocations (426); GetLocationAtIndex (431); GetAddress().GetLineEntry() (432) | none | BreakpointManager.cpp:237 (Read); LSP returned BreakpointManagerHandlers.cpp:72 (file absent) |
| removeBreakpoint (248) | GetBreakpointById2 (518); RemoveBreakpoint2 (522) | BreakpointDelete (448) | none | BreakpointManager.cpp:219 [2 incl. stale BreakpointManagerHandlers.cpp:54] |
| loadModuleSymbols (262) | Execute ".reload /f \"<basename>\"" (369) | none (returns ok) | none | BreakpointManager.cpp:339 [1] |
| forceReloadAllSymbols (273) | Execute ".reload /f" (383) | none (returns ok) | none | BreakpointManager.cpp:352 [2 incl. stale BreakpointManagerHandlers.cpp:121] |
| stepOver (285) | SetExecutionStatus(STEP_OVER) (289) | GetSelectedThread().StepOver(eOnlyDuringStepping) (384) | none | Whatdbg.cpp:643 [1] |
| stepInto (295) | SetExecutionStatus(STEP_INTO) (294) | GetSelectedThread().StepInto() (389) | none | Whatdbg.cpp:657 [1] |
| stepOut (304) | Execute "gu" (299) | StepOutOfFrame(frame) (396) | none | Whatdbg.cpp:671 [1] |
| interrupt (315) | OpenProcess (306); DebugBreakProcess (311); CloseHandle (312) | SendAsyncInterrupt (403) | none | Whatdbg.cpp:685 [1] |
| terminateDebuggee (333) | TerminateProcesses (347) | Signal(SIGKILL) (340); Kill (348) on failure; resume (352) on success | none | Whatdbg.cpp:442 [1] |
| appendSymbolPath (345) | AppendSymbolPath (273) | HandleCommand "settings append target.debug-file-search-paths <path>" (522) | none | Whatdbg.cpp:370, 377 (Windows), 380 (other) [1 = only the non-Windows call] |
| appendSourcePath (357) | AppendSourcePath (281) | HandleCommand "settings set target.source-map \".\" \"<path>\"" (531) | none | Whatdbg.cpp:376 (Windows), 379 (other) [1 = only the non-Windows call] |
| getStackTrace (383) | GetStackTrace (786); getStackFrame (795): GetNameByOffset (750); GetLineByOffset (760) | GetNumFrames (711); GetFrameAtIndex (715); GetFunctionName (680); GetLineEntry (684) | none | Whatdbg.cpp:497 [1] |
| getLocals (406) | getOrCreateSymbolGroup (806): SetScopeFrameByIndex (550); GetScopeSymbolGroup2 (553); getSymbols (810): GetNumberSymbols (712), GetSymbolParameters (717), GetSymbolName (722); getVariableObject (727): GetSymbolTypeName (662), GetSymbolValueText (674), prettyPrint (680) | cacheFrameVariables (724): GetSelectedThread (631); GetFrameAtIndex (631); GetVariables(true,true,true,true) (632); GetValueAtIndex (728) | none | Whatdbg.cpp:576 [1] |
| getVariableChildren (425) | getOrCreateSymbolGroup (821); ExpandSymbol (826); getSymbols (830) | cacheFrameVariables (745); GetValueAtIndex (750); GetNumChildren (751); GetChildAtIndex (755) | none | Whatdbg.cpp:577 [1] |
| evaluateExpression (445) | getOrCreateSymbolGroup (842); GetSymbolOptions (847); AddSymbolOptions(SYMOPT_NO_PUBLICS) (848); restore via SetSymbolOptions (849-850); AddSymbol (852); RemoveSymbolByIndex (856); getSymbolValue (857) | GetSelectedThread (772); GetFrameAtIndex (773); EvaluateExpression (774); GetError (776-786) | none | Whatdbg.cpp:716 [1] |
| getThreads (463) | GetNumberThreads (572); GetThreadIdsByIndex (578); GetCurrentThreadId (582); SetCurrentThreadId (586, 617); GetCurrentThreadHandle (591); GetThreadDescription (596) | GetNumThreads (537); GetThreadAtIndex (541); GetName (542); GetThreadID (543) | none | Whatdbg.cpp:482 [1] |
| getEventThreadSystemId (475) | GetEventThread (628); GetCurrentThreadId (633); SetCurrentThreadId (634, 637); GetCurrentThreadSystemId (636) | GetSelectedThread().GetThreadID() (557) | none | Whatdbg.cpp:133, 216, 295, 737 [4] |
| setCurrentThreadBySystemId (489) | GetThreadIdBySystemId (649); SetCurrentThreadId (653) | SetSelectedThreadByID (564) | none | Whatdbg.cpp:494, 529, 593 [3] |
| resetSymbolGroupCache (497) | cachedSymbolGroup.Reset (534) | cachedFrameVariables.Clear (569) | none | Whatdbg.cpp:331 [1]; Session.cpp:257 (Windows, Read) not in LSP |

Session.h doc notes: evaluateExpression "may cause the engine to execute target code" (Session.h:442-443). Session.h:398 says getLocals entries carry "hasChildren/variablesReference"; Session.cpp:699-700 and Session_mac.cpp:670-671 set hasChildren and symbolIndex (variablesReference is set by Whatdbg.cpp:616-620).

## T6. BreakpointManager (BreakpointManager.h/.cpp)

Members (BreakpointManager.h):
- session: Session& (208) — not owned.
- breakpoints: unordered_map<uint16_t, BreakpointInfo> (211) — primary registry keyed by DAP id.
- nextDapId: uint16_t = 1 (214) — monotonic, never reused.
- lineSearchWindow: static constexpr uint32_t = 4 (217).

BreakpointInfo (BreakpointManager.h:15-42): sourcePath (21), dapId (24, default 0), line (27, default 0), resolvedLine (34, default 0), engineId: optional<int32_t> (41).

Public methods:
- ctor (62; defined .cpp:18-21).
- onSetBreakpoints(rawSourcePath, requestedBreakpoints) (80; .cpp:288-331).
- onModuleLoad(imageName) (92; .cpp:333-344).
- onReload() (101; .cpp:346-357).
- getBreakpointStoppedEvent(engineId, threadId) const (112; .cpp:131-160).
- onBreakpointLocationFound(engineId, resolvedLine) (125; .cpp:162-182).

Private methods:
- hasUnresolvedBreakpoints() const (130; .cpp:23-30): true when any entry has no engineId.
- onSymbolsLoaded() (136; .cpp:359-384).
- getBreakpointOffset(sourcePath, requestedLine) noexcept (152; .cpp:32-100): loop delta 0..lineSearchWindow (45), candidate = requested + delta; stops on found or engineBusy (45, 50).
- setBreakpointLocation(entryInfo&) noexcept (165; .cpp:102-129): offset via getBreakpointOffset (104); if offset != 0: addBreakpoint(offset) (108); on success sets engineId and resolvedLine.
- removeOrphanedBreakpoints(normalizedKey, requestedLines) (180; .cpp:196-225): entries whose normalized path matches; kept if line in requested list (returns line->dapId map), else removeBreakpoint (219) if engineId and erase.
- addBreakpoint(rawSourcePath, line, dapId) (194; .cpp:227-261): path conversion (231-235: toWindowsPath on Windows only, .cpp:189-194); session.addBreakpointByLocation (237); info.engineId and resolvedLine set only when location present (244-249); insert_or_assign (260).
- getBreakpointResponse(info, rawSourcePath) const noexcept (204; .cpp:263-286): verified = resolvedLine != 0 (266); line = resolvedLine if verified else requested (271-272); message "WHATDBG: pending - module not loaded for <path>:<line>" when unverified (276-279); source {path} (281-283).
- Helpers: normalizePath (.cpp:184-187), toWindowsPath (.cpp:189-194, Windows only).

State fields touched: none. BreakpointManager.cpp has no State reference.

Call flow:
- onSetBreakpoints (.cpp:288-331): normalizedKey (292); requestedLines from "line" (298-304); removeOrphanedBreakpoints (306); for each line: reuse dapId if existing, else addBreakpoint with nextDapId then ++nextDapId (323-324); response per line via getBreakpointResponse (327).
- onModuleLoad (.cpp:333-344): only if hasUnresolvedBreakpoints: session.loadModuleSymbols(imageName) (339) then onSymbolsLoaded.
- onReload (.cpp:346-357): only if hasUnresolvedBreakpoints: session.forceReloadAllSymbols() (352) then onSymbolsLoaded.
- onSymbolsLoaded (.cpp:359-384): for each entry without engineId: setBreakpointLocation; on success emit getBreakpointChangedEvent(dapId, resolvedLine) (371).
- getBreakpointStoppedEvent (.cpp:131-160): threadId, allThreadsStopped; if an entry has engineId == engineId: reason "breakpoint", hitBreakpointIds [dapId] (151-152); else reason "step" (156). Event "stopped" (159).
- onBreakpointLocationFound (.cpp:162-182): match entry by engineId; set resolvedLine; one "breakpoint" changed event (178).

Platform differences that change registry behavior (observed in code):
- Windows: addBreakpointByLocation returns a location only when getOffsetStatus is found (Session.cpp:501-509). Its resolvedLine is the requested line (Session.cpp:509), not the line of the resolved candidate. Pending entries keep an empty engineId and are retried by onReload/onModuleLoad.
- macOS: addBreakpointByLocation returns a location for every valid SBBreakpoint (Session_mac.cpp:423-441), with resolvedLine 0 when GetNumLocations is 0 (427-438). Such an entry has an engineId, so hasUnresolvedBreakpoints is false for it (BreakpointManager.cpp:23-30), and onModuleLoad/onReload do not retry it. Its resolution arrives through Session_mac.cpp:213-234 -> drainBreakpointLocationResolved.
- On macOS, setBreakpointLocation (which calls addBreakpoint(offset) = BreakpointCreateByAddress) runs only for entries with an empty engineId (BreakpointManager.cpp:367), i.e. only when BreakpointCreateByLocation returned an invalid breakpoint.

## T7. dap::Reader, writeMessage, dap::Types

Reader (Reader.h, Reader.cpp):
- Thread: juce::Thread named "dap::Reader" (Reader.cpp:20). Started by Whatdbg::run -> reader.start() (Whatdbg.cpp:65) -> startThread (Reader.cpp:36). Windows: _setmode(stdin, O_BINARY) (Reader.cpp:34).
- FIFO: juce::AbstractFifo fifo { fifoCapacity } (Reader.h:81); fifoCapacity = 64 (Reader.h:23); storage std::vector<juce::var> resized to 64 (Reader.cpp:22; Reader.h:84).
- Producer: background thread via addMessage (Reader.cpp:120-138): fifo.write(1); on blockSize1 > 0 stores message; else DROPPED (no queue), logged in JUCE_DEBUG only (131-137).
- Consumer: tryPop (Reader.cpp:46-55), non-blocking, main thread only; returns void var when empty.
- Framing (readContentLength, Reader.cpp:57-87): std::getline on std::cin per line; strips trailing '\r'; empty line ends the header block; line starting "Content-Length: " sets contentLength via getIntValue (78-81); other headers ignored; returns -1 when no Content-Length.
- Body (readMessage, Reader.cpp:89-118): std::cin.read(contentLength bytes) (92); juce::JSON::parse (98). Parse failure gives a void var, which is not queued (Reader.cpp:156).
- run (Reader.cpp:140-166): loop while not threadShouldExit and isConnected. If std::cin fails after read: isConnected = false (154). If contentLength < 0 or std::cin fails: isConnected = false (163). So a header block with no Content-Length ends the reader thread.
- Shutdown: ~Reader -> stop() (Reader.cpp:25-28, 39-44): signalThreadShouldExit (41); std::fclose(stdin) (42); stopThread(2000) (43). readContentLength checks threadShouldExit only between lines (62); a blocked std::getline (65) is unblocked only by the fclose. The 2000 literal is unnamed.
- Destruction order (C++ rule; members destroyed in reverse declaration order): Whatdbg.h declares state (370), session (375), breakpointManager (378), reader (381), so reader is destroyed first. Whatdbg::run calls session.shutdown(getEndModeForExit()) (Whatdbg.cpp:90) before any destructor.

writeMessage (Whatdbg.cpp:311-322, declared Whatdbg.h:361): juce::JSON::toString(message, true) (313); UTF-8 bytes (314-315); header "Content-Length: <bytes>\r\n\r\n" (317); std::cout.write header, then body (319-320); std::cout.flush (321). noexcept. Declared "Not thread-safe — must be called on the main thread only" (Whatdbg.h:359). No check of write results. Windows: _setmode(stdout, O_BINARY) (Whatdbg.cpp:57).

dap::Types (Types.h). No Types.cpp. All functions are inline in the header.
- using DynObj = juce::ReferenceCountedObjectPtr<juce::DynamicObject> (Types.h:13)
- inline int nextSequenceNumber { 1 } (Types.h:27) — process-wide variable
- inline int nextSeq () noexcept (Types.h:33-36)
- inline juce::var getResponse (int requestSeq, const juce::String& command, bool isSuccess, juce::var body = juce::var ()) noexcept (Types.h:49-65): fields seq, type "response", request_seq, command, success; body only if not void.
- inline juce::var getErrorResponse (int requestSeq, const juce::String& command, const juce::String& message) noexcept (Types.h:77-89): success false, message.
- inline juce::var getEvent (const juce::String& event, juce::var body = juce::var ()) noexcept (Types.h:100-112): fields seq, type "event", event; body only if not void.
- inline juce::var getBreakpointChangedEvent (int dapId, int line) noexcept (Types.h:122-134): event "breakpoint", body reason "changed", breakpoint {id, verified true, line}. Added by 7f0d713.
- inline juce::var getCapabilities () noexcept (Types.h:146-189): static vector of 31 (name, bool) pairs (148-181); 5 are true: supportsConfigurationDoneRequest (150), supportsEvaluateForHovers (154), supportsExceptionInfoRequest (164), supportTerminateDebuggee (165), supportsTerminateRequest (171). The rest are false.
- inline juce::String getString (const juce::var& obj, const juce::Identifier& key) noexcept (Types.h:200-206): empty string when obj is not a DynamicObject.

## T8. Main.cpp startup sequence

1. main (Main.cpp:294-308). `juce::ArgumentList (argc, argv).containsOption ("--help")` (296): prints ProjectInfo::projectName + versionString + helpText (298), returns 0.
2. [macOS only] `getenv (reexecMarker) == nullptr` (303): execWithSidecar(argv) (304).
   - execWithSidecar (215-235): getOrCreateConfigDirectory (217); getOrCreateSidecar (219); if valid: setenv DYLD_LIBRARY_PATH = sidecar dir (223); setenv WHATDBG_REEXEC = "1" (224); execv(argv[0], argv) (225). On failure: stderr "re-exec failed" and return 1 (227-234). If sidecar invalid: stderr "sidecar extraction failed", return 1.
   - getOrCreateSidecar, macOS (205-213): directory = config dir + "liblldb" (207); applySidecarBinary("liblldb.dylib", BinaryData::liblldb_dylib) (209-210).
3. runAdapter (238-292):
   - configDir = getOrCreateConfigDirectory (242). Config dir = userApplicationDataDirectory + "whatdbg" (108-109).
   - [JUCE_DEBUG] log file configDir/"whatdbg.log" (245); jam::debug::Log::Scope (246); crash handler (247); std::set_terminate (248); log "WHATDBG: started" (250).
   - sidecarDir = getOrCreateSidecar (253).
     - Windows (184-202): directory "dbgeng" (187); applySidecarBinary for dbgeng.dll (189-190), dbghelp.dll (191-192), dbgcore.dll (193-194), symsrv.dll (195-196). Returns the directory only if all four succeeded (198-200).
     - macOS (205-213): as above.
   - If sidecarDir valid: `Whatdbg whatdbg;` (261) -> constructor; whatdbg.initialize(sidecarDir) (262) -> session.initialize (Whatdbg.cpp:60). If true: whatdbg.run() (269). Else exitCode = 1 (276).
   - If sidecar invalid: exitCode = 1 (284).
   - [JUCE_DEBUG] log exit code (288). Return exitCode (291).
4. applySidecarBinary (165-182): writes only when the destination is missing or its size differs (169-170). Writes via juce::FileOutputStream (176-178).
5. Process exit: `runAdapter` returns; `main` returns its value.

Constants: sidecarDirName "whatdbg" (27), dbgengSubdir "dbgeng" (28), liblldbSubdir "liblldb" (29), logFileName "whatdbg.log" (30), reexecMarker "WHATDBG_REEXEC" (103, macOS).

Config directory: Main.cpp:108 uses juce::File::userApplicationDataDirectory. Main.cpp:13 and CLAUDE.md say the Windows sidecar is ~/.config/whatdbg/dbgeng/.

## T9. Platform #if blocks outside Session*.cpp, Callbacks.*, Loader.*

| File:line | Guard | What differs |
|---|---|---|
| Main.cpp:20-25 | JUCE_MAC | includes unistd.h, cstdio, cstring, cerrno |
| Main.cpp:102-104 | JUCE_MAC | reexecMarker constant |
| Main.cpp:184-202 | JUCE_WINDOWS | getOrCreateSidecar: dbgeng, dbghelp, dbgcore, symsrv DLLs |
| Main.cpp:204-236 | JUCE_MAC | getOrCreateSidecar: liblldb.dylib; execWithSidecar (re-exec) |
| Main.cpp:302-305 | JUCE_MAC | main: re-exec check before runAdapter |
| Whatdbg.h:5-8 | JUCE_WINDOWS | includes Callbacks.h and Loader.h |
| Whatdbg.cpp:12-15 | JUCE_WINDOWS | includes fcntl.h, io.h |
| Whatdbg.cpp:55-58 | JUCE_WINDOWS | initialize: _setmode(stdout, O_BINARY) |
| Whatdbg.cpp:369-371 | JUCE_WINDOWS | addSearchPaths: appendSymbolPath("srv*") |
| Whatdbg.cpp:375-381 | JUCE_WINDOWS vs else | addSearchPaths: Windows uses cwd with "/" -> "\\" for both appendSourcePath and appendSymbolPath; other platforms use cwd unchanged |
| Whatdbg.cpp:403-408 | JUCE_WINDOWS vs else | onAttach: Windows executionState = launching; other: executionState = stopped and stopReason = initialBreak |
| BreakpointManager.cpp:189-194 | JUCE_WINDOWS | toWindowsPath helper |
| BreakpointManager.cpp:231-235 | JUCE_WINDOWS vs else | addBreakpoint: Windows nativePath = backslash form; other nativePath = raw path |
| dap/Reader.cpp:11-14 | JUCE_WINDOWS | includes fcntl.h, io.h |
| dap/Reader.cpp:32-35 | JUCE_WINDOWS | start: _setmode(stdin, O_BINARY) |
| Session.h:5-11 | JUCE_WINDOWS | includes windows.h, wrl/client.h, dbgeng.h, Loader.h, Callbacks.h |
| Session.h:13-15 | JUCE_MAC | includes lldb/API/LLDB.h |
| Session.h:499-579 | JUCE_WINDOWS | private: getOrCreateSymbolGroup, getOrCreateDebugClient, getOrCreateDebugInterfaces, setDebugInterfaces, loader, outputCallbacks, eventCallbacks, client, control, symbols, dataSpaces, systemObjects, cachedSymbolGroup, cachedFrameIndex, isComOwned |
| Session.h:581-624 | JUCE_MAC | private: cacheFrameVariables, getVariableObject, debugger, listener, target, process, cachedFrameVariables, cachedFrameIndex |
| debug/PrettyPrint.h:4-6 | JUCE_WINDOWS | includes dbgeng.h |
| debug/PrettyPrint.h:8-10 | JUCE_MAC | includes lldb/API/LLDB.h |
| debug/PrettyPrint.h:56-149 | JUCE_WINDOWS | declarations: stripDecimalPrefix, formatSymbolValue, readTargetString, findChildByName, getChildValueText, prettyPrint (dbgeng overload) |
| debug/PrettyPrint.h:151-164 | JUCE_MAC | declaration: prettyPrint(lldb::SBValue&, String) |

Also: State.h, Types.h, Reader.h, Reader.cpp (except lines above), BreakpointManager.h, Whatdbg.h (except 5-8), and ProjectInfo.h have no platform guard.

## T10. Named constants (static constexpr, plus class-scope constexpr) in Source/

| Name | Value | File:line | Scope |
|---|---|---|---|
| sidecarDirName | "whatdbg" | Main.cpp:27 | file static |
| dbgengSubdir | "dbgeng" | Main.cpp:28 | file static |
| liblldbSubdir | "liblldb" | Main.cpp:29 | file static |
| logFileName | "whatdbg.log" | Main.cpp:30 | file static |
| helpText | raw string (help text) | Main.cpp:31-100 | file static |
| reexecMarker | "WHATDBG_REEXEC" | Main.cpp:103 (JUCE_MAC) | file static |
| terminateTimeoutMs | 5000 (std::uint32_t) | Whatdbg.h:35 | class Whatdbg |
| pollTimeoutMs | 50 (std::uint32_t) | Whatdbg.cpp:17 | file static |
| idleSleepMs | 10 (int) | Whatdbg.cpp:18 | file static |
| maxStackFrames | 50 (int) | Whatdbg.cpp:19 | file static |
| symbolTypeSize | 256 (int) | Session.cpp:659 | function static (getSymbolTypeName) |
| symbolValueSize | 512 (int) | Session.cpp:671 | function static (getSymbolValue) |
| symbolNameSize | 256 (int) | Session.cpp:708 | function static (getSymbols) |
| nameBufferSize | 512 (int) | Session.cpp:739 | function static (getStackFrame) |
| fileBufferSize | 1024 (int) | Session.cpp:740 | function static (getStackFrame) |
| msvcThreadNameException | 0x406D1388 (DWORD) | Callbacks.cpp:16 | file static |
| stdioReadBufferSize | 1024 (std::size_t) | Session_mac.cpp:73 | file static |
| allEventsMask | ~0u (std::uint32_t) | Session_mac.cpp:74 | file static |
| isStandardOutput | false | Session_mac.cpp:176 | function static (onProcessEvent) |
| isStandardError | true | Session_mac.cpp:177 | function static (onProcessEvent) |
| millisecondsPerSecond | 1000 (std::uint32_t) | Session_mac.cpp:315 | function static (pollEvents) |
| lineSearchWindow | 4 (std::uint32_t) | BreakpointManager.h:217 | class BreakpointManager |
| fifoCapacity | 64 (int) | Reader.h:23 | class dap::Reader |
| dbgengDecimalPrefix | "0n" | SessionPrettyPrint.cpp:20 | file static |
| maxStringReadSize | 256 (ULONG) | SessionPrettyPrint.cpp:84 | function static (readTargetString) |
| childNameSize | 256 (int) | SessionPrettyPrint.cpp:110 | function static (findChildByName) |
| valueSize | 512 (int) | SessionPrettyPrint.cpp:128 | function static (getChildValueText) |
| ssoThreshold | 16 (int) | SessionPrettyPrint.cpp:190 | function static (getStdStringText) |
| elemTypeSize | 256 (int) | SessionPrettyPrint.cpp:264 | function static (getVectorSize) |
| maxStringReadSize | 256 (std::size_t) | SessionPrettyPrint_mac.cpp:33 | function static (prettyPrintJuceString) |
| projectName, companyName, legalCompanyName, versionString, productWebsite, companyEmail | string literals | ProjectInfo.h:34-40 | namespace ProjectInfo (generated) |
| versionNumber | 0x100 (int) | ProjectInfo.h:38 | namespace ProjectInfo (generated) |

Unnamed literals on timing paths: Reader.cpp:43 `stopThread (2000)`. Session_mac.cpp:340 uses SIGKILL. Callbacks.cpp:16 is named.

State.h, Session.h, Callbacks.h, Types.h, PrettyPrint.h, Loader.h/.cpp declare no static constexpr.

## T11. Timing waits and sleeps in Source/

| Site | Value | Effect |
|---|---|---|
| Whatdbg.cpp:17, 79 | pollTimeoutMs = 50 -> session.pollEvents(50), only while executionState is running or launching | Windows: Session.cpp:218 WaitForEvent(0, 50). macOS: Session_mac.cpp:316-320 rounds to 1 s and calls listener.WaitForEvent(1, event) |
| Whatdbg.cpp:18, 83 | idleSleepMs = 10 -> juce::Thread::sleep(10), when not polling | sleep only in the non-poll branch |
| Whatdbg.h:35; Whatdbg.cpp:449, 253-254, 258 | terminateTimeoutMs = 5000 -> deadline = juce::Time::getMillisecondCounter() + 5000 | checked once per loop pass in drainTerminateTimeout |
| Reader.cpp:43 | stopThread(2000) | ~Reader waits at most 2000 ms for the reader thread |
| Reader.cpp:65 | std::getline(std::cin) | blocking read, no timeout |
| Reader.cpp:92 | std::cin.read(body, contentLength) | blocking read, no timeout |
| Session.cpp:218 | control->WaitForEvent(0, timeoutMs) | Windows timed wait (timeoutMs from pollEvents) |
| Session_mac.cpp:320 | listener.WaitForEvent(timeoutSeconds, event) | macOS timed wait, whole seconds |
| Whatdbg.cpp:254, 449 | juce::Time::getMillisecondCounter() | reads only, no wait |

No other sleep, WaitableEvent, or wait is present in Source/ (all Source files read). BreakpointManager.h:217 lineSearchWindow is a line count, not a time.

## T12. SPEC.md features and tests/smoke

SPEC.md feature sections (header line; user-flow lines; per-feature acceptance is the User Flow ending step, and Success Criteria at SPEC.md:399-434):

| # | Feature (SPEC.md line) | User flow lines | Acceptance text (verbatim, happy-path end step) |
|---|---|---|---|
| 1 | Launch Mode (38) | Windows 40-50; macOS 52-61 | Windows 50: "10. DAW loads normally, user works with it"; macOS 61: "9. The program runs normally, user works with it" |
| 2 | Attach Mode (77) | 79-85 | 85: "6. User continues from configurationDone" |
| 3 | Breakpoints (93) | 95-102 | 99: "4. If resolved: response has `verified: true`"; 100: "5. If not resolved (module not loaded yet): the registry entry keeps an empty `engineId`, response has `verified: false`" |
| 4 | Stepping (127) | Windows 129-135; macOS 137-142 | 135: "6. DAP `stopped` event emitted with `reason: \"step\"`"; 142: "5. DAP `stopped` event emitted with `reason: \"step\"`" |
| 5 | Pause (153) | Windows 155-163; macOS 165-171 | 161: "6. DAP `stopped` event emitted with `reason: \"pause\"`"; 170: "5. DAP `stopped` event emitted with `reason: \"pause\"`" |
| 6 | Variable Inspection (185) | 187-195 | 195: "8. Variables displayed with name, value, type, expand triangle for composite types" |
| 7 | Expression Evaluation (243) | Windows 245-253 (end 252); macOS 255-260 | 260: "5. DAP evaluate response with formatted result"; method calls not supported on Windows (271-273) |
| 8 | Debuggee Output Capture (285) | Windows 287-292; macOS 294-299 | 299: "5. Message appears in nvim-dap REPL panel" |
| 9 | Multi-Thread Support (312) | 314-326 | 326: "Stopped events use `getEventThreadSystemId()` for the real event thread's OS TID" |
| 10 | Terminate / Disconnect (330) | 332-355 | 333: "`terminate` command, or `disconnect` with `terminateDebuggee: true` — kills the target process" |
| 11 | Target Crash / Exception Info Surfacing (359) | 361-382 | 369-372: stopped reason exception, output stderr, stopReason set to none, exceptionCode and exceptionAddress preserved |

Success Criteria (SPEC.md:399-434). Checked items: launch/attach (402), breakpoints before/after load (403), hit + stack (404), step next/stepIn/stepOut (405), pause (406), locals (407), struct expansion (408), juce::String / std::string / unique_ptr (409), evaluate (410), output (411), threads (412), terminate no zombie macOS (413), continue after pause (414), disconnect detaches (415), macOS compiles (420), BLESSED (421), header doxygen (422-426), debug logging (427), module-load storms (428), symbol group cache (429-430), smoke tests (431-434). Unchecked: "Windows: compiles with MSVC ... has not been compiled; MSVC compilation is unverified as of this document" (418-419).

SPEC.md:377 says supportsExceptionInfoRequest is advertised in Source/dap/Types.h (confirmed, Types.h:164).

tests/smoke/run_smoke.lua (entry, read in full): usage `<whatdbg> <fixture> <fixture_wait> <fixture.cpp> <fixture_crash>` (14); exitTimeoutMs = 10000 (16); breakpoint marker "SMOKE_BREAKPOINT_LINE" (17); run order (39-50), then SUMMARY pass/fail (70).

The ten scenarios (Report.new name, function, file:line, what it checks):

| # | Report name | Function (file:line) | What it checks |
|---|---|---|---|
| 1 | 01_launch_bp_continue | scenarioLaunchBpContinue (scenario_breakpoint.lua:36-64) | initialize, initialized, launch, setBreakpoints, configurationDone; stopped reason "breakpoint"; threads >= 1; stackTrace >= 1 frame; continue; exited and terminated events within 10000 ms |
| 2 | 02_attach | scenarioAttach (scenario_process.lua:24-58) | spawns fixture_wait; attach(pid); configurationDone; thread event reason "started"; terminate |
| 3 | 03_step | scenarioStep (scenario_breakpoint.lua:66-101) | stop at breakpoint; next; stopped reason "step"; continue; exited (logs post-continue message log on failure) |
| 4 | 04_pause | scenarioPause (scenario_process.lua:60-84) | launch fixture_wait (no breakpoint); thread started; pause; stopped reason "pause"; terminate |
| 5 | 05_variables | scenarioVariables (scenario_breakpoint.lua:113-155) | stackTrace; scopes; variables: "name" contains "whatdbg"; "counter" contains "42"; "numbers" contains "size=" |
| 6 | 06_evaluate | scenarioEvaluate (scenario_breakpoint.lua:157-182) | evaluate "1 + 2" result contains "3"; evaluate "*counterPtr" result contains "42" |
| 7 | 07_output | scenarioOutput (scenario_process.lua:86-118) | exited; output events contain "BREAKPOINT_TARGET_A" (stdout) and "BREAKPOINT_TARGET_B" (stderr) |
| 8 | 08_crash | scenarioCrash (scenario_process.lua:120-155) | launch fixture_crash; stopped reason "exception"; exceptionInfo success; exceptionId contains "EXC_BAD_ACCESS"; breakMode "unhandled"; terminate |
| 9 | 09_terminate_no_zombie | scenarioTerminateNoZombie (scenario_terminate.lua:27-62) | stop at breakpoint; debuggee pid found (pgrep); terminate success; pid gone within 5000 ms; ps STAT has no Z; zombie count did not rise |
| 10 | 10_disconnect_detach | scenarioDisconnectDetach (scenario_process.lua:157-182) | stop at breakpoint; disconnect success; debuggee pid still alive after 500 ms (detach, not kill); then the test kills it with kill -9 |

Helper functions: runToStoppedBreakpoint (scenario_breakpoint.lua:5-34); runToLaunched (scenario_process.lua:7-22); findDebuggeePid and countZombies (scenario_terminate.lua:6-25); ScenarioRunner.runScenario (scenario_runner.lua:3-11) wraps scenario bodies in pcall.

## T14. Identifier lexicon

- cast/identifiers.md: does not exist (cast/ listing: .cast-format, cmake.cast, installer/, installer.cast, signing.md, spell.md).
- Source/generated/: ProjectInfo.h only (43 lines). No Identifiers.h.
- cast/spell.md output rows (read in full): @cmake:cmake -> CMakeLists; project info; signing; release; debug; pack; installer; @Distribution; @Installer; @Gh; @Release; @Entitlements; @ProjectInfo.

String literal census. Command: grep -ohE '"[A-Za-z][A-Za-z0-9_]*"' over Source/Whatdbg.cpp, Source/dap/Types.h, Source/debug/BreakpointManager.cpp, sorted and counted. WhatdbgHandlers.cpp is absent. Strings containing spaces or punctuation are not in this count (for example "Content-Length: ", "Unknown command", "[Whatdbg] ..." log text).

Counts, verbatim (count, literal):
```
  21 "seq"
   8 "arguments"
   6 "type"
   6 "reason"
   5 "threadId"
   5 "output"
   4 "variablesReference"
   4 "stopped"
   4 "name"
   4 "launch"
   4 "command"
   4 "attach"
   3 "variables"
   3 "threads"
   3 "scopes"
   3 "pause"
   3 "line"
   3 "initialize"
   3 "evaluate"
   3 "breakpoint"
   3 "allThreadsStopped"
   2 "verified"
   2 "value"
   2 "terminated"
   2 "success"
   2 "stepOut"
   2 "stepIn"
   2 "step"
   2 "stackTrace"
   2 "source"
   2 "setBreakpoints"
   2 "response"
   2 "request_seq"
   2 "path"
   2 "next"
   2 "message"
   2 "id"
   2 "frameId"
   2 "exceptionInfo"
   2 "event"
   2 "description"
   2 "cwd"
   2 "continue"
   2 "configurationDone"
   2 "changed"
   2 "category"
   2 "breakpoints"
   2 "body"
   1 "unhandled"
   1 "totalFrames"
   1 "thread"
   1 "text"
   1 "terminateDebuggee"
   1 "terminate"
   1 "systemProcessId"
   1 "symbolIndex"
   1 "supportsValueFormattingOptions"
   1 "supportsTerminateThreadsRequest"
   1 "supportsTerminateRequest"
   1 "supportsSteppingGranularity"
   1 "supportsStepInTargetsRequest"
   1 "supportsStepBack"
   1 "supportsSetVariable"
   1 "supportsSetExpression"
   1 "supportsRestartFrame"
   1 "supportsReadMemoryRequest"
   1 "supportsModulesRequest"
   1 "supportsLogPoints"
   1 "supportsLoadedSourcesRequest"
   1 "supportsInstructionBreakpoints"
   1 "supportsHitConditionalBreakpoints"
   1 "supportsGotoTargetsRequest"
   1 "supportsFunctionBreakpoints"
   1 "supportsExceptionOptions"
   1 "supportsExceptionInfoRequest"
   1 "supportsExceptionFilterOptions"
   1 "supportsEvaluateForHovers"
   1 "supportsDisassembleRequest"
   1 "supportsDelayedStackTraceLoading"
   1 "supportsDataBreakpoints"
   1 "supportsConfigurationDoneRequest"
   1 "supportsConditionalBreakpoints"
   1 "supportsCompletionsRequest"
   1 "supportsClipboardContext"
   1 "supportsCancelRequest"
   1 "supportsBreakpointLocationsRequest"
   1 "supportTerminateDebuggee"
   1 "stderr"
   1 "started"
   1 "startMethod"
   1 "stackFrames"
   1 "running"
   1 "result"
   1 "request"
   1 "program"
   1 "process"
   1 "pid"
   1 "isLocalProcess"
   1 "initialized"
   1 "hitBreakpointIds"
   1 "hasChildren"
   1 "expression"
   1 "expensive"
   1 "exited"
   1 "exitCode"
   1 "exceptionId"
   1 "exception"
   1 "disconnect"
   1 "breakMode"
   1 "allThreadsContinued"
   1 "Locals"
```

Classification of the repeated literals (by the code site, facts only):
- DAP event name literal sites: "stopped" (Whatdbg.cpp:740, 762; BreakpointManager.cpp:159), "output" (Whatdbg.cpp:205, 772), "exited" (Whatdbg.cpp:241), "terminated" (243), "thread" (296), "process" (Whatdbg.cpp:785), "initialized" (338), "breakpoint" (Types.h:133, reached from BreakpointManager.cpp:371 through getBreakpointChangedEvent).
- DAP command names used as handler keys (Whatdbg.cpp:26-43) and as getResponse command argument: initialize, launch, attach, configurationDone, disconnect, terminate, setBreakpoints, threads, stackTrace, scopes, variables, continue, next, stepIn, stepOut, pause, evaluate, exceptionInfo.
- Reason values: "breakpoint", "step", "pause", "exception", "started", "changed", "launch", "attach" (startMethod), "stderr", "stdout"/"console" (category, from State and drains), "unhandled" (breakMode).
- Capability keys: 31 names in Types.h:150-180 (listed above).
- Body/field name literal sites, examples: "reason" (Whatdbg.cpp:294, 736, 756; BreakpointManager.cpp:151, 156; Types.h:130), "threadId" (Whatdbg.cpp:295, 737, 759; BreakpointManager.cpp:134), "allThreadsStopped" (BreakpointManager.cpp:135; Whatdbg.cpp:738, 760), "variablesReference" (Whatdbg.cpp:553, 620, 722), "name" (Whatdbg.cpp:552, 617, 780), "line" (BreakpointManager.cpp:271; Types.h:127), "source" and "path" (BreakpointManager.cpp:282-283), "id" and "verified" (Types.h:125-126; BreakpointManager.cpp:269-270).
- Strings in the pattern not counted above (contain spaces or punctuation): "Content-Length: ", "Unknown command", "Failed to launch process", "Failed to attach", "Could not evaluate: ", "pid ", "Unhandled exception: ", "0x", "WHATDBG: pending - module not loaded for ", "Thread ", "[Whatdbg] ..." (JUCE_DEBUG logs).

Strings present in the census that appear only as log content are not separated out; the census is by regex, not by call site.

## T15. Literals of the debug layer that the T14 census does not hold

The T14 census reads three files. These literals are in the other files of `Source/` (string-literal searches of 2026-11-12).

| Literal | Site | Use |
|---|---|---|
| `"column"` | `Session.cpp:770`; `Session_mac.cpp:699` | DAP property name of a stack frame, value 1 |
| `"stdout"`, `"stderr"` | `Session_mac.cpp:93` | DAP `output` category values |
| `"console"` | `State.h:275` | DAP `output` category value, the default of `debuggeeOutputCategory` |
| `"frame"` | `Session.cpp:744` | A default frame name. It is a display text, not a DAP word. |

The other DAP literals of `Session.cpp` and `Session_mac.cpp` (`name`, `id`, `value`, `type`, `symbolIndex`, `source`, `path`, `line`, `hasChildren`) are in T14.
Two literals of the T14 census are not DAP words: `"running"` is in a comment (`Whatdbg.cpp:444`), and `"Locals"` is a display text (`Whatdbg.cpp:552`).

## Documentation versus code (facts only)

- Main.cpp:13 and CLAUDE.md "Sidecar path": Windows sidecar `~/.config/whatdbg/dbgeng/`. Code: Main.cpp:108 uses userApplicationDataDirectory, then "whatdbg" (109), then "dbgeng" (28, 187).
- Session.h:398 says getLocals entries carry "hasChildren/variablesReference". Session.cpp:699-700 and Session_mac.cpp:670-671 set hasChildren and symbolIndex; variablesReference is set by Whatdbg.cpp:616-620.
- SPEC.md:418-419 states the Windows build has not been compiled with MSVC.
- SPEC.md Architecture Constraints (386-395) say "No magic numbers". Reader.cpp:43 uses the unnamed literal 2000.
- BreakpointManager.h:114-117 names the caller as "Whatdbg::drainBreakpointLocationResolved"; Whatdbg.cpp:182-193 is that drain.

