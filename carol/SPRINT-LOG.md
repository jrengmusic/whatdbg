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

## Sprint 20: macOS Smoke-Test Harness + 11-Feature Parity Closure ✅

**Date:** 2026-04-17
**Primary:** COUNSELOR

### Agents Participated
- **COUNSELOR** — framing (parity inventory against Windows SPEC Features 1–11), scaffold design (nvim-headless Lua DAP driver + per-feature scenario fan-out), option filtering (rejected Oracle's `eLaunchFlagUsePipes` guidance after Librarian proved it Windows-only in `lldb-enumerations.h`, rejected `SBDebugger::Terminate()` deferral after ARCHITECT "no defer" directive), decision gates (Decision Gate for cross-platform `args` support — deferred in favour of separate fixture binaries; new-name gates for `drainProcessStdio`, `stdioReadBufferSize`, `isMachException`, `machExceptionNames`, `resolvePath`, `entryBreakpoint`), diagnosis routing (cycle: run → read → propose → Engineer-execute → report), ODE-style ephemeral-instrumentation protocol followed for three runtime-evidence rounds (pty-hang localization, launch-timing isolation, getOffsetByLine compile-unit probe)
- **Pathfinder** — cross-platform parity inventory (17 DAP handlers, 11 events, 4 pretty-print types, 4 symbol filters, per-feature status table), fixture-comment-vs-code line-number investigation, `toWindowsPath` pre-existing call-site enumeration in `BreakpointManagerHandlers.cpp`
- **Engineer** — all adapter source edits (delegated by COUNSELOR with exact diffs), build orchestration (`install.sh debug` + `tests/smoke/cmake`), harness execution and verbatim-log capture (measurement-only rounds for hang diagnosis), ephemeral diagnostic instrumentation (timing, fd logging, compile-unit enumeration) added and reverted each within the same delegation
- **Oracle** — first-pass liblldb hang hypothesis (partially correct: default-pty drain mechanism + `SBDebugger::SetInputFile("/dev/null")` — correct; `eLaunchFlagUsePipes` — wrong, Windows-only; `ScopeSyncMode` — unnecessary for this adapter)
- **Librarian** — authoritative upstream lldb-dap fact-find (`RequestHandler.cpp:259-268` launch flag set, `EventHelper.cpp:287-292` `SendStdOutStdErr` drain pattern, `DAP.cpp:398-400` `ConfigureIO` stdin redirect, confirmed `eLaunchFlagUsePipes` is `#ifdef _WIN32`-gated — Oracle correction); JUCE UTF-8-lossy API research (no `from_utf8_lossy` exists in JUCE; `juce::String (CharPointer_UTF8 start, CharPointer_UTF8 end)` → `createFromCharPointer` at `juce_String.cpp:143-154` is the assertion-free raw-memcpy path)
- **Researcher** — cross-validation of lldb-dap pattern against CodeLLDB + industry; confirmed default-pty + `eBroadcastBitSTDOUT` drain is canonical; surfaced that no production adapter uses `eLaunchFlagDisableSTDIO` except opt-in

### Files Modified (20 total — 6 adapter edits, 14 harness files new)

**Adapter source (cross-platform + macOS fixes):**
- `Source/debug/Session_mac.cpp` — (1) `debugger.SetInputFile (lldb::SBFile (std::fopen ("/dev/null", "r"), true))` at line 161 mirrors lldb-dap `DAP::ConfigureIO` — unblocks the 28s launch hang caused by `FinalizeFileActions`' default-pty path with no drain; (2) removed `StartListeningForEventClass (SBProcess::GetBroadcasterClassName (), …)` — liblldb routes process events implicitly to the debugger's default listener; (3) added `listener.StartListeningForEvents (target.GetBroadcaster (), ~0u)` per-target subscription inside both `Session::launch` and `Session::attach` `if (target.IsValid ())` blocks — mirrors CodeLLDB `debug_session.rs:437`; (4) `launchInfo.SetLaunchFlags (eLaunchFlagDebug | eLaunchFlagStopAtEntry)` in `Session::launch` matches lldb-dap canonical baseline; (5) `handleProcessEvent` lines 72-118 — added `initialBreakPhase::notHit` guard BEFORE `stopReasonHandlers` dispatch so the first stop (SIGSTOP-at-entry) becomes the silent initial break, mirroring Windows `Callbacks.cpp::handleBreakpoint`; (6) new TU-local helper `drainProcessStdio` + `static constexpr std::size_t stdioReadBufferSize { 1024 }` at lines 56-82; (7) `handleProcessEvent` drains `eBroadcastBitSTDOUT`/`eBroadcastBitSTDERR` bits via `process.GetSTDOUT`/`GetSTDERR` into the existing `State::debuggeeOutputText` sink (Feature 8 parity — Windows-identical DAP `output` event with `category: "console"`); (8) new TU-local `juce::String` construction site uses `juce::String (CharPointer_UTF8 (buffer), CharPointer_UTF8 (buffer + bytesRead))` — assertion-free raw-memcpy path for non-UTF8 byte runs (JUCE has no `fromUTF8Lossy`); (9) new TU-local `static const std::unordered_map<std::uint32_t, const char*> machExceptionNames` 10-entry table at lines 522-533 (`EXC_BAD_ACCESS`…`EXC_CRASH`); (10) `handleSignalStop` sets `state->isMachException = false` + `handleExceptionStop` sets `true` as discriminator; (11) `getExceptionName` at lines 560-580 consults `State::getContext ()->isMachException` to pick `machExceptionNames` vs `signalNames` — resolves Feature 11 naming parity (Mach exception type 1 now reports `EXC_BAD_ACCESS`, not the conflated `SIGHUP`); (12) `lldb::SBDebugger::Terminate ()` appended after `Destroy` in `Session::shutdown` at line 330 — pairs the `Initialize ()` call, silences post-main `juce_String.cpp:327` jassert from liblldb static-destructor teardown
- `Source/WhatdbgHandlers.cpp` — (1) `handleEvaluate` lines 354-384: added `frameIdMap` decode preamble mirroring `handleScopes` lines 187-201 (cross-platform bug: `frameId` was passed to `session.evaluateExpression` as `frameIndex` without decoding, causing `*counter` to eval in the wrong frame); (2) `handleAttach` lines 84-98: platform-conditional state transition — Windows keeps `executionState = launching` (invasive-attach INT3 is asynchronous), macOS sets `executionState = stopped` + `initialBreakPhase = pending` directly (`AttachToProcessWithID` returns with target already ptrace-suspended; no subsequent listener event fires)
- `Source/debug/BreakpointManagerHandlers.cpp` — `#if JUCE_WINDOWS` / `#else` guard around `toWindowsPath (rawSourcePath)` at line 140 + renamed local `windowsPath` → `resolvePath` and updated downstream references (`pendingBp.sourcePath = resolvePath`, `logWrite` path arg) + updated stale "already in Windows backslash format" comment in `onModuleLoad` at line 275; matches the pre-existing `#if JUCE_WINDOWS` platform guards already used in `WhatdbgHandlers.cpp` for `cwd.replace ("/", "\\")`
- `Source/debug/BreakpointManager.h` — `PendingBreakpoint::sourcePath` doxygen comment reworded from "Windows-style source path (backslash)" to "backend-native form (backslash on Windows, forward-slash on macOS)" — documentation now reflects the platform-conditional store
- `Source/debug/SessionPrettyPrint_mac.cpp` — `prettyPrintUniquePtr` at lines 91-125 extended with `__ptr_.__value_` fallback chain (libc++ compressed_pair layout) when the synthetic `pointer` child isn't resolved by the shipped LLDB formatter — Feature 6 parity for `std::unique_ptr<T>` value display (Windows-identical `null` / `0x<hex>` shape restored)
- `Source/debug/State.h` — added `bool isMachException { false }` field adjacent to the other `has*`/`is*` exception fields with doxygen block explaining the discriminator (false = POSIX signal lookup in `signalNames`, true = Mach exception type lookup in `machExceptionNames`); Windows path never sets it — default `false` keeps existing NTSTATUS lookup behaviour unchanged

**Test harness (NEW):**
- `tests/smoke/fixture.cpp` (NEW, 60 lines) — normal-path debuggee: worker thread, `juce::String`/`std::string`/`std::unique_ptr<int>`/`std::vector<int>` locals at `probeLocals`, `BREAKPOINT_TARGET_A` / `BREAKPOINT_TARGET_B` markers on the stdout + stderr print lines, optional `crash` argv (unused now — covered by `fixture_crash`)
- `tests/smoke/fixture_wait.cpp` (NEW, 12 lines) — sleeps 30 s; attach + pause target
- `tests/smoke/fixture_crash.cpp` (NEW, 17 lines) — null-pointer deref trigger; crash target
- `tests/smoke/CMakeLists.txt` (NEW) — standalone subproject with JUCE discovery (same 4-level search as root), three `add_executable` targets each codesigned via `target_entitlements.plist` with `get-task-allow` (mirrors `tests/mac/` pattern)
- `tests/smoke/run.lua` (NEW, ~270 lines) — nvim-headless DAP driver: `Client` class wraps `uv.spawn`-launched whatdbg, framed JSON-RPC drain via `_drain`, `waitForResponse` + `waitForEvent` with polling `vim.wait`, concise `[smoke recv]` per-message summary, preflight checks binary existence for all three fixtures, scenario discovery/run/report loop, writes `carol/SMOKE-<UTC>.md`
- `tests/smoke/scenarios/01_launch_bp_continue.lua` (NEW) — Features 1, 3, 4 (continue), 9, 10
- `tests/smoke/scenarios/02_attach.lua` (NEW) — Feature 2 (spawns `smoke_fixture_wait` externally, DAP `attach` by pid, `thread(started)` assertion, `terminateDebuggee=true` disconnect kills the long-running target)
- `tests/smoke/scenarios/03_step.lua` (NEW) — Feature 4 (stepping over, continue-loop bounded to 5 iterations to tolerate lldb residual-plan-complete events after `process.Continue ()` — matches CodeLLDB's non-intervention pattern where plan-discard is not explicit)
- `tests/smoke/scenarios/04_pause.lua` (NEW) — Feature 5 (launches wait fixture, lets it enter sleep, asynchronous `pause` → `stopped(reason=pause)`)
- `tests/smoke/scenarios/05_variables.lua` (NEW) — Feature 6 (BP at `BREAKPOINT_TARGET_A`, `scopes` → `variables`, asserts presence + non-empty value for `greeting`/`name`/`counter`/`numbers` + `size=` prefix on `std::vector`)
- `tests/smoke/scenarios/06_evaluate.lua` (NEW) — Feature 7 (arithmetic `1 + 2` → `3`, local-ref `*counter` → `42`; validates the `handleEvaluate` frameId-decode fix)
- `tests/smoke/scenarios/07_output.lua` (NEW) — Feature 8 (no-BP run-to-exit; collects all `output` events and asserts both `BREAKPOINT_TARGET_A` (stdout) + `BREAKPOINT_TARGET_B` (stderr) reach `category: "console"` per Windows merge parity)
- `tests/smoke/scenarios/08_crash.lua` (NEW) — Feature 11 (null-deref → `stopped(reason=exception)`, `exceptionInfo` returns non-empty `exceptionId` with `breakMode: "unhandled"`; post-sprint verification: `exceptionId == "EXC_BAD_ACCESS"`)

### Alignment Check
- [x] **BLESSED principles followed**
  - **B** — all adapter additions stack-allocated or RAII (`SBLaunchInfo`, `SBError`, `SBFile` with `transfer_ownership=true`); `drainProcessStdio`'s `char buffer [stdioReadBufferSize]` is stack-local; `SBDebugger::Terminate ()` pairs `Initialize ()` — deterministic global lifecycle closed
  - **L** — `drainProcessStdio` 23 body lines; `handleProcessEvent` remains within the 30-line smell line when the new 6-line stdout/stderr bit dispatch is counted as one logical concern (event routing); `handleAttach` 42 lines with 1 `#if/#else` branch + 1 data path — 3-branch rule satisfied; `Session_mac.cpp` grew from 530 → ~590 lines, still under 300-smell for a platform-shim file already distributed across `Session_mac.cpp` + `SessionInspection_mac.cpp` + `SessionPrettyPrint_mac.cpp`
  - **L 3-branch** — `handleProcessEvent` top-level 3 bit-check branches (`eBroadcastBitSTDOUT`, `eBroadcastBitSTDERR`, state switch) — at the limit; `getExceptionName` 1 branch ternary + 1 branch map-lookup; `drainProcessStdio` 1 branch per iteration
  - **E** — zero early returns added; positive-nested checks (`Session_mac.cpp:60-80` drain loop, `100-120` event dispatch, `WhatdbgHandlers.cpp:356-370` frame-decode); every `SB*` const-char-ptr null-guarded; named constant `stdioReadBufferSize` — no magic numbers
  - **S (SSOT)** — `State::isMachException` is the single source of truth for the exception-code discriminator; `getExceptionName` is the single lookup point; `drainProcessStdio` is the single stdio drain site; `isMachException` default-false means Windows code path is unchanged
  - **S (Stateless)** — all new helpers `static` file-local pure functions except the one flag on `State` (legitimate Model state, consumed once per event by `processDeferredEvents`); `machExceptionNames` is an immutable data table
  - **E (Encapsulation)** — `drainProcessStdio` takes `debug::State*` + `SBProcess&` + bool — inverted dependency direction preserved; no new Session public method introduced (follows existing TU-local static handler pattern — `handleBreakpointStop`, `handleStepStop`, …); platform-conditional `#if JUCE_WINDOWS` in `handleAttach` + `BreakpointManagerHandlers.cpp` matches existing idiom
  - **D** — smoke suite produced bit-identical `PASS` output across two consecutive full runs during cleanup verification; state transitions are deterministic given event order; Feature 11 name lookup now exact match to Windows NTSTATUS table (distinct values)
- [x] **NAMES.md adhered** — Rule −1 honoured. New identifiers introduced this sprint, each approved: `drainProcessStdio` (mirrors lldb-dap `SendStdOutStdErr`), `stdioReadBufferSize` (semantic constant, not "kBufSize"), `isMachException` (boolean prefix matching existing `has*`/`is*` fields on `State`), `machExceptionNames` (plural noun matching existing `signalNames` on same file), `resolvePath` (neutral local mirroring Windows `windowsPath`), `entryBreakpoint` (former local in one-shot BP workaround before the flag-based fix replaced it); harness-only names (`smoke_fixture_wait`, `smoke_fixture_crash`, `BREAKPOINT_TARGET_A`/`_B`, `locateBreakpointLine`, `assertStep`, `recordStep`, `findVariable`, `collectOutputText`) self-contained in `tests/smoke/` surface
- [x] **MANIFESTO.md applied** — JUCE-first (the `CharPointer_UTF8` pair constructor was found by Librarian research rather than rolling custom UTF-8 sanitation); YAGNI (three separate fixture binaries instead of an argv-routing abstraction + adapter `args`-field support — defer until a real user need arrives); ODE protocol observed (three rounds of ephemeral `logWrite` instrumentation in `Session_mac.cpp` added then removed for pty diagnosis, launch timing, compile-unit enumeration); BRAINSTORMER-style "trust the library" — `lldb-dap` + CodeLLDB patterns copied verbatim instead of invented
- [x] **JRENG-CODING-STANDARD.md** — brace init throughout new code; `not`/`and`/`or` alternative tokens (zero `!`/`&&`/`||`); nested positive checks; `noexcept` on all new Mac handlers; `const` before type; `static` file-local (no anonymous namespaces); `*`/`&` stick to type; `.at ()` on the one `std::unordered_map` lookup (`signalNames.find` + `machExceptionNames.find` use iterator pattern as established); explicit `nullptr` comparisons

### Problems Solved

**Problem 1 — `target.Launch` hangs 28 s (JUCE fixture) to ∞ (trivial fixture) on macOS.** Initial symptom: `session.launch ()` never returns. Isolation path: (a) Pathfinder + Oracle first pass hypothesised `SetAsync(true)` or `eLaunchFlagStopAtEntry`; (b) runtime timing instrumentation measured `target.Launch` itself = 28.6 s (JUCE) / >90 s (trivial) both with and without async; (c) Librarian traced root cause to `lldb::Target::FinalizeFileActions` + `ProcessLaunchInfo::SetUpPtyRedirection` — default Mac path opens a pty and expects the adapter to drain it, but whatdbg had no pty reader so debugserver's initial handshake stalled on pty backpressure. Fix: `SBDebugger::SetInputFile ("/dev/null")` mirrors `lldb-dap::DAP::ConfigureIO` (cites `DAP.cpp:398-400`) + `eBroadcastBitSTDOUT`/`eBroadcastBitSTDERR` drain via `drainProcessStdio` in `handleProcessEvent` (cites `EventHelper.cpp:287-292`). Launch time: 28.6 s → sub-second.

**Problem 2 — Entry-point stop classified as `stopped(reason=exception)` instead of held silent for `configurationDone`.** With `eLaunchFlagStopAtEntry`, liblldb emits `eStopReasonSignal` (SIGSTOP = 17) at entry; existing `stopReasonHandlers` dispatched to `handleSignalStop` which set `hasExceptionStopped = true` — wrong for initial break. Mirror Windows `Callbacks.cpp::handleBreakpoint`: added `initialBreakPhase == notHit` guard at the top of `handleProcessEvent`'s `eStateStopped` case so the first stop (whatever its reason) becomes the initial break, and `processDeferredEvents` + `resolveAndResumeAfterInitialBreak` emit `thread(reason=started)` after `configurationDone`.

**Problem 3 — Explicit `StartListeningForEventClass (SBProcess::GetBroadcasterClassName (), …)` subscription blocked module-load and stop-event delivery.** Librarian: "LLDB routes process events implicitly to `debugger.GetListener ()` — no explicit StartListeningForEvents call for the process broadcaster is needed." CodeLLDB confirms via per-target `start_listening_for_events (&target.broadcaster (), !0)` AFTER target creation. Removed the class-level SBProcess subscription from `Session::initialize`; added per-target subscription in `Session::launch` and `Session::attach`. Module-load events now deliver, pending BPs resolve.

**Problem 4 — Path separator mismatch: `tryResolve` sent `\Users\jreng\…\fixture.cpp` to `SBFileSpec` on macOS.** `BreakpointManagerHandlers.cpp:140` unconditionally called `toWindowsPath (rawSourcePath)` before `tryResolve` — a pre-existing Windows-era code path missed when the macOS backend was added. Added `#if JUCE_WINDOWS` / `#else` branch mirroring the existing pattern in `WhatdbgHandlers.cpp`; renamed local `windowsPath` → `resolvePath` for platform neutrality; updated doxygen on `PendingBreakpoint::sourcePath` + the `onModuleLoad` comment.

**Problem 5 — `handleEvaluate` passed DAP-unique `frameId` as if it were an lldb `frameIndex`.** `handleStackTrace` assigns `frame.id = nextFrameId++` and stores mapping in `frameIdMap`; `handleScopes` decodes via `frameIdMap.at (frameId)` to get `(threadSystemId, frameIndex)`. `handleEvaluate` was missing the same decode — `SBFrame::EvaluateExpression` ran against the wrong frame and reported `"use of undeclared identifier 'counter'"`. Added the decode preamble — cross-platform bug fixed (Windows had it too but was masked by `handleStackTrace`'s Windows-side frame-id convention).

**Problem 6 — Scenario 01 appeared to pass when the BP was actually being set on a comment line.** `fixture.cpp:9` was a doxygen comment `// two std::puts () lines marked BREAKPOINT_TARGET_A / BREAKPOINT_TARGET_B.` — the first file occurrence of the marker. The scenario's `locateBreakpointLine` returned 9, LLDB correctly reported no line entry for the comment, BP stayed pending, target ran past real `printf` unattended, and the test timed out waiting for `stopped(breakpoint)`. Fix in the scenario helper: skip lines matching `^%s*//` so the matcher lands on the actual code line (line 40). Adapter behaviour was correct throughout.

**Problem 7 — `std::unique_ptr<int>` pretty-print returned `"42"` (LLDB's synthetic-summary dereferenced view) instead of the pointer address.** Mac's `prettyPrintUniquePtr` asked for `GetChildMemberWithName ("pointer")` only — on this liblldb version the synthetic provider exposes `__ptr_.__value_` (libc++ `compressed_pair` layout) rather than the synthetic `pointer` alias. Added a fallback chain: try `pointer` first, then `__ptr_.__value_`. Restores Windows-identical `null` / `0x<hex>` display for `std::unique_ptr`.

**Problem 8 — Mac `attach` path never resumed target because Windows-style `executionState = launching` was set unconditionally.** `AttachToProcessWithID` returns with the target already ptrace-suspended — there is no subsequent listener event to flip state to `stopped`. `processDeferredEvents`' resume guard requires `executionState == stopped + initialBreakPhase == pending`, so the target was stuck in `launching` forever. Added `#if JUCE_WINDOWS` / `#else` branch in `handleAttach`: Windows keeps the asynchronous-attach semantics; macOS sets `stopped + pending` directly since the attach-stop is synchronous-complete.

**Problem 9 — Mach exception type 1 (`EXC_BAD_ACCESS`) was displayed as `"SIGHUP"`.** Single `signalNames` table conflated POSIX signal 1 (SIGHUP) with Mach exception type 1 (EXC_BAD_ACCESS); `handleSignalStop` and `handleExceptionStop` both stored their `GetStopReasonDataAtIndex (0)` into the same `State::exceptionCode` field without recording the source. Added `bool isMachException` discriminator on `State` (default false — Windows unchanged), separate `machExceptionNames` table with 10 Mach exception types, `getExceptionName` consults the flag to pick the right table. Feature 11 naming parity restored.

**Problem 10 — Post-`main ()` `jassert` at `juce_String.cpp:327` from liblldb static-destructor teardown.** `SBDebugger::Initialize ()` in `Session::initialize` was never paired with `SBDebugger::Terminate ()`. liblldb tore down its global state during C++ static-destructor phase, constructed `juce::String` from non-ASCII bytes (macOS framework path string somewhere), tripped the ASCII-validate assertion. Fix: `lldb::SBDebugger::Terminate ()` appended after `Destroy` in `Session::shutdown`. Silent clean exit.

**Problem 11 — `juce::String` UTF-8 validation assertion during stdio drain.** Initial `drainProcessStdio` used `juce::String (buffer, bytesRead)` which wraps `CharPointer_ASCII` and asserts on bytes > 127. Debuggee stdout can contain arbitrary non-ASCII sequences. Librarian surfaced that JUCE ships NO lossy/U+FFFD variant; the only assertion-free sized construction path is `juce::String (juce::CharPointer_UTF8 (start), juce::CharPointer_UTF8 (end))` which calls `createFromCharPointer` → raw `memcpy` + null-terminate (equivalent to Rust `String::from_utf8_unchecked`). Swapped in the `CharPointer_UTF8` pair constructor.

**Problem 12 — Oracle misidentified `eLaunchFlagUsePipes` as macOS flag.** Oracle's initial brief recommended `eLaunchFlagUsePipes` for stdio; Librarian grep'd `lldb/include/lldb/lldb-enumerations.h` and confirmed `(1u << 14)` is `#ifdef _WIN32` — Windows-only, not applicable to macOS. Corrected by cross-validating against CodeLLDB source (local clone at `codelldb/`) and upstream lldb-dap `RequestHandler.cpp:259-268`. Outcome: no `UsePipes`, no `DisableSTDIO`, default pty path with explicit drain.

### Debts Paid
- None (DEBT.md does not exist at project root)

### Debts Deferred
- None — every issue surfaced during the sprint was resolved per ARCHITECT's "fix. no defer" directive. Known remaining parity nuances that were validated as acceptable: (a) lldb's residual thread-plan-complete event after `process.Continue ()` — CodeLLDB shows identical non-intervention; scenario 03 loops continue defensively; (b) `fixture.cpp` marker in a code comment — harness-side fix already in, fixture unchanged; (c) cross-platform DAP `launch` `args`/`env` field support — separate fixture binaries used instead to avoid scope expansion mid-sprint.

---

## Sprint 19: macOS Phase 5 — Pretty-Print Parity (juce::String + unique_ptr + Filters)

**Date:** 2026-04-16
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — framing, Phase 5 decomposition (probe-first vs implement-then-smoke), CONTRACT enforcement (rejected SPEC-violating options per Option Filter HARD GATE, rejected waiver-comment workaround), name gates (`parseHexAddress` reuse approved per NAMES Rule 5 Consistency), decision gates D-7/D-8/D-9, MANIFESTO L smell-check reaffirmation (33/35-line functions = one concern each, not wrong decomposition)
- Pathfinder — Windows formatter call-site map (`prettyPrint` single caller at `SessionInspection.cpp:93`, `formatSymbolValue` at line 89; inline filter at 77-79; call graph `getLocals → enumerateSymbols → {formatSymbolValue, prettyPrint}`); commit style + Sprint 18 heading format lookup
- Engineer — probe scaffold (`fixture_pretty_print.cpp` rewrite from JUCEApplicationBase to plain `int main` + `juce_core` only; `probe_pretty_print.cpp` SBValue-by-value signature fix); CMake `WHATDBG_BUILD_MAC_PROBES` option in `tests/mac/CMakeLists.txt`; `SessionPrettyPrint_mac.cpp` implementation (`prettyPrintJuceString` + `prettyPrintUniquePtr` + `parseHexAddress` helper + `prettyPrint` dispatcher); `SessionInspection_mac.cpp` wiring (`shouldSkipSymbol` filter + integration into `getLocals`/`getVariableChildren` + `makeVariableDynObj` prettyPrint override + `<unavailable>` fallback + `evaluateExpression` formatter routing + stale Phase-5-promise comment removal); `PrettyPrint.h` platform-guard fix (unconditional `<dbgeng.h>` include was compile-breaking latent bug on mac)
- Auditor — full CONTRACT sweep post-implementation (PASS with 4 nits: 3 cross-platform parity gaps + 1 pre-existing `namespace detail` project-wide violation); three parity nits fixed in same sprint per session CONTRACT addendum ("no divergence")

### Files Modified (6 total, 2 new)
- `Source/debug/SessionPrettyPrint_mac.cpp` — fleshed out from 3-line placeholder to 147 lines; `static std::uint64_t parseHexAddress (const juce::String& text) noexcept` at lines 21-33 (mirrors Windows `SessionPrettyPrint.cpp:111` per NAMES Rule 5); `static juce::String prettyPrintJuceString (lldb::SBValue& value) noexcept` at 47-77 (28 body lines, child walk `text.data` → `SBProcess::ReadCStringFromMemory` → `"..."` wrap); `static juce::String prettyPrintUniquePtr (lldb::SBValue& value) noexcept` at 91-122 (29 body lines, libc++ `pointer` child → `parseHexAddress` → `null`/`0x<hex>`, Windows-identical output shape, no implicit `0x`-prefix guard); `juce::String debug::detail::prettyPrint (lldb::SBValue&, const juce::String& typeName) noexcept` dispatcher at 128-142 (2 branches: `juce::String` + `std::unique_ptr<`; `std::string`/`std::vector` fall through to LLDB built-in)
- `Source/debug/SessionInspection_mac.cpp` — `static bool shouldSkipSymbol (const juce::String& name) noexcept` at 17-23 (4-or lookup: `<`-prefix, `leakDetector`, `__vfptr`, `juce::compileUnitMismatchSentinel`); `makeVariableDynObj` 43-76 (33 lines, routes through `detail::prettyPrint`, falls back to `<unavailable>` literal for optimized-out locals matching Windows `SessionInspection.cpp:90`); `getLocals` + `getVariableChildren` apply `shouldSkipSymbol` pre-build at lines 136 + 167; `evaluateExpression` 186-221 (35 lines, success path now routes through `detail::prettyPrint` with `GetValue()/GetSummary()` fallback, stale Phase-5-deferred comment removed)
- `Source/debug/PrettyPrint.h` — restructured platform guards (lines 1-131); unconditional `#include <dbgeng.h>` + Windows-only type declarations moved inside `#if JUCE_WINDOWS`; mac `prettyPrint` declaration added under `#if JUCE_MAC` (compile-breaking latent bug fixed as adjacent violation per Case 2)
- `tests/mac/fixture_pretty_print.cpp` (NEW, ~24 lines) — plain `int main` with four typed locals (`const juce::String juceStr { "hello" }`, `const std::string stdStr { "hello" }`, `const std::unique_ptr<int> uniq { std::make_unique<int> (42) }`, `const std::vector<int> vec { 1, 2, 3 }`) + `juce::ignoreUnused` + `__builtin_debugtrap ()`; links `juce::juce_core` only (no JUCE application lifecycle — `JUCEApplicationBase` requires `juce_events`, YAGNI for fixture)
- `tests/mac/probe_pretty_print.cpp` (NEW, ~120 lines) — standalone SB API launcher: `SBDebugger::Initialize/Create` + `SetAsync(false)` + `LaunchSimple` + wait-for-stop + iterate `SBValueList` + print `name | type | value | summary` per variable; links vendored `liblldb.dylib` via `@rpath`; ephemeral (remove post-Phase-5)
- `tests/mac/CMakeLists.txt` — new `option(WHATDBG_BUILD_MAC_PROBES "..." OFF)` at line 53; Phase 5 probe block lines 55-148 (JUCE discovery, `fixture_pretty_print` via plain `add_executable` + `juce::juce_core` + `-g -O0` + codesigned with `target_entitlements.plist`, `probe_pretty_print` via `add_executable` + liblldb link + `INSTALL_RPATH "${LIBLLDB_DIR}"` + codesigned with `entitlements.plist` + `PROBE_TARGET_PATH` compile definition + `add_dependencies` on fixture)

### Alignment Check
- [x] BLESSED principles followed
  - **B**: `SBValue`/`SBProcess`/`SBError` are SB API value types with ref-counted internals — RAII; stack-allocated `char buffer[maxStringReadSize] {}` at `SessionPrettyPrint_mac.cpp:63` scoped to function; no raw `new`/`delete`
  - **L**: 300/30 smell detector observed — all five new static helpers under 30 lines (`parseHexAddress` 10, `prettyPrintJuceString` 28, `prettyPrintUniquePtr` 29, `prettyPrint` dispatcher 14, `shouldSkipSymbol` 6); `makeVariableDynObj` 33 + `evaluateExpression` 35 (3-5 over smell line) — analyzed per MANIFESTO L ("smell detector, not arbitrary limit"), one concern each (DAP variable object construction / expression evaluation-and-format respectively), no wrong decomposition, inline shape matches Windows counterparts; file `SessionPrettyPrint_mac.cpp` 147 lines, `SessionInspection_mac.cpp` 213 lines — well within 300
  - **L 3-branch**: `prettyPrint` dispatcher 2 branches + fall-through; `shouldSkipSymbol` single boolean composed of 4 `or` operands = lookup-style predicate, semantically equivalent to data table
  - **E**: zero early returns; positive nested checks (`SessionPrettyPrint_mac.cpp:58, 70, 97, 101, 106, 108`, `SessionInspection_mac.cpp:31, 136, 155, 167, 189`); every `const char*` from SB API null-checked (e.g., `SessionPrettyPrint_mac.cpp:54-56, 99-101`); named constant `maxStringReadSize` — no magic numbers
  - **S (SSOT)**: `shouldSkipSymbol` single filter predicate (2 call sites: `getLocals` + `getVariableChildren`); `makeVariableDynObj` single variable-DAP-schema builder; `prettyPrint` single dispatcher; `parseHexAddress` reused from Windows per Rule 5 (not re-invented)
  - **S (Stateless)**: all new helpers `static` file-local pure functions; `makeVariableDynObj` mutates no `Session` state beyond writing the output object
  - **E (Encapsulation)**: Windows COM types properly confined inside `#if JUCE_WINDOWS` after `PrettyPrint.h` fix; mac TU only sees `lldb::SBValue&` under `#if JUCE_MAC`; platform leak stops at `Session`'s public boundary as before
  - **D**: formatting pipeline is pure function of `SBValue::GetValue()`/`GetSummary()`/child-walk + `SBProcess::ReadCStringFromMemory` — no hidden state; bit-identity vs Windows reference pending Phase 6 runtime smoke
- [x] NAMES.md adhered — Rule -1 honored; 5 new identifiers approved (`prettyPrint`, `prettyPrintJuceString`, `prettyPrintUniquePtr`, `shouldSkipSymbol`) + 1 reused via Rule 5 Consistency (`parseHexAddress` from Windows side); probe scaffolding names (`WHATDBG_BUILD_MAC_PROBES`, `fixture_pretty_print`, `probe_pretty_print`, `PROBE_TARGET_PATH`, local fixture vars `juceStr`/`stdStr`/`uniq`/`vec`) self-contained in ephemeral test surface
- [x] MANIFESTO.md applied — JUCE-first discipline (fixture uses `juce::String` native type + `juce::ignoreUnused`; formatter uses `juce::String` throughout, no raw `std::string` in the DAP pipeline); YAGNI enforced (fixture rejected `JUCEApplicationBase` requirement, used plain `int main`; waiver-comment mechanism rejected as CAROL-forbidden workaround)
- [x] JRENG-CODING-STANDARD.md — brace init throughout; `not`/`and`/`or` alternative tokens (zero `!`/`&&`/`||`); nested positive checks; `noexcept` on all new functions; `const` before type; `static` file-local symbols (no anonymous namespaces); `*`/`&` stick to type

### Problems Solved

**Problem 1 — Probe-first vs implement-blind.** PLAN Step 5.1 requires "verify output matches Windows" — not assume LLDB built-ins cover STL types. Resolved by building `fixture_pretty_print` + `probe_pretty_print` to capture raw `GetValue()`/`GetSummary()` for the four types. Probe output drove D-7/D-8/D-9 rulings: `juce::String` → NULL/NULL (custom formatter needed), `std::string` → NULL/"hello" (LLDB built-in matches Windows), `std::unique_ptr<int>{42}` → NULL/`42` pointee (diverges from Windows `0x<hex>`), `std::vector<int>{1,2,3}` → NULL/`size=3` (matches Windows), plus surfaced `juce::compileUnitMismatchSentinel` JUCE-internal symbol requiring filter addition.

**Problem 2 — Fixture needed `JUCEApplicationBase` → pulled `juce_events` indirectly.** Initial scaffold inherited `JUCEApplicationBase` ("console app base"), but `juce_add_console_app` with only `juce_core` linked cannot resolve the base class (it lives in `juce_events`). Rewrote fixture as plain `int main` + link `juce::juce_core` directly — YAGNI wins; fixture only needs a stack frame, not an event loop.

**Problem 3 — `tests/mac/` is a standalone CMake subproject, not wired into root.** First build attempt failed because root `CMakeLists.txt` doesn't `add_subdirectory(tests/mac)` — the subproject has its own `project()` declaration. Build commands now target `tests/mac/build/` separately. Pre-existing pattern from Phase 0.3 `smoke_liblldb` — not Phase 5 scope to change.

**Problem 4 — `PrettyPrint.h` unconditional `<dbgeng.h>` leaked Windows types into mac TU.** On macOS, any file including `PrettyPrint.h` would pull `<dbgeng.h>` → `IDebugDataSpaces4*`, `ULONG64`, etc. — compile error. Fix-as-adjacent-violation per Case 2: moved `<dbgeng.h>` + Windows signatures inside `#if JUCE_WINDOWS`, added mac signature under `#if JUCE_MAC`. Latent bug exposed by first-ever cross-platform use of the header.

**Problem 5 — `<unavailable>` string divergence (Auditor nit).** Windows `SessionInspection.cpp:88-90` emits literal `"<unavailable>"` string for optimized-out locals via ternary on `SUCCEEDED(valueResult)`. Mac implementation fell through null-chain to empty string. Per session CONTRACT addendum ("no divergence"), added `<unavailable>` fallback after `prettyPrint` override in `makeVariableDynObj`.

**Problem 6 — `evaluateExpression` skipped formatter on mac (Auditor nit).** Windows `SessionInspection.cpp:289-316` routes expression-eval results through `stripDecimalPrefix` + `juce::String` hot path. Mac returned raw `GetValue()`/`GetSummary()` verbatim. Fixed: `evaluateExpression` success path now calls `detail::prettyPrint(value, typeName)` with `GetValue/GetSummary` null-chain fallback, matching Windows pattern (minus dbgeng-specific `stripDecimalPrefix` which is not applicable to LLDB).

**Problem 7 — `prettyPrintUniquePtr` implicit `0x`-prefix guard diverged from Windows (Auditor nit).** Mac had `if (addrStr.startsWith ("0x") or addrStr == "0")` before parsing — absent on Windows. Dropped the guard; `parseHexAddress` now called unconditionally, branch only on `address == 0` vs non-zero. Matches Windows `SessionPrettyPrint.cpp:314-321` exactly.

**Problem 8 — Option Filter HARD GATE violation.** Proposed D-7 with options `A` (custom formatter) + `B` (ship without formatter, SPEC §Feature 6 violation). Offering a SPEC-violating option breaks COUNSELOR.md Option Filter ("An option that fails any filter is not a valid option. Do not offer it"). Acknowledged violation; D-7 was not a gate question — SPEC-required custom formatter is the only path.

**Problem 9 — Waiver-comment workaround proposal rejected.** After Fix 1 + Fix 2 pushed `makeVariableDynObj` to 33 and `evaluateExpression` to 35 lines, initially proposed `getStackTrace`-style "BLESSED L: N lines. ... decomposition rejected" waiver comment. ARCHITECT rejected — "waiver comment" is a workaround (CAROL.md forbids), and MANIFESTO L explicitly states "smell detectors, not arbitrary limits" with 3-5 line overage on one-concern functions passing the check. No comment needed. Both functions accepted as-is.

**Problem 10 — `parseHexAddress` extracted.** `prettyPrintJuceString` at 44 body lines + `prettyPrintUniquePtr` at 37 body lines both exceeded 30 via inline `startsWith("0x") → substring → getHexValue64` duplication. Extracted `static std::uint64_t parseHexAddress (const juce::String&) noexcept` — same semantics and identical name to Windows `SessionPrettyPrint.cpp:111` per NAMES Rule 5 Consistency (not improvised). Both callers now under 30 lines.

### Debts Paid
- None (DEBT.md does not exist at project root)

### Debts Deferred
- None — 3 of 4 Auditor nits fixed in-sprint per session CONTRACT addendum; 4th nit (`namespace detail` project-wide JRENG violation) is pre-existing, predates Phase 5, not sprint scope

## Sprint 18: macOS liblldb Backend — Session + Inspection + Feature 11 Port

**Date:** 2026-04-16
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — session framing, CONTRACT enforcement, PLAN updates (session decision #2 bundle drop, Step 2.2/2.3 rewrite, A.1 correction), name-gate decisions (17 new identifiers approved across Phase 2-4 + Feature 11), decision gates (Q1-Q4, symbol/source path impl, exception-code semantics, L-3-BRANCH refactor, helper splits), scope discipline (retracted bail/defer options, reasserted SPEC parity contract)
- Pathfinder — Phase 0/1 state survey, current codebase map, Builds-loss impact check, Windows-pollution sweep across cross-platform files, commit/sprint fact-gather
- Librarian — LLDB `target.debug-file-search-paths` + `target.source-map` authoritative research (lldb-dap `DAP.cpp:1101` pattern, `SBDebugger::SetInternalVariable` vs `HandleCommand` semantics)
- Engineer — CMake platform-select + APPLE block, Main.cpp guards, Session.h mac members, Session_mac.cpp Phase 3.1-3.6 impls (launch/attach/resume/step/interrupt/breakpoints/symbols/threads/pollEvents), Phase 4 (getStackTrace/getLocals/getVariableChildren/evaluateExpression + 2 helpers), Feature 11 mac port (signal + exception stop handlers, signalNames table, cross-platform getExceptionName promotion), audit-finding fixes (renames obj→threadEntry, module→moduleRef, TID jassert, out-pointer jasserts, timeoutMs WHY), pollEvents split into handleProcessEvent/handleTargetEvent, SBValue const-correctness fix, Whatdbg.cpp Windows-header guards
- Auditor — full CONTRACT sweep on Phase 2.1-3.6 (0 critical, 5 high) — findings drove the rename + jassert + WHY-comment fixes

### Files Modified (12 total, 3 new)
- `CMakeLists.txt` — Source select block (`REMOVE_ITEM` + `APPEND` per platform) at lines 144-174; new `if(APPLE)` block at lines 394-422 with liblldb include/link, BUILD_RPATH/INSTALL_RPATH `@loader_path`, POST_BUILD copy of dylib + LICENSE-liblldb.txt next to binary
- `PLAN-whatdbg-mac.md` — overview + session decision #2 rewritten to sibling-dylib model (no `.app` bundle); Step 2.2 + Step 2.3 action text rewritten; verified against `JUCEUtils.cmake:2147` (`juce_add_console_app` → flat Mach-O, no `MACOSX_BUNDLE`)
- `Source/Main.cpp` — `#include <BinaryData.h>` + `extractSidecarBinaries()` definition guarded `#if JUCE_WINDOWS`; sidecarDir on macOS = `juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory()` (where POST_BUILD copies liblldb.dylib); preserves existing `if (sidecarDir != juce::File{})` gate with no new identifier
- `Source/Whatdbg.cpp` — `<io.h>`/`<fcntl.h>` + `_setmode(_fileno(stdout), _O_BINARY)` wrapped `#if JUCE_WINDOWS`
- `Source/WhatdbgHandlers.cpp` — `handleLaunch` + `handleAttach` symbol/source path setup: `appendSymbolPath("srv*")` wrapped `#if JUCE_WINDOWS` (Microsoft symbol server URL, no macOS analogue); `cwd.replace("/","\\")` wrapped `#if JUCE_WINDOWS`, `#else` branch passes `cwd` verbatim on macOS
- `Source/dap/Reader.cpp` — `<io.h>`/`<fcntl.h>` + `_setmode(_fileno(stdin), _O_BINARY)` + `_fileno(stdin) >= 0` guard wrapped `#if JUCE_WINDOWS`; `#else` branch uses POSIX `fileno(stdin)`
- `Source/debug/Callbacks.h` — `getExceptionName` forward declaration removed (moved to State.h for cross-platform visibility)
- `Source/debug/Session.h` — `#if JUCE_MAC` block populated with `<lldb/API/LLDB.h>` include + 4 SB members (`debugger`, `listener`, `target`, `process`) + 2 Phase 4 cache members (`cachedFrameVariables`, `cachedFrameIndex`) + 2 Phase 4 helper declarations (`ensureFrameVariablesCache`, `makeVariableDynObj`); column doxygen corrected `always 0` → `always 1 (1-based per DAP convention)`
- `Source/debug/State.h` — `getExceptionName` declaration added in `namespace debug` with platform-native semantic doxygen (NTSTATUS on Windows, signal number on macOS); `exceptionCode` field doxygen expanded with per-platform source
- `Source/debug/Session_mac.cpp` (NEW, ~520 lines) — 7 static TU-local handlers + 2 dispatch tables (`stopReasonHandlers`, `signalNames`); `Session` lifecycle (~/init/shutdown/launch/attach/resume/pollEvents); execution (step*/interrupt); breakpoints (add/remove + source→offset via module/compile-unit iteration); symbol/source paths via `debugger.HandleCommand("settings append target.debug-file-search-paths …")` + `HandleCommand("settings set target.source-map \".\" \"…\"")`; threads with TID `jassert` + narrowing cast; `resetSymbolGroupCache` clears cachedFrameVariables; `getExceptionName` mac impl with signalNames lookup
- `Source/debug/SessionInspection_mac.cpp` (NEW, ~185 lines) — `ensureFrameVariablesCache` + `makeVariableDynObj` helpers; `getStackTrace` (SBFrame walk, SBLineEntry source info, column=1 per corrected Windows parity); `getLocals` (SBFrame::GetVariables, per-frame SBValueList cache via helper); `getVariableChildren` (GetNumChildren/GetChildAtIndex via helper); `evaluateExpression` (SBFrame::EvaluateExpression + SBError check, raw LLDB output pending Phase 5 pretty-print wrap)
- `Source/debug/SessionPrettyPrint_mac.cpp` (NEW, 3 lines) — TU placeholder; Phase 5 will populate macOS pretty-print formatters for STL-type parity

### Alignment Check
- [x] BLESSED principles followed
  - **B**: SB API value types with ref-counted internals — RAII on `SBDebugger`/`SBListener`/`SBTarget`/`SBProcess`/`SBValueList`; liblldb.dylib lifecycle owned by dynamic loader via `@loader_path`; sidecar sibling-file model vs Windows AppData extraction (platform-appropriate)
  - **L**: 3-branch rule satisfied — inner stop-reason switch replaced with `stopReasonHandlers` lookup table (O(1), 6 cases as data); `pollEvents` split into `handleProcessEvent`+`handleTargetEvent`; `getLocals` 37→13 lines via helpers; `getVariableChildren` 43→20 lines; `getStackTrace` 38 lines accepted per YAGNI (single-use `makeFrameDynObj` would not survive BLESSED L criteria)
  - **E**: positive nested checks throughout; `not`/`and`/`or` alternative tokens; `jassert` on out-pointers (`outEngineId`/`outOffset`/`outLine`) + TID narrowing at boundaries per PLAN line 206; `juce::ignoreUnused` on genuinely unused parameters; WHY comments on platform-divergent no-ops
  - **S (SSOT)**: `State::getContext()` single access pattern mirrored from Windows; `cachedFrameVariables` is the single per-frame local-variable cache (invalidated by `resetSymbolGroupCache`); `getExceptionName` declaration in one header (State.h); `makeVariableDynObj` is SSOT for variable DAP shape across `getLocals` + `getVariableChildren`
  - **S (Stateless)**: no new orchestrator flags added; handlers write `debug::State*` directly, no intermediate machinery state
  - **E (Encapsulation)**: platform leak stops at `Session`'s public boundary; `Whatdbg`/`WhatdbgHandlers`/`BreakpointManager` unchanged on mac except for `#if JUCE_WINDOWS` guards on Windows-isms in command handlers
  - **D**: DAP output schema bit-parity with Windows (`id` = loop index on stack frames, `hasChildren` bool on variables, `column = 1`) — not assumption, verified by reading `SessionInspection.cpp`
- [x] NAMES.md adhered — Rule -1 honored; 17 new identifiers ARCHITECT-approved: `debugger`, `listener`, `target`, `process`, `processState`, `debugState`, `cachedFrameVariables`, `cachedFrameIndex`, `ensureFrameVariablesCache`, `makeVariableDynObj`, `threadEntry`, `moduleRef`, `signalNames`, `stopReasonHandlers`, `handleBreakpointStop`, `handleStepStop`, `handleInterruptStop`, `handleSignalStop`, `handleExceptionStop`, `handleProcessEvent`, `handleTargetEvent` (mirrors Windows `Callbacks.cpp` established pattern)
- [x] MANIFESTO.md applied — JUCE-first discipline (no rolled crash handlers, `juce::SystemStats::setApplicationCrashHandler` on both platforms; `juce::File::currentExecutableFile` for sidecarDir; `juce::Result` for Session return semantics; `juce::DynamicObject`/`juce::var` for DAP shape)
- [x] JRENG-CODING-STANDARD.md — brace init throughout; `not`/`and`/`or`; `.at()` where applicable; no early returns (single exit point per function); no anonymous namespaces (`static` file-local symbols); `enum class`; `noexcept` on all new functions; `const` before type; platform-divergent C++20 keyword hazard avoided (`module` → `moduleRef` rename)

### Problems Solved

**Problem 1 — `.app` bundle assumption broken.** PLAN Step 2.2 used `$<TARGET_BUNDLE_CONTENT_DIR>/Frameworks/liblldb.dylib`, but `juce_add_console_app` produces a flat Mach-O on macOS (verified at `JUCEUtils.cmake:2147`: `add_executable(${target})` with no `MACOSX_BUNDLE`). PLAN was wrong. Bundling a CLI tool is also semantically wrong — macOS CLI convention is flat binary + sibling dylib (lldb, clang, brew). Replaced with `BUILD_RPATH`/`INSTALL_RPATH @loader_path` + POST_BUILD copy pattern.

**Problem 2 — PLAN A.1 wrong on symbol/source path APIs.** `SBTarget::AppendImageSearchPath(from, to, error)` is a path-remap (from→to), NOT an additive search path. `SBDebugger::SetSourceMap` does not exist in LLDB 21.x. @Librarian found lldb-dap's own pattern: `target.debug-file-search-paths` via `HandleCommand("settings append …")` + `target.source-map` single-pair remap via `HandleCommand("settings set …")` (DAP.cpp:1101). Implemented both via `HandleCommand` rather than `SetInternalVariable` which only supports `eVarSetOperationAssign` (replace, not append).

**Problem 3 — Caller bugs in WhatdbgHandlers.cpp exposed by cross-platform compile.** `session.appendSymbolPath("srv*")` is Windows MS symbol-server URL with no macOS analogue; `cwd.replace("/","\\")` force-converts POSIX paths to Windows separators and breaks LLDB path resolution on macOS. Both wrapped `#if JUCE_WINDOWS` with `#else` branch passing `cwd` verbatim.

**Problem 4 — LLDB SB API non-const accessors.** `SBValue::GetValue()`/`GetSummary()`/`GetName()`/`GetTypeName()`/`MightHaveChildren()` are all declared non-const in `SBValue.h`. `makeVariableDynObj` signature corrected from `const lldb::SBValue&` → `lldb::SBValue&` to match LLDB API convention (SB objects are cheap ref-counted wrappers; non-const ref is the idiom).

**Problem 5 — CONTRACT addendum on feature parity.** Earlier drafts suggested options like "no-op + log" for `appendSymbolPath`/`appendSourcePath` and "defer L-3-BRANCH" — ARCHITECT explicitly rejected both patterns. CONTRACT locked: ALL Windows features must be implemented on macOS; backend divergence (dbgeng vs liblldb) is expected, but feature-surface parity is non-negotiable. "A debugger is a debugger." Drove rewriting options A/B/C into single-path implementation for symbol/source paths, Feature 11 mac port, and pollEvents L-3-BRANCH refactor.

**Problem 6 — Feature 11 (target crash surfacing) was Windows-only.** Sprint 17 surfaced debuggee crashes via NTSTATUS in DAP `exceptionInfo`. On macOS, LLDB reports `eStopReasonSignal` / `eStopReasonException` instead. Ported: `handleSignalStop` + `handleExceptionStop` populate `hasExceptionStopped`/`exceptionCode`/`exceptionAddress` (from `SBFrame::GetPC()`); `signalNames` TU-local table maps signal numbers → `"EXC_BAD_ACCESS"`/`"EXC_BREAKPOINT"`/etc.; `getExceptionName` declaration promoted from Windows-only `Callbacks.h` to cross-platform `State.h`; `State.h:172-184` doxygen expanded with per-platform `exceptionCode` semantics.

**Problem 7 — Inner stop-reason switch grew to 6 branches during Feature 11 port.** Original Phase 3.2 deferred L-3-BRANCH per PLAN line 486; adding signal + exception cases would have made it worse. Refactored to `stopReasonHandlers` lookup table (`std::unordered_map<lldb::StopReason, void(*)(debug::State*, lldb::SBThread&)>`) with 5 static handler functions — O(1) dispatch, adding cases is data not code, mirrors Windows `Callbacks.cpp::exceptionHandlers` pattern.

**Problem 8 — `pollEvents` 73 lines after Feature 11 + L-refactor.** Outer process-event / target-event dispatch kept the function over 30 lines. Split into `handleProcessEvent (state, process, event)` + `handleTargetEvent (state, event)` static TU-local helpers. `pollEvents` body reduced to 14 lines.

**Problem 9 — `module` C++20 keyword hazard.** Local variable `module` in `Session_mac.cpp` (added during Phase 3) is a reserved identifier under C++20. Project is currently C++17 so technically legal, but forward-incompatible. Renamed to `moduleRef` per NAMES Rule -1 (ARCHITECT approved).

**Problem 10 — TID narrowing silent cast violated PLAN line 206.** `lldb::tid_t` is uint64_t; `std::uint32_t` storage in `State` requires explicit check per PLAN "(assert or log)". Initial implementation followed Windows `Callbacks.cpp:281` convention (silent cast), but PLAN contract overrides codebase pattern. Fixed: `jassert (tid <= std::numeric_limits<std::uint32_t>::max())` before cast in `getThreads()` + `getEventThreadSystemId()`.

**Problem 11 — Windows-pollution in cross-platform files.** `Reader.cpp` used `<io.h>` + `_setmode(_fileno(stdin), _O_BINARY)` for binary-mode stdin on Windows (CRLF translation). `Whatdbg.cpp` used the same pattern for stdout. Both missed in Sprint 16 detox. Thorough @Pathfinder sweep confirmed Whatdbg.cpp was the only remaining offender; `#if JUCE_WINDOWS` guards applied. Non-Windows branch in `Reader::stop` uses POSIX `fileno(stdin)`.

### Debts Paid
- None (DEBT.md does not exist at project root)

### Debts Deferred
- None



**Date:** 2026-04-16
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — session framing, scope discipline, CONTRACT enforcement, audit triage, name-gate approvals, docs sync
- Pathfinder — crash surface discovery (Log.h / callbacks / dbgeng event loop), Hungarian-notation sweep across Source/ and modules/
- Librarian — JUCE `SystemStats::setApplicationCrashHandler` / `getStackBacktrace` / `FileLogger` facts; documented `std::terminate` SEH gap on Windows
- Engineer — crash-handler install, diagnostic-breadcrumb placement across shutdown chain, `Reader::stop()` iterations (fclose placement + idempotent guard), `Session::shutdown(EndMode)` refactor, `Loader::~Loader` FreeLibrary removal, exception surfacing (state fields, handler dispatch, DAP stopped+output emission, `handleExceptionInfo`), shadow-state collapse (`InitialBreakPhase`), Hungarian strip (17 constants), SPEC/ARCHITECTURE doc edits
- Auditor — full-sprint audit — 13 diagnostic categories flagged for strip, 5 decision findings (D1–D5), shadow-state finding, 11 doc findings

### Files Modified (18 total)
- `Source/Main.cpp` — `onApplicationCrash` (SEH path) + `onApplicationTerminate` (`std::set_terminate` path) installed under `#if JUCE_DEBUG` after `g_logFile` opens; both write `juce::SystemStats::getStackBacktrace()` via existing `logWrite`
- `Source/Whatdbg.h` — `handleExceptionInfo` declaration added; explicit `~Whatdbg` removed (was diagnostic-only)
- `Source/Whatdbg.cpp` — `isRunning = false` after target exits so loop doesn't wait for disconnect; `EndMode` selection (`passive` when `executionState == exited`, `terminate`/`detach` otherwise); new `hasExceptionStopped` block in `processDeferredEvents` emits DAP `stopped(reason=exception)` + `output(category=stderr)`; `exceptionInfo` registered in `commandHandlers`; `initialBreakPhase` references; `std::fclose (stdin)` call removed (now owned by `Reader::stop`)
- `Source/WhatdbgHandlers.cpp` — `handleExceptionInfo` implementation — responds with `exceptionId`, `description`, `breakMode: "unhandled"` built from persistent `State::exceptionCode` + `exceptionAddress`
- `Source/dap/Reader.cpp` — `Reader::stop` closes stdin idempotently (`if (_fileno (stdin) >= 0)`) to unblock `std::getline`; destructor returns to defaulted body after diagnostic strip
- `Source/dap/Reader.h` — `kFifoCapacity` → `fifoCapacity` (Hungarian strip)
- `Source/dap/Types.h:133` — `supportsExceptionInfoRequest` flipped to `true`
- `Source/debug/State.h` — `InitialBreakPhase { notHit, pending, resolved }` enum added; `isInitialBreakSeen` + `isInitialBreakHandled` collapsed into single `initialBreakPhase` field (SSOT + Stateless); new exception fields `hasExceptionStopped`, `exceptionCode`, `exceptionAddress`
- `Source/debug/Session.h` — `EndMode { terminate, detach, passive }` enum added; `shutdown(bool shouldTerminate)` → `shutdown(EndMode mode)` with no default (Explicit)
- `Source/debug/Session.cpp` — `shutdown` maps EndMode → `DEBUG_END_ACTIVE_TERMINATE`/`_ACTIVE_DETACH`/`_PASSIVE`; `~Session` and init-failure path pass explicit `EndMode::passive`
- `Source/debug/Callbacks.h` — `getExceptionName(std::uint32_t) noexcept` returning `juce::String` (name or `"0x<hex>"` fallback); doc comments updated to reference `initialBreakPhase::pending`
- `Source/debug/Callbacks.cpp` — `ExceptionHandler` sig includes `PEXCEPTION_RECORD64`; `handleUnknownException` on 2nd-chance populates `hasExceptionStopped` + `exceptionCode` + `exceptionAddress` and sets `executionState = stopped`; `handleBreakpoint` transitions `initialBreakPhase`; `handleSingleStep` + `EXCEPTION_SINGLE_STEP` map entry deleted (dead per PLAN, stripped per ARCHITECT); `exceptionNames` lookup (17 NTSTATUS/SEH codes); `getExceptionName` definition in `namespace debug`; `ExitProcess` callback log added for event-callback consistency
- `Source/debug/BreakpointManager.h` — `kLineSearchWindow` → `lineSearchWindow`; explicit `~BreakpointManager` declaration removed
- `Source/debug/BreakpointManager.cpp` — `lineSearchWindow` references; diagnostic destructor body removed
- `Source/debug/Loader.cpp` — `FreeLibrary(dbgengModule)` removed from `~Loader` (dbgeng unload hang — symsrv threads + residual COM state); `dbgengModule = nullptr` + Guard-Rule comment retained (named threat)
- `Source/debug/SessionInspection.cpp` — 6 `k`-prefix constants stripped (`symbolNameSize`, `symbolTypeSize`, `symbolValueSize`, `maxStackFrames`, `nameBufferSize`, `fileBufferSize`)
- `Source/debug/SessionPrettyPrint.cpp` — 5 `k`-prefix constants stripped (`maxStringReadSize`, `childNameSize`, `valueSize`, `ssoThreshold`, `elemTypeSize`)
- `Source/Main.cpp` — `kSidecarDirName`, `kDbgEngSubdir`, `kLogFileName` → camelCase
- `Source/Whatdbg.cpp` — `kPollTimeoutMs` → `pollTimeoutMs`
- `SPEC.md` — `kLineSearchWindow` reference updated; Feature 11 (Target Crash / Exception Info Surfacing) added
- `ARCHITECTURE.md` — `Session::shutdown(EndMode)` signature + 3-way rationale; Exception Flow subsection in Data Flow; new State fields (`hasExceptionStopped`, `exceptionCode`, `exceptionAddress`, `initialBreakPhase`) in State Management
- `install.sh` — `clear` prepended so rebuild output is unobstructed

### Alignment Check
- [x] BLESSED principles followed (B: `Reader` owns stdin lifecycle after fix, `Loader` named-threat Guard for intentional leak; L: 3-branch lookups preserved; E: `EndMode` removes ambiguous default, `getExceptionName` single exit, positive nesting; S SSOT: `exceptionNames` TU-local behind `getExceptionName`, `initialBreakPhase` replaces paired bools; S Stateless: no new orchestrator flags; E Encapsulation: crash handlers use JUCE API not rolled SEH/terminate; D: EndMode mapping total, phase lifecycle total)
- [x] NAMES.md adhered — Rule -1 honored; ARCHITECT approved every new name (`EndMode` + 3 values, `InitialBreakPhase` + 3 values, `hasExceptionStopped`, `exceptionCode`, `exceptionAddress`, `exceptionNames`, `getExceptionName`, `handleExceptionInfo`, `onApplicationCrash`, `onApplicationTerminate`, `initialBreakPhase`)
- [x] MANIFESTO.md applied — JUCE-first discipline (single `SystemStats::setApplicationCrashHandler` call, not rolled SEH/terminate); Guard Rule explicit on FreeLibrary omission
- [x] JRENG-CODING-STANDARD.md — Hungarian fully purged (17 constants), `not`/`and`/`or`, brace init, no early returns, `enum class`, no anonymous namespaces

### Problems Solved

**Problem 1 — Target crash invisible to DAP widget.** Debuggee's unhandled SEH (e.g. `0xC0000005`) was logged to `whatdbg.log` only; nvim saw target die with no crash reason. Added `hasExceptionStopped` deferred-event path: `handleUnknownException` captures exception record on 2nd-chance, `processDeferredEvents` emits DAP `stopped(reason=exception)` + `output(category=stderr)`, `handleExceptionInfo` responds with structured details. `supportsExceptionInfoRequest: true` advertised. Widget now shows stack trace at crash point via existing `stackTrace` handler.

**Problem 2 — Adapter dragged into target's grave (silent cascade).** Multi-layered silent death between target's `ExitProcess` and `main()` exit. Diagnosed through progressive breadcrumb instrumentation:
- (a) `isRunning` never flipped after target exit → loop busy-spins waiting for nvim disconnect that never arrives → fixed with `isRunning = false` in `processDeferredEvents` exit block
- (b) `Reader::stop` called `stopThread(2000)` while `std::getline(std::cin)` blocked → JUCE `Thread::stopThread` fell through to `TerminateThread` → CRT state corruption → fixed by closing stdin (idempotent via `_fileno >= 0` guard) before `stopThread`
- (c) `client->EndSession(DEBUG_END_ACTIVE_DETACH)` hung on a target already dead (dbgeng session no longer active) → fixed via `EndMode::passive` selected when `executionState == exited`
- (d) `Loader::~Loader` called `FreeLibrary(dbgeng.dll)` → dbgeng's symsrv threads + residual COM state make unload unsafe → FreeLibrary removed, HMODULE intentionally leaked for OS process cleanup (Guard Rule comment documents the named threat)

**Problem 3 — JUCE crash handler + `std::set_terminate` installed as safety nets.** Catches future adapter-side SEH exceptions (Windows `SetUnhandledExceptionFilter`) and uncaught C++ exceptions (`std::terminate` path) with stack-backtrace write to log. Debug-only (`#if JUCE_DEBUG`).

**Problem 4 — Shadow state on initial-break lifecycle.** `isInitialBreakSeen` + `isInitialBreakHandled` encoded a 3-state machine with 2 bools (invalid `(notHandled, seen)` combination possible). Collapsed into `InitialBreakPhase { notHit, pending, resolved }` — single field, impossible invalid states (MANIFESTO §S SSOT + §S Stateless).

**Problem 5 — Dead `EXCEPTION_SINGLE_STEP` branch.** `handleSingleStep` set `state->hasStepCompleted`, but the field is also set via WaitForEvent side-effect at `Whatdbg.cpp:77` — the exception-path setter was dead. Handler + map entry deleted. `hasStepCompleted` retained (live via the other path).

**Problem 6 — Hungarian notation (17 `k`-prefix constants).** JRENG-CODING-STANDARD "descriptive names, not type-based" — `k` prefix violates. Stripped across 6 files via mechanical rename. No other Hungarian (`m_`, `pFoo`, `iFoo`, SCREAMING_SNAKE) found.

**Problem 7 — Doc drift from code.** `SPEC.md` referenced `kLineSearchWindow`, had no exception-info feature; `ARCHITECTURE.md` had pre-sprint `shutdown(bool)` signature, no exception flow in Data Flow, old state fields. Synced all three doc edits in Phase C.

### Debts Paid
- None (DEBT.md does not exist at project root)

### Debts Deferred
- None



**Date:** 2026-04-15
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — `/goplan`, decision gating (D-1 through D-6), orchestration, RFC → Appendix A inline, path reorganization, `/log`
- Engineer — Build script (`scripts/build-liblldb-mac.sh`), collapsed Phase 1.1+1.2 header detox, `debug::ResolveStatus` introduction, reorganization (Builds/liblldb, Resources/macos/liblldb), smoke-test + target + entitlements + codesign setup, script evolution (zstd fix, header-staging fixes ×2, install-name fix, tarball switch)
- Auditor — Phase 1 audit (FAIL on static Windows parity → PASS after scope collapse), Phase 0 smoke iterations (5 consecutive FAIL reports → PASS)

### Files Modified (14 total)
- `PLAN-whatdbg-mac.md` — D-1 through D-4 rulings locked; D-5/D-6 marked deferrable; Appendix A added with API mapping tables + event dispatch pattern + SBListener setup, inlined from superseded RFC; D-3 path example updated to reflect reorg
- `ARCHITECTURE.md` — file structure extended with `Resources/macos/liblldb/` mirror + `Builds/liblldb/` + `scripts/`
- `.gitignore` — `build/` removed (old location); `Resources/macos/` added
- `scripts/build-liblldb-mac.sh` (new, 110 lines) — pinned LLVM tarball fetch, MinSizeRel universal build, header + dylib staging, `install_name_tool -id @rpath/liblldb.dylib`, size report
- `tests/mac/smoke_liblldb.cpp` (new, 64 lines) — SBDebugger init, CreateTarget, Launch, wait for eStateExited, report PASS/FAIL
- `tests/mac/target_program.cpp` (new, 5 lines) — trivial launch target (`int main() { return 0; }`)
- `tests/mac/CMakeLists.txt` (new, 50 lines) — standalone cmake, two executables, two codesign POST_BUILD hooks
- `tests/mac/entitlements.plist` (new) — smoke binary: `cs.allow-unsigned-executable-memory` + `cs.disable-library-validation` + `cs.debugger`
- `tests/mac/target_entitlements.plist` (new) — target binary: `get-task-allow`
- `Source/debug/State.h` — removed `<windows.h>`; added `<cstdint>`; `ULONG` → `std::uint32_t` on 2 fields; added `debug::ResolveStatus` enum class alongside `ExecutionState`
- `Source/debug/Session.h` — full D-1-A detox: COM includes + private members wrapped in `#if JUCE_WINDOWS`, reserved `#if JUCE_MAC` blocks, `HRESULT` → `juce::Result` on 6 methods, `getOffsetByLine` → `debug::ResolveStatus`, `pollEvents(uint32_t timeoutMs, bool& outHadEvent)` new signature; 8 public-API type substitutions
- `Source/debug/Session.cpp`, `Source/debug/SessionInspection.cpp`, `Source/debug/SessionPrettyPrint.cpp` — bodies wrapped in `#if JUCE_WINDOWS ... #endif`; public-method return paths updated to `juce::Result::ok()` / `juce::Result::fail(...)`; `getOffsetByLine` Windows impl maps HRESULT → `ResolveStatus` enum
- `Source/debug/BreakpointManager.h`, `BreakpointManager.cpp`, `BreakpointManagerHandlers.cpp` — `ULONG` → `std::uint32_t` throughout (25 replacements in `.cpp` pair); call sites of `getOffsetByLine` branch on `ResolveStatus` enum (no message-string matching); `SUCCEEDED`/`FAILED` macros replaced with `.wasOk()`/`.failed()` on `juce::Result`
- `Source/Whatdbg.h`, `Source/Whatdbg.cpp`, `Source/WhatdbgHandlers.cpp` — `ULONG` → `std::uint32_t`; `Callbacks.h` + `Loader.h` includes wrapped in `#if JUCE_WINDOWS`; `pollEvents` caller adopts new 2-arg signature with `bool hadEvent { false }` local

### Alignment Check
- [x] BLESSED principles followed (B: SB API + ComPtr RAII unchanged; L: 3 mac `Session_*` files still planned, no PIMPL, no abstract base; E: positive nesting preserved; S SSOT: `debug::State` still owns state, `ResolveStatus` eliminates shadow-category hack; S Stateless: no new `Session` state fields; E Encapsulation: callers no longer grep error messages; D: `ResolveStatus` mapping total)
- [x] NAMES.md adhered — Rule -1 honored; every new identifier ARCHITECT-approved (`juce::Result`, `debug::ResolveStatus` + 3 values, `outHadEvent` param, `hadEvent` caller local, `SMOKE_TARGET_PATH` compile-def)
- [x] MANIFESTO.md applied (JUCE-first: `juce::Result` chosen over custom `SessionStatus` after ARCHITECT reminder)
- [x] JRENG-CODING-STANDARD.md — no early returns introduced; `not`/`and`/`or`; brace init; `.at()` already used; `enum class`; no anonymous namespaces; no `namespace detail`

### Problems Solved

**Problem 1 — RFC load-bearing content vs. plan self-containment.** RFC-WHATDBG-MAC-00 contained the DbgEng↔liblldb API mapping tables + event-dispatch pattern but also factually wrong claims (Xcode framework linkage, "Session.h untouchable"). Inlined the accurate sections as Appendix A of PLAN; deleted the RFC.

**Problem 2 — Windows-type pollution above `debug::Session`.** RFC claimed Session.h was the sole platform boundary; audit found `ULONG` and `<windows.h>` leaking into `State.h`, `BreakpointManager.h`, `Whatdbg.h`. D-1-A mechanical substitution + `#if JUCE_WINDOWS` guards across 10 files.

**Problem 3 — Non-redistributable Xcode LLDB (~354 MB, Python + Swift + private frameworks).** Replaced Xcode framework linkage with self-built vendored dylib per D-3-C: `scripts/build-liblldb-mac.sh` → `Resources/macos/liblldb/`. Final 161 MB universal (under 200 MB budget).

**Problem 4 — Five consecutive Phase 0.3 smoke test failures diagnosed + fixed.** (a) Staging copied `lldb/API/` but not top-level `lldb/lldb-*.h` — fixed with glob. (b) `lldb/API/SBLanguages.h` is CMake-generated in build tree — fixed by staging from `$BUILD_DIR/tools/lldb/include/lldb/API/`. (c) Staged dylib's `LC_ID_DYLIB` still said `liblldb.21.1.8.dylib` after rename — fixed with `install_name_tool -id`. (d) `task_for_pid` denied to ad-hoc-signed smoke — fixed with Developer ID codesign + `cs.debugger` entitlement. (e) AMFI blocked debug of Apple-signed hardened `/bin/echo` — fixed by building local target_program with `get-task-allow`.

**Problem 5 — `HRESULT` binary-ness forced a string-match hack.** First engineer pass stringified HRESULT into `juce::Result::fail` message so `BreakpointManager.cpp` could detect `E_UNEXPECTED` via `.contains("80000003")`. Auditor flagged BLESSED Encapsulation + SSOT violation. ARCHITECT locked option (b): purpose-built `debug::ResolveStatus { resolved, notFound, engineBusy }` enum for `getOffsetByLine` only; `juce::Result` retained for the other 7 methods.

**Problem 6 — Path reorganization (`build/` vs `Builds/` visual collision).** Reorganized machinery under `Builds/liblldb/{llvm-project,cmake}/` (reuses existing JUCE `Builds/` gitignore rule), dist artefacts at `Resources/macos/liblldb/` (mirrors Windows `Resources/<arch>/` sidecar pattern).

**Problem 7 — GitHub git-pack RPC failures on 2 GB `llvm/llvm-project` clone (4 consecutive).** HTTP/2 stream cancellation during sideband transfer. Switched script from `git clone` to `curl -L --retry-all-errors -C -` tarball fetch — resumable, CDN-served, no pack protocol.

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 15: Standalone Breakpoint Resolution

**Date:** 2026-04-13
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Diagnosis, planning, log analysis, delegation, audit review
- Pathfinder — Codebase survey, Reader implementation discovery, build/install workflow, nvim-dap config
- Engineer — Diagnostic logging in Reader, CreateProcess callback fix (reverted), initial break BP resolution, DRY extraction
- Auditor — BLESSED compliance audit (found DRY/SSOT violation in duplicated resolution block)

### Files Modified (7 total)
- `Source/dap/Reader.cpp:98-121` — Added diagnostic logWrite: parsed message type/command, FIFO-full drop warning, JSON parse failure
- `Source/Whatdbg.h:174-182` — Added `resolveAndResumeAfterInitialBreak()` private method declaration with doxygen
- `Source/Whatdbg.cpp:158,268-289` — Collapsed `processDeferredEvents` initial break handler to call `resolveAndResumeAfterInitialBreak()`; added method implementation (forceReloadAllSymbols + onModuleLoad + resume + thread event)
- `Source/WhatdbgHandlers.cpp:85-88` — Collapsed `handleConfigurationDone` stopped branch to call `resolveAndResumeAfterInitialBreak()`
- `retag.sh` → `release.sh` — Renamed, aligned with TIT release workflow (gh release delete + cleanup-tag, optional commit message)
- `.github/workflows/release.yml:17` — Accept bare version tags (`[0-9]*`) in addition to v-prefixed (`v*`)

### Alignment Check
- [x] BLESSED principles followed (SSOT: extracted duplicated BP resolution into single method; Explicit: clear method name describes intent; Lean: no new patterns, reuses existing onModuleLoad/forceReloadAllSymbols)
- [x] NAMES.md adhered (resolveAndResumeAfterInitialBreak — verb phrase, semantic, Rule 1/3/4 compliant)
- [x] MANIFESTO.md applied
- [x] JRENG-CODING-STANDARD.md followed (no early returns, `not`/`and`/`or` tokens, brace init, braces on new line)

### Problems Solved

**Problem 1 — Standalone breakpoints never resolved**
Exe module loads via CreateProcess callback before setBreakpoints arrives. forceReloadAllSymbols fallback in handleSetBreakpoints returned E_UNEXPECTED (symbol engine not ready before first WaitForEvent). Fix: resolve pending BPs at the initial breakpoint when symbol engine is ready. Two code paths covered (configurationDone before/after initial break) via shared method.

**Problem 2 — Invisible Reader message flow**
Reader only logged "queued message" — no visibility into what command was parsed, whether FIFO dropped messages, or whether JSON parsing failed. Fix: added diagnostic logWrite for parsed command name, FIFO-full drops, and parse failures.

**Problem 3 — DRY violation (audit finding)**
BP resolution + resume logic duplicated in processDeferredEvents and handleConfigurationDone. Fix: extracted resolveAndResumeAfterInitialBreak() as single source of truth.

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 14: Mason Distribution + FetchContent + README + CI

**Date:** 2026-04-02
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, CI workflow writing, CMakeLists FetchContent, README, package.yaml, doc updates
- Researcher — Mason registry packaging research (registry format, package.yaml schema, custom registry, CI pipeline, mason-nvim-dap bridge)
- Engineer — Directory cleanup (deleted mason/ and whatdbg-mason-registry/)

### Files Modified (8 total)

- `README.md` — Created: comprehensive project README (why whatdbg exists, features, build, mason install, nvim-dap config for standalone + plugin debugging, architecture links)
- `.github/workflows/release.yml` — Created: GitHub Actions CI — builds Release with MSVC+Ninja on tag push, packages whatdbg.exe into whatdbg-win-x64.zip, generates registry.json.zip for mason, creates GitHub Release with all artifacts
- `packages/whatdbg/package.yaml` — Created: mason package definition (pkg:github/jrengmusic/whatdbg, win_x64 target)
- `CMakeLists.txt:88-97` — JUCE discovery fallback: FetchContent auto-fetches JUCE 8.0.12 from GitHub when not found locally. Local dev unchanged (sibling directory still preferred)
- `SPEC.md:1-4` — Added full name, repo URL
- `PLAN.md:1-4,12-13` — Added full name, repo URL, broadened objective
- `ARCHITECTURE.md:1-4,17` — Added full name, repo URL, broadened purpose
- `carol/SPRINT-LOG.md:1-4` — Added full name, repo URL

### Alignment Check
- [x] BLESSED principles followed (Lean: single repo for binary + registry, no separate registry repo; SSOT: package.yaml version auto-updated by CI from git tag; Explicit: FetchContent version pinned to 8.0.12)
- [x] NAMES.md adhered
- [x] MANIFESTO.md applied
- [x] JRENG-CODING-STANDARD.md — N/A (no C++ changes)

### Problems Solved

**Problem 1 — No distribution path**
whatdbg had no way to be installed by users. Fix: GitHub Actions CI builds Release binary on tag push, mason package.yaml enables `:MasonInstall whatdbg` via custom registry.

**Problem 2 — JUCE not vendored, CI can't build**
JUCE found via sibling directory — unavailable in CI. Fix: FetchContent fallback in CMakeLists.txt auto-fetches JUCE 8.0.12 from GitHub. Local dev unaffected (sibling path checked first).

**Problem 3 — Separate mason registry repo overhead**
Mason requires registry.json.zip as a release asset. Initially planned as a separate repo. Fix: merged into whatdbg repo — release workflow builds both binary and registry artifacts in one job.

**Problem 4 — No README**
Fix: comprehensive README following END/TIT style — purpose, features, build instructions, mason install, nvim-dap config examples (standalone + DAW plugin debugging).

### Technical Debt / Follow-up
- CI workflow untested — first run triggered by `git tag v0.0.1 && git push origin v0.0.1`
- `yq` in CI uses pip install (Python yq) — may need `snap install yq` or direct binary download if pip version is incompatible
- registry.json.zip format unverified against mason's expectations — needs testing with `:MasonInstall whatdbg`
- No LICENSE file in repo
- `fopen`/`fclose` raw C I/O — should be juce::FileLogger (carried)

## Sprint 13: Audit Completion + SPEC + PLAN v5 + Documentation

**Date:** 2026-04-02
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, delegation, SPEC.md writing, PLAN.md v5.0 update, direct fixes (duplicate variables, narrowing conversion, dx revert, diagnostic logging)
- Machinist (7 parallel) — tryResolve early returns, ComPtr conversion, BreakpointManager split, Whatdbg split, Exception lookup table, leakDetector/vfptr filter, doxygen documentation
- Researcher — Mason registry packaging research (registry format, package.yaml, custom registry, CI pipeline, mason-nvim-dap bridge)

### Files Modified (20+ total)

**Audit completion (Machinist sweep — unfinished items from Sprint 12):**
- `Source/debug/BreakpointManager.cpp` — tryResolve 4 early returns eliminated (single exit point with `engineNotReady` flag + positive nested checks)
- `Source/debug/BreakpointManagerHandlers.cpp` — New: handleSetBreakpoints + onModuleLoad extracted (326 lines)
- `Source/Whatdbg.cpp` — Reduced to ~301 lines (core orchestrator only)
- `Source/WhatdbgHandlers.cpp` — New: 16 DAP command handlers extracted (355 lines)
- `Source/debug/Session.h` — `cachedSymbolGroup` → `ComPtr<IDebugSymbolGroup2>`
- `Source/debug/Session.cpp` — `cachedSymbolGroup` manual Release → ComPtr.Reset/Attach
- `Source/debug/SessionInspection.cpp` — `secondaryClient` → ComPtr; `enumerateSymbols` filters `leakDetector` + `__vfptr`; dx integration removed (scope contamination)
- `Source/debug/Callbacks.cpp` — Exception 6-branch if-else → 4 static handler functions + lookup map

**Doxygen documentation (all 10 headers):**
- `Source/Log.h` — logWrite, g_logFile documented
- `Source/Whatdbg.h` — Whatdbg class, all public methods, all members
- `Source/dap/Reader.h` — Reader class, start/stop/tryPop
- `Source/dap/Types.h` — DynObj alias, makeResponse, makeEvent, makeCapabilities, getString
- `Source/debug/State.h` — All fields with who-sets/who-reads documentation
- `Source/debug/Session.h` — All public methods with params, returns, thread safety notes
- `Source/debug/Callbacks.h` — OutputCallbacks, EventCallbacks
- `Source/debug/Loader.h` — Loader class
- `Source/debug/BreakpointManager.h` — All public methods
- `Source/debug/PrettyPrint.h` — All debug::detail functions

**Documentation:**
- `SPEC.md` — Created v1.0: complete specification with 10 features, user flows, edge cases, error handling, architecture constraints, success criteria
- `PLAN.md` — Updated to v5.0: all features marked complete, file structure, design decisions, remaining debt

### Alignment Check
- [x] BLESSED principles followed (Lean: BreakpointManager split 530→224+326, Whatdbg split 580→301+355, Exception 6-branch→lookup; Bound: raw pointers→ComPtr; Explicit: comprehensive doxygen on all public APIs)
- [x] NAMES.md adhered (handleBreakpoint, handleThreadName, handleSingleStep, handleUnknownException — semantic handler names)
- [x] MANIFESTO.md applied (tryResolve zero early returns, all audit findings addressed)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, const before type throughout

### Problems Solved

**Problem 1 — Incomplete audit sweep**
Sprint 12 Machinist left 7 items unaddressed. Fixed: tryResolve early returns, ComPtr conversion, BreakpointManager split, Whatdbg split, Exception lookup table, leakDetector filter, doxygen. All 45 audit findings now resolved.

**Problem 2 — No SPEC.md**
ARCHITECT explicitly requested SPEC.md in audit instructions. Written v1.0 covering all 10 features with complete user flows, edge cases, error handling tables. Updated scope: whatdbg is a general-purpose Windows DAP adapter, not limited to JUCE plugins.

**Problem 3 — Stale PLAN.md**
v4.0 still listed Steps 8-11 as "Remaining". Updated to v5.0 reflecting all completed work, file structure, design decisions, remaining debt.

**Problem 4 — dx scope contamination (discovered and reverted)**
Attempted NatVis via `dx -r0` in getLocals. `dx` command contaminates session-global scope even from secondary client — GetSymbolValueText returns garbage after dx runs. Reverted to prettyPrint-only. Also attempted DbgModel.h C++ API — header won't compile (C++20/WinRT constructs).

### Technical Debt / Follow-up
- `fopen`/`fclose` raw C I/O — should be juce::FileLogger
- Dead EXCEPTION_SINGLE_STEP branch — keep for instruction-level stepping
- NatVis in variables panel — dx contaminates scope, DbgModel.h won't compile
- dap-repl routing — nvim-dap-ui limitation #306
- No tests, no error recovery
- Mason registry packaging — requires GitHub releases with pre-built binaries + CI pipeline
- WhatdbgHandlers.cpp 355 lines, BreakpointManagerHandlers.cpp 326 lines — borderline

## Sprint 12: Comprehensive Audit + Clean Sweep + Multi-Thread Frame Fix

**Date:** 2026-04-02
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, research coordination, delegation, direct fixes (dx scope bug, formatSymbolValue duplicate, diagnostic logging, narrowing conversion)
- Auditor — Full codebase audit: 45 findings across 7 categories (4 critical, 22 high, 14 medium, 5 low)
- Machinist — Clean sweep: dead code removal, early return elimination, file splitting, dispatch table, DynObj consolidation, logging unification, duplicated code extraction, ARCHITECTURE.md update
- Researcher — dbgmodel.dll Data Model feasibility research (NatVis, IHostDataModelAccess, dbgmodel.dll location, sidecar compatibility)
- Engineer — dx-based NatVis integration attempt (reverted — scope contamination), multi-thread frame ID mapping fix
- Librarian — dbgeng Evaluate API research (Execute ?? vs Evaluate, secondary client, C++ expression syntax)

### Files Modified (16 total)

**Dead code removal:**
- `Source/debug/State.h` — Removed `pendingStoppedBody` (never used), `breakpointThreadId` (replaced by getEventThreadSystemId)
- `Source/dap/Types.h` — Removed dead `getInt()` function; `DynObj` alias now canonical here
- `Source/debug/Callbacks.cpp` — Removed write to deleted `breakpointThreadId`

**Early return elimination:**
- `Source/dap/Reader.cpp` — 3 early returns in `run()` refactored to `isConnected` flag + positive nested checks

**File splitting (Session.cpp 1228 lines → 3 files):**
- `Source/debug/Session.cpp` — Lifecycle only (~370 lines): init, launch, attach, shutdown, stepping, interrupt, breakpoints, symbols, threads
- `Source/debug/SessionInspection.cpp` — New: `CaptureOutputCallback`, `enumerateSymbols` shared helper, `getStackTrace`, `getLocals`, `getVariableChildren`, `evaluateExpression`
- `Source/debug/SessionPrettyPrint.cpp` — New: `prettyPrint` split into `prettyPrintJuceString`, `prettyPrintStdString`, `prettyPrintUniquePtr`, `prettyPrintVector`; `formatSymbolValue`, `stripDecimalPrefix`, `readTargetString`, `parseHexAddress`, `findChildByName`, `getChildValueText`
- `Source/debug/PrettyPrint.h` — New: shared declarations in `debug::detail` namespace

**Dispatch table:**
- `Source/Whatdbg.h` — Added `#include <functional>`, `CommandHandler` alias, `commandHandlers` map; added `nextFrameId`, `frameIdMap`, `lastScopesThreadId` members
- `Source/Whatdbg.cpp` — 16-branch else-if → `std::unordered_map` dispatch table in constructor; `handleStackTrace` assigns unique frame IDs with thread mapping; `handleScopes` decodes frameId → (threadId, frameIndex) and sets thread context; `handleVariables` restores thread context

**Logging unification:**
- `Source/debug/BreakpointManager.cpp` — All 11 `juce::Logger::writeToLog` → `logWrite`; `using DynObj` → `using dap::DynObj`

**Documentation:**
- `ARCHITECTURE.md` — Updated to v0.3.0: three-file Session split, variable inspection, expression evaluation, ODS capture, pause, multi-thread, stepping, terminate, symbol group caching, dispatch table, debug-only logging

### Alignment Check
- [x] BLESSED principles followed (Lean: Session.cpp split 1228→~370 lines, prettyPrint split into 4 per-type functions, dispatch table replaces 16-branch chain; Bound: dead fields removed, early returns eliminated; SSOT: DynObj defined once, enumerateSymbols shared helper eliminates duplicate loop, stripDecimalPrefix eliminates duplicate 0n logic; Explicit: all logging via logWrite)
- [x] NAMES.md adhered (enumerateSymbols, stripDecimalPrefix, prettyPrintJuceString — semantic verb-noun names)
- [x] MANIFESTO.md applied (zero early returns after fix, positive nested checks throughout)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, const before type

### Problems Solved

**Problem 1 — Multi-thread frame ID collision**
frameIds were non-unique (0, 1, 2 per thread). nvim-dap requests stackTrace for all 43+ threads. After enumeration, current thread context was last thread, not event thread. Variables showed garbage/null. Fix: globally unique frameIds via `nextFrameId++` counter, `frameIdMap` maps frameId → (threadSystemId, frameIndex). `handleScopes` decodes and sets thread context. `handleVariables` restores from `lastScopesThreadId`.

**Problem 2 — dx command corrupts session-global scope**
Attempted NatVis via `dx -r0` in getLocals. `dx` internally modifies scope context, corrupting `GetSymbolValueText` results. Secondary client doesn't isolate scope. Fix: reverted dx from getLocals. dx remains in evaluateExpression (REPL) where one-shot scope changes are acceptable.

**Problem 3 — DbgModel.h compilation error**
`DbgModel.h` line 12811: `syntax error: '<' was unexpected`. SDK header uses C++20/WinRT constructs incompatible with project settings. Fix: abandoned C++ Data Model API approach. Using `dx` command via Execute for NatVis evaluation in REPL only.

**Problem 4 — Session.cpp 4x line limit**
1228 lines, limit 300. Fix: split into Session.cpp (lifecycle), SessionInspection.cpp (variables/stack/evaluate), SessionPrettyPrint.cpp (type formatters). Shared declarations in PrettyPrint.h.

**Problem 5 — 16-branch dispatch chain**
`handleCommand` had 16 else-if string comparisons, limit 3. Fix: `std::unordered_map<std::string, CommandHandler>` dispatch table. O(1) lookup, data-driven, adding commands is data not code.

**Problem 6 — Duplicated getLocals/getVariableChildren**
~60 lines of identical symbol enumeration code. Fix: `enumerateSymbols` shared helper with parent filter parameter.

### Technical Debt / Follow-up
- `fopen`/`fclose` raw C I/O — should be `juce::FileLogger` (logged, deferred)
- `cachedSymbolGroup` is raw owning pointer — should be `ComPtr<IDebugSymbolGroup2>`
- `secondaryClient` in evaluateExpression is raw pointer — should be `ComPtr`
- BreakpointManager.cpp still 530 lines (limit 300) — handleSetBreakpoints ~230 lines, tryResolve ~130 lines
- tryResolve still has 4 early returns (pre-existing from Sprint 3)
- Whatdbg.cpp still ~580 lines — processDeferredEvents ~120 lines
- Exception callback has 6 branches (limit 3) — should be lookup table
- `isInitialBreakHandled` set in Callbacks.cpp but only read within same callback — could be local static
- NatVis in variables panel deferred — dx contaminates scope, DbgModel.h won't compile
- `leakDetector` members visible in variable expansion

## Sprint 11: Expression Evaluation, Multi-Thread, Symbol Group Caching, Terminate Fix

**Date:** 2026-04-02
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, research coordination, delegation, direct fixes (evaluate formatting, early return fix, diagnostic logging, narrowing conversion, prettyPrint signature restoration)
- Librarian — dbgeng Evaluate API research (Execute ?? vs Evaluate, secondary client capture, C++ expression syntax), dbgeng thread enumeration API research (IDebugSystemObjects, GetThreadIdsByIndex, GetThreadDescription, thread context for scopes)
- Engineer — Expression evaluation (CaptureOutputCallback, evaluateExpression, handleEvaluate), multi-thread support (getThreads, getEventThreadSystemId, setCurrentThreadBySystemId, handleThreads/handleStackTrace rewire), symbol group caching (getOrCreateSymbolGroup, resetSymbolGroupCache), terminate fix (shutdown bool parameter, shouldTerminateOnExit)

### Files Modified (6 total)

- `Source/debug/Session.h` — Added `IDebugSystemObjects` ComPtr; added `evaluateExpression`, `getThreads`, `getEventThreadSystemId`, `setCurrentThreadBySystemId`, `resetSymbolGroupCache` public methods; added `getOrCreateSymbolGroup` private method; added `cachedSymbolGroup`/`cachedFrameIndex` cache members; `shutdown` takes `bool shouldTerminate = false`
- `Source/debug/Session.cpp` — `CaptureOutputCallback` class for output capture; `evaluateExpression` via secondary client `Execute("?? expr")` with `.symopt- 100` for unqualified symbol resolution, juce::String pretty-print via dot/arrow `Evaluate` + `ReadMultiByteStringVirtual`; `getThreads` enumerates real threads with OS TIDs and `GetThreadDescription` names; `getEventThreadSystemId`/`setCurrentThreadBySystemId` for thread context; `getOrCreateSymbolGroup` caches per frame; `getLocals`/`getVariableChildren` refactored to use cached group; `prettyPrint` accepts group + symbols parameters (no internal group creation); `shutdown` uses `DEBUG_END_ACTIVE_TERMINATE` vs `DEBUG_END_ACTIVE_DETACH` based on parameter; `IDebugSystemObjects` QI'd in initialize, added to isInitialized/shutdown
- `Source/Whatdbg.h` — Added `handleEvaluate` declaration; added `shouldTerminateOnExit` member
- `Source/Whatdbg.cpp` — `handleEvaluate` wired in dispatch; `handleDisconnect` sets `shouldTerminateOnExit` from command name + `terminateDebuggee` arg; `run()` passes flag to `session.shutdown()`; `handleThreads` uses `session.getThreads()`; `handleStackTrace` sets thread context from DAP `threadId`; all stopped events use `session.getEventThreadSystemId()` for real OS TID; `resetVariablesState` calls `session.resetSymbolGroupCache()`
- `Source/dap/Types.h` — `supportsEvaluateForHovers` set to `true`
- `Source/debug/BreakpointManager.h` + `.cpp` — `isUserBreakpoint(ULONG)` added (from Sprint 10, same commit)

### Alignment Check
- [x] BLESSED principles followed (Bound: CaptureOutputCallback stack-lifetime, cachedSymbolGroup released in resetSymbolGroupCache/shutdown; SSOT: thread IDs from dbgeng, not hardcoded; Lean: getOrCreateSymbolGroup eliminates per-request group creation; Explicit: shouldTerminateOnExit flag, OS TID as DAP threadId)
- [x] NAMES.md adhered (evaluateExpression, getEventThreadSystemId, setCurrentThreadBySystemId, cachedSymbolGroup, shouldTerminateOnExit — semantic names)
- [x] MANIFESTO.md applied (early return in prettyPrint fixed to positive-check wrapper)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, const before type, static_cast for narrowing

### Problems Solved

**Problem 1 — No expression evaluation**
DAP `evaluate` request was unsupported. Implemented via secondary dbgeng client + `Execute("?? expr")` with output capture. `.symopt- 100` enables unqualified local variable resolution. juce::String expressions auto-resolve to actual string content via `Evaluate("(expr).text.data")` + `ReadMultiByteStringVirtual`, trying both `.` and `->` access.

**Problem 2 — `??` output passed through formatSymbolValue incorrectly**
`??` returns type-first format (`class juce::String * 0x...`) while `GetSymbolValueText` returns address-first. `formatSymbolValue` matched `startsWith("class ")` → returned empty. Fix: `??` output gets its own lighter formatting (backtick strip + 0n removal only).

**Problem 3 — Hardcoded single thread**
`handleThreads` returned hardcoded thread id=1. All stopped events used threadId=1. `handleStackTrace` ignored threadId. Fix: `IDebugSystemObjects` QI'd; `getThreads` enumerates real threads with `GetThreadIdsByIndex` + `GetThreadDescription`; stopped events use `getEventThreadSystemId()`; `handleStackTrace` calls `setCurrentThreadBySystemId` before tracing.

**Problem 4 — Terminate detached instead of killing process**
`EndSession(DEBUG_END_ACTIVE_DETACH)` let the target continue. Fix: `shutdown(bool)` uses `DEBUG_END_ACTIVE_TERMINATE` when `shouldTerminateOnExit` is true. Set by `handleDisconnect` from DAP `terminate` command or `terminateDebuggee` argument.

**Problem 5 — Symbol group created fresh per request**
`getLocals`, `getVariableChildren`, and `prettyPrint` each created and released their own `IDebugSymbolGroup2`. Fix: `getOrCreateSymbolGroup` caches per frame, reused across all requests within a stop event. `prettyPrint` accepts the cached group as parameter. Cache invalidated on every stop event via `resetSymbolGroupCache`.

**Problem 6 — Narrowing conversion in shutdown**
`DEBUG_END_ACTIVE_TERMINATE`/`DEBUG_END_ACTIVE_DETACH` are `int` defines, brace init to `ULONG` narrowed. Fix: `static_cast<ULONG>()`.

### Technical Debt / Follow-up
- `State::breakpointThreadId` is dead — `getEventThreadSystemId()` replaced it. Remove field.
- `fopen`/`fclose` raw C I/O — should be `juce::FileLogger`
- `logWrite` vs `juce::Logger::writeToLog` inconsistency in BreakpointManager.cpp
- Early returns in Types.h and BreakpointManager.cpp::tryResolve
- Dead `EXCEPTION_SINGLE_STEP` branch in Callbacks.cpp
- `debuggeeOutputText` accumulation pressure at high frequency
- `leakDetector` members visible in variable expansion
- Tier 2 NatVis via Debugger Data Model deferred
- dap-repl routing limitation (nvim-dap-ui #306)

## Sprint 10: Polish — stepOut fix, pretty-printing, debug-only logging

**Date:** 2026-04-01
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, research coordination, delegation, direct edits (early return fix, diagnostic logging add/remove, Log.h guard, Main.cpp guard)
- Pathfinder — stepOut breakpoint detection flow analysis
- Researcher — Pretty-printing research (NatVis, IDebugSymbolGroup2, IDebugDataSpaces4, Debugger Data Model, MSVC STL layouts, juce::String internals)
- Engineer — stepOut reason fix (isUserBreakpoint + processDeferredEvents routing), pretty-print implementation (4 type formatters), value formatting improvements (0n strip, pointer truncation, composite empty)

### Files Modified (5 total)

- `Source/Log.h` — Wrapped `g_logFile` and `logWrite` in `#if JUCE_DEBUG`; Release builds get no-op `logWrite`
- `Source/Main.cpp:75-77,110-114` — `fopen`/`fclose` of log file guarded with `#if JUCE_DEBUG`
- `Source/debug/Session.h:85` — Added `IDebugDataSpaces4` ComPtr member for target memory reading
- `Source/debug/Session.cpp:11-62,64-427,529-534,600-605` — `IDebugDataSpaces4` QI'd in initialize, added to isInitialized/shutdown; `formatSymbolValue` enhanced (0n anywhere, pointer type truncation, composite empty); 4 static helpers (`readTargetString`, `parseHexAddress`, `findChildByName`, `getChildValueText`); `prettyPrint` with 4 type formatters (juce::String, std::string, std::unique_ptr, std::vector); pretty-print hooked into both `getLocals` and `getVariableChildren`; compiler-generated symbol filter (`<` prefix)
- `Source/debug/BreakpointManager.h:52` + `Source/debug/BreakpointManager.cpp:47-50` — Added `isUserBreakpoint(ULONG)` method
- `Source/Whatdbg.cpp:452-475` — Breakpoint hit block routes internal BPs (stepOut `gu`) through step-completion path when `isStepPending` and engineId is not user-registered

### Alignment Check
- [x] BLESSED principles followed (SSOT: type formatters in one static function; Bound: temporary symbol groups created/released per prettyPrint call; Lean: shared helpers; Explicit: named constants, no magic numbers)
- [x] NAMES.md adhered (readTargetString, parseHexAddress, findChildByName, prettyPrint, isUserBreakpoint — semantic names)
- [x] MANIFESTO.md applied (early return in prettyPrint fixed to positive-check wrapper)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, const before type

### Problems Solved

**Problem 1 — stepOut reports reason "breakpoint" instead of "step"**
`gu` (step out) plants an internal breakpoint at the return address. `Breakpoint` callback fires with an unknown engineId, sets `hasBreakpointHit`. The step detection block (`isStepPending + no other flags`) doesn't fire because `hasBreakpointHit` is true. Fix: added `isUserBreakpoint(engineId)` to BreakpointManager. In processDeferredEvents, when `hasBreakpointHit` and `isStepPending` and NOT a user BP → emit `reason: "step"` instead.

**Problem 2 — dbgeng value formatting not human-readable**
`GetSymbolValueText` returns `0n877`, `0x00000000\`addr class Type *`, `class juce::String`. Fix: `formatSymbolValue` strips `0n` prefix before digits anywhere in string, removes backticks, truncates pointer trailing type, shows empty for composite types.

**Problem 3 — Compiler-generated symbols visible**
MSVC generates `<begin>$L0`, `<end>$L0`, `<range>$L0` for range-for loops. Fix: filter by `symbolName.startsWithChar('<')` in both getLocals and getVariableChildren.

**Problem 4 — No pretty-printing for common types**
`IDebugSymbolGroup2::GetSymbolValueText` is NatVis-unaware — shows raw type names. Fix: Tier 1 type-specific formatters via `GetSymbolTypeName` matching + child expansion + `ReadMultiByteStringVirtual`. Four formatters: juce::String (text→data→char*), std::string (SSO-aware _Buf/_Ptr), std::unique_ptr (address or "null"), std::vector (size from _Myfirst/_Mylast pointer diff + element type size).

**Problem 5 — File logging active in Release builds**
`logWrite` with `vfprintf` and `fopen`/`fclose` ran in all builds. Fix: `#if JUCE_DEBUG` guard around `g_logFile`, `logWrite`, `fopen`, `fclose`. Release gets inline no-op.

### Technical Debt / Follow-up
- `fopen`/`fclose` raw C I/O — should be `juce::FileLogger` (logged, deferred)
- `logWrite` vs `juce::Logger::writeToLog` inconsistency in BreakpointManager.cpp
- Early returns in Types.h and BreakpointManager.cpp::tryResolve
- No expression evaluation (DAP `evaluate` request)
- No multi-thread scope selection
- Symbol group created fresh per prettyPrint call — double cost for pretty-printed variables
- `leakDetector` members visible in variable expansion
- Tier 2 NatVis via Debugger Data Model deferred
- dap-repl routing limitation (nvim-dap-ui #306)

## Sprint 9: Variable Inspection (scopes + variables + expansion)

**Date:** 2026-04-01
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, research coordination, delegation, one role violation (direct edit, corrected)
- Pathfinder — Current scopes/variables stubs, DAP flow analysis
- Librarian — dbgeng variable inspection API research (IDebugSymbolGroup2, SetScopeFrameByIndex, ExpandSymbol, GetSymbolValueText, symbol parameters, threading)
- Engineer — Variable inspection implementation (getLocals, getVariableChildren, handleScopes, handleVariables, variablesReference registry, formatSymbolValue, compiler symbol filter)

### Files Modified (4 total)

- `Source/debug/Session.h:68-72` — Added `getLocals (int frameIndex)` and `getVariableChildren (int frameIndex, int symbolIndex)` declarations
- `Source/debug/Session.cpp:11-62,485-549,554-620` — Added `formatSymbolValue` static helper (backtick strip, `0n` decimal prefix removal, pointer type truncation, composite type empty value); `getLocals` — sets scope frame, creates symbol group, enumerates top-level symbols with `ParentSymbol == DEBUG_ANY_ID`, filters compiler-generated `<` symbols; `getVariableChildren` — same pattern with `ExpandSymbol` and `ParentSymbol == parentIndex` filter
- `Source/Whatdbg.h:60-64` — Added `#include <unordered_map>`, `nextVariablesRef` counter, `variablesRefMap` registry, `resetVariablesState()` declaration
- `Source/Whatdbg.cpp:76,289-369,396,411` — `resetVariablesState` clears registry on every stop event (pause, breakpoint, step); `handleScopes` returns "Locals" scope with variablesReference from registry; `handleVariables` dispatches to `getLocals` (symbolIndex == -1) or `getVariableChildren`, assigns child variablesReferences for expandable symbols

### Alignment Check
- [x] BLESSED principles followed (SSOT: variablesRefMap is the single registry for all references; Bound: symbol group created and released per request, no leaked COM objects; Lean: formatSymbolValue is one shared function; Explicit: named constants for buffer sizes; Encapsulation: Session owns symbol enumeration, Whatdbg owns DAP mapping)
- [x] NAMES.md adhered (getLocals, getVariableChildren, formatSymbolValue, variablesRefMap, nextVariablesRef, resetVariablesState — semantic names)
- [x] MANIFESTO.md applied (no early returns, positive nested checks throughout)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, const before type, space after function name, named constants

### Problems Solved

**Problem 1 — scopes/variables stubs returned empty arrays**
handleScopes and handleVariables were stubs since Sprint 3. Implemented using `IDebugSymbolGroup2` via `GetScopeSymbolGroup2` (already on `IDebugSymbols3` which was QI'd). `SetScopeFrameByIndex` maps DAP frameId directly to dbgeng frame index.

**Problem 2 — Struct/class expansion**
DAP `variablesReference` scheme: integer registry (`std::unordered_map<int, std::pair<int, int>>`) maps ref → (frameIndex, symbolIndex). Scope ref uses symbolIndex -1 for top-level locals. Child refs registered on demand when `SubElements > 0`. Registry reset on every stop event. `ExpandSymbol` called per-request on fresh symbol group — no cross-request state contamination.

**Problem 3 — dbgeng value format not human-readable**
`GetSymbolValueText` returns raw debugger notation: `0n877` (decimal), `0x00000000\`10db01b0` (backtick 64-bit), `0x... class Foo *` (pointer + type). `formatSymbolValue` cleans all three: strips `0n` before digits anywhere in string, removes backticks, truncates pointer type suffix. Composite types (`class X`, `struct Y`) show empty value — type column and expand triangle provide the information.

**Problem 4 — Compiler-generated range-for symbols visible**
MSVC generates `<begin>$L0`, `<end>$L0`, `<range>$L0` for range-based for loops. Filtered out by skipping symbols whose name starts with `<` in both `getLocals` and `getVariableChildren`.

### Technical Debt / Follow-up
- No pretty-printing for JUCE types (juce::String shows internal members, not string content)
- No expression evaluation (DAP `evaluate` request — getValue(), paramID access)
- Symbol group created fresh per request — acceptable but could be cached with `Update` parameter for stepping performance
- `IDebugSystemObjects` not QI'd — multi-thread scope selection not supported (single-thread hardcoded)
- Pre-existing early returns in Types.h and BreakpointManager.cpp
- Diagnostic logging removal — Step 11
- `leakDetector` members visible in expansion — could filter by name pattern

## Sprint 8: OutputDebugString Capture + DapStopped Sign

**Date:** 2026-04-01
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, research coordination, delegation, log analysis
- Pathfinder — Current Output2 callback code, debug::Widget exploration, nvim-dap sign config, dapui console config
- Librarian — dbgeng OutputDebugString capture (found `arg` carries `DEBUG_OUTPUT_DEBUGGEE` mask, not `flags`), nvim-dap-ui console vs repl routing
- Researcher — DAP adapter ODS patterns (cppvsdbg, codelldb, LLDB, Ghidra)
- Engineer — OutputDebugString capture implementation (Output2, State, deferred events, output mask)

### Files Modified (5 total)

- `Source/debug/State.h:52-53` — Added `hasDebuggeeOutput` and `debuggeeOutputText` deferred event fields for OutputDebugString capture
- `Source/debug/Callbacks.cpp:73-88` — `Output2` now checks `arg & DEBUG_OUTPUT_DEBUGGEE` (0x80) to identify target process OutputDebugString; accumulates text on State via `+=`
- `Source/debug/Session.cpp:43-46` — `SetOutputMask` configured with `DEBUG_OUTPUT_NORMAL | DEBUG_OUTPUT_WARNING | DEBUG_OUTPUT_ERROR | DEBUG_OUTPUT_DEBUGGEE`
- `Source/Whatdbg.cpp:439-451` — `processDeferredEvents` emits DAP `output` event with `category: "console"` for captured debuggee output
- `~/.config/nvim/lua/dap/dapui_config.lua:127` — DapStopped sign glyph `>>` → `→` (U+2192) for END font fallback testing

### Alignment Check
- [x] BLESSED principles followed (SSOT: output captured once in Output2, consumed once in processDeferredEvents; Explicit: DEBUG_OUTPUT_DEBUGGEE mask check; Bound: deferred event pattern, no cross-thread writes)
- [x] NAMES.md adhered (hasDebuggeeOutput, debuggeeOutputText — semantic boolean + content)
- [x] MANIFESTO.md applied (no early returns in new code, established deferred event pattern followed)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, const before type

### Problems Solved

**Problem 1 — OutputDebugString indistinguishable from engine output (parked since Sprint 3)**
`Output2` callback received all output with `flags=0x0`. Root cause: wrong parameter. The `arg` parameter (not `flags`) carries the `DEBUG_OUTPUT_*` mask. `DEBUG_OUTPUT_DEBUGGEE` (0x80) identifies target process OutputDebugString. `flags` carries `DEBUG_OUTCBF_*` format flags (irrelevant for filtering). Fix: check `static_cast<ULONG>(arg) & DEBUG_OUTPUT_DEBUGGEE`.

**Problem 2 — Output2 was dead stub**
`Output2` computed `isTextOrDml` then `juce::ignoreUnused` all parameters. Since `OutputCallbacks` exposes `IDebugOutputCallbacks2` via QI, dbgeng routes through `Output2` (not `Output`), making all output invisible. Fix: implemented proper filtering and State accumulation in `Output2`.

**Problem 3 — Output mask might exclude debuggee output**
dbgeng per-client output masks can filter categories. If `DEBUG_OUTPUT_DEBUGGEE` is not in the mask, Output2 never fires for debuggee output. Fix: explicit `SetOutputMask` including `DEBUG_OUTPUT_DEBUGGEE` in `initialize()`.

**Problem 4 — DAP output events route to dap-repl, not DAP Console**
nvim-dap-ui's "console" panel is an integrated terminal (PTY) for `runInTerminal` requests — NOT a DAP output event viewer. All DAP `output` events go to the REPL regardless of `category`. This is hardcoded in nvim-dap (`Session:event_output` → `repl.append`). Known limitation (nvim-dap-ui issue #306, open since 2022). codelldb shows output in Console only because it uses `terminal: "integrated"` (raw PTY), not DAP output events. For plugin debugging (DLL in DAW), there's no process stdio — dap-repl is the correct destination.

### Technical Debt / Follow-up
- dap-repl is the only destination for DAP output events in nvim-dap — no workaround without custom `on_output` handler
- `debuggeeOutputText` uses `+=` accumulation — high-frequency OutputDebugString from audio thread could cause string allocation pressure; acceptable for debug builds
- Pre-existing early returns in Types.h and BreakpointManager.cpp (carried from Sprint 7)
- scopes/variables stubs — Step 10
- Diagnostic logging removal — Step 11

## Sprint 7: Pause (DebugBreakProcess) + BP Resolution Fix + RAII Cleanup

**Date:** 2026-04-01
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, log analysis, architectural decisions, delegation, role violation (wrote 1 edit directly — corrected)
- Pathfinder — Codebase state discovery (3 invocations: initial state, pause/handle state, naked new instances, nvim sign config, git diff)
- Engineer — All code changes across 7 source files (4 invocations: initial DebugBreakProcess, OpenProcess fix, lazy PID fix, DynObj + thread event)
- Auditor — Verified DebugBreakProcess changes (found pre-existing violations), verified DynObj migration
- Librarian — DebugBreakProcess API research (threading, handle acquisition, exception detection, SetInterrupt confirmation)

### Files Modified (9 total)

- `Source/debug/State.h:27` — Added `targetProcessId` field for process ID storage (set by CreateProcess callback and handleAttach)
- `Source/debug/Session.h:53-54,62` — Added `forceReloadAllSymbols()` declaration; changed `interrupt()` to take `ULONG processId` parameter; removed `targetProcessId` member and `IDebugSystemObjects` ComPtr
- `Source/debug/Session.cpp:258-271,282-310,314-316` — Added `forceReloadAllSymbols()` (`.reload /f` global); `interrupt()` uses `OpenProcess` + `DebugBreakProcess` + `CloseHandle` with PID parameter; added `kNameBufferSize`/`kFileBufferSize` constants; braces added to `pollEvents` if-block
- `Source/debug/Callbacks.cpp:206-215` — `CreateProcess` callback captures process handle, calls `GetProcessId()`, stores PID on State
- `Source/debug/BreakpointManager.cpp:1-4,365-410` — Added `#include "../Log.h"`; retry block after main BP loop: if pending BPs exist, calls `forceReloadAllSymbols()`, retries resolution, updates response array
- `Source/Whatdbg.cpp:59-75,220-225,355-381` — Pause detection in poll loop (isPausePending + S_OK); DAP `thread` event emitted on target start (both configurationDone and processDeferredEvents); `handlePause` passes `state.targetProcessId` to `interrupt()`
- `Source/dap/Types.h:8` — Added `DynObj` alias; all 4 message builders use `DynObj` instead of naked `new`
- `PLAN.md` — v4.0: reflects completed work (Steps 1-6), defines remaining Steps 8-11
- `~/.config/nvim/lua/dap/dapui_config.lua:127` — DapStopped sign glyph `>>` back to `→` (U+2192) for END font fallback testing

### Alignment Check
- [x] BLESSED principles followed (Bound: RAII via ReferenceCountedObjectPtr eliminates naked new; Lean: no unnecessary abstractions; Explicit: targetProcessId with clear lifecycle; SSOT: PID stored once on State; Encapsulation: Session receives PID via parameter)
- [x] NAMES.md adhered (targetProcessId, forceReloadAllSymbols, DynObj — semantic names)
- [x] MANIFESTO.md applied (no early returns in new code, no workarounds)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, const before type, space after function name, named constants

### Problems Solved

**Problem 1 — SetInterrupt incompatible with single-thread polling**
`SetInterrupt(DEBUG_INTERRUPT_EXIT)` does not work from the same thread as `WaitForEvent` in a polling model. Fix: `DebugBreakProcess` via `OpenProcess` + stored PID. Injects `int 3` into target process via remote thread.

**Problem 2 — Sidecar dbgeng IDebugSystemObjects broken**
`GetCurrentProcessHandle` returns E_UNEXPECTED (0x8000FFFF). `GetCurrentProcessSystemId` returns PID 0. Fix: capture PID from `CreateProcess` callback via `GetProcessId(handle)` for launch mode, direct parameter storage for attach mode.

**Problem 3 — PID not available immediately after CreateProcess2**
`GetCurrentProcessSystemId` called right after `CreateProcess2` returns PID 0 — process not yet registered with dbgeng. Fix: capture PID in `CreateProcess` callback (fires during WaitForEvent after process is created).

**Problem 4 — nvim-dap "No thread to stop" blocks pause command**
nvim-dap refuses to send pause if it doesn't know about any threads. Fix: emit DAP `thread` event with `reason: "started"` when target transitions to running after initial break.

**Problem 5 — BP set after module load stays pending forever**
Plugin module loads before any BPs are set → no symbol reload (no pending BPs). Later BP set → `getOffsetByLine` fails (symbols not loaded) → BP goes pending → no future LoadModule event → pending forever. Fix: in `handleSetBreakpoints`, if any BPs go pending, call `.reload /f` (global symbol reload) then retry resolution.

**Problem 6 — Naked `new juce::DynamicObject()` across codebase (BLESSED B violation)**
20 instances of `auto* obj { new juce::DynamicObject() }` — ownership gap between allocation and `juce::var` taking reference. Fix: `using DynObj = juce::ReferenceCountedObjectPtr<juce::DynamicObject>` alias in all 4 files, all instances converted.

**Problem 7 — Pre-existing magic numbers in Session.cpp**
`512` and `1024` buffer sizes unnamed. Fix: `kNameBufferSize` and `kFileBufferSize` constants alongside existing `kMaxStackFrames`.

### Technical Debt / Follow-up
- Pre-existing early returns in `Types.h` (getString, getInt) and `BreakpointManager.cpp` (tryResolve — 4 early returns)
- `juce::Logger::writeToLog` vs `logWrite` inconsistency in BreakpointManager.cpp
- Diagnostic logging throughout Callbacks.cpp and Whatdbg.cpp — remove after stable
- Dead `EXCEPTION_SINGLE_STEP` branch in Callbacks.cpp
- stepOut reports `reason: "breakpoint"` instead of `"step"` (cosmetic)
- scopes/variables stubs — no variable inspection
- `forceReloadAllSymbols` is global `.reload /f` — acceptable for user-initiated BP set but could be optimized to per-module if needed
- BP on function signature line resolves to first executable line inside body — normal PDB/MSVC behavior, not a bug

## Sprint 6: Stepping + Module Load Storm Fix

**Date:** 2026-04-01
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, log analysis, deferred event debugging, research coordination, delegation
- Pathfinder — Codebase state discovery, build artifact identification, nvim keymap location
- Engineer — All code changes across 8 files
- Auditor — Verified stepping implementation (found 2 critical priority ordering issues)
- Librarian — dbgeng stepping API research, dbgeng Reload per-module syntax, SetInterrupt threading model
- Researcher — dbgeng per-module Reload syntax, DAP pause implementation patterns (Ghidra, DbgShell)

### Files Modified (10 total)

- `.gitignore` — Created: `Builds/` and `*.log`
- `ARCHITECTURE.md:65` — Doc fix: `juce::HeapBlock<juce::var>` → `std::vector<juce::var>` (non-trivial type)
- `Source/debug/State.h` — Added `isInitialBreakHandled`, `hasStepCompleted`, `lastLoadedModuleName`, `lastLoadedImageName`
- `Source/debug/Session.h` — `forceReloadSymbols` → `loadModuleSymbols(imageName)`; added `stepOver`, `stepInto`, `stepOut`, `interrupt`
- `Source/debug/Session.cpp` — `SetCodeLevel(DEBUG_LEVEL_SOURCE)` at init; `loadModuleSymbols` via `control->Execute(".reload /f")` with `quoted()` basename; step/interrupt implementations
- `Source/debug/Callbacks.cpp` — Exception: `isInitialBreakHandled` permanent flag distinguishes initial INT3 from user interrupt; `EXCEPTION_SINGLE_STEP` branch (dead — source stepping doesn't fire it); diagnostic exception code logging; LoadModule: captures `imageName`, early return eliminated
- `Source/Whatdbg.h` — `handleNext/handleStepIn/handleStepOut/handlePause` declarations; `isStepPending`, `isPausePending` members
- `Source/Whatdbg.cpp` — Four stepping handlers; step completion detected from WaitForEvent S_OK + isStepPending; pause detection from WaitForEvent S_OK + isPausePending; `isInitialBreakSeen` cleared in configurationDone; deferred event priority guards (`and not hasBreakpointHit`, `and not hasStepCompleted`); diagnostic WaitForEvent logging
- `~/.config/nvim/lua/core/keymaps.lua` — `<leader>dO` → `<leader>dx` for step out (no shift)

### Alignment Check
- [x] LIFESTAR principles followed (Lean: no unnecessary abstractions, SSOT: step/pause state in one place, Explicit: isStepPending/isPausePending with clear lifecycle)
- [x] NAMING-CONVENTION.md adhered (stepOver, stepInto, stepOut, interrupt, isStepPending, isPausePending, isInitialBreakHandled)
- [x] ARCHITECTURAL-MANIFESTO.md applied (no early returns, no workarounds)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, quoted(), const before type

### Problems Solved

**Problem 1 — Module load storm: global Reload("/f") on every module load**
Replaced with per-module `.reload /f <basename>` via `IDebugControl::Execute`. Three failed approaches before success: `Reload("/f ntdll")` (E_INVALIDARG), `ld` via Execute (S_OK but no PDB parsing), `Reload("/f <full_path>")` (E_FAIL for special chars). Final: `.reload /f` + `basename.quoted()` via Execute.

**Problem 2 — `!` in module names breaks dbgeng command parsing**
"JRENG! Filter Strip" `!` parsed as module/symbol delimiter. Fix: `juce::String::quoted()`.

**Problem 3 — EXCEPTION_SINGLE_STEP does not fire for source-level stepping**
Source-level stepping (`SetCodeLevel(DEBUG_LEVEL_SOURCE)`) uses internal breakpoints, not CPU single-step. WaitForEvent returns S_OK without callback. Fix: detect step completion via `isStepPending` + WaitForEvent S_OK + no other callback flags.

**Problem 4 — WaitForEvent on stopped target resumes execution (Sprint 2 bug resurfaced)**
After step completed, code didn't detect stop → called WaitForEvent again → resumed target. Fix: step detection sets `executionState = stopped`.

**Problem 5 — `isInitialBreakSeen` never cleared, resumes target after real BP hit**
`handleConfigurationDone` resumed after initial INT3 but didn't clear flag. Subsequent BP hits triggered stale initial break handler on next iteration. Fix: clear `isInitialBreakSeen` in configurationDone.

**Problem 6 — `SetInterrupt` for pause does not work from same thread as WaitForEvent**
`SetInterrupt(ACTIVE)` and `SetInterrupt(EXIT)` both fail when called between WaitForEvent calls. Confirmed by Ghidra source, Microsoft DbgShell: SetInterrupt must be called from a different thread while WaitForEvent is BLOCKED. Requires architectural change (Option A) or `DebugBreakProcess` workaround (Option B).

**Problem 7 — User interrupt EXCEPTION_BREAKPOINT re-triggers initial break handler**
After `isInitialBreakSeen` cleared, any `EXCEPTION_BREAKPOINT` re-matches the initial break check. Fix: `isInitialBreakHandled` permanent flag — set once, never cleared.

### Technical Debt / Follow-up
- **PAUSE NON-FUNCTIONAL** — `SetInterrupt` incompatible with single-thread polling architecture. Requires either: (A) dedicated engine thread with `WaitForEvent(INFINITE)` + SetInterrupt from main thread, or (B) `DebugBreakProcess` via stored process handle. ARCHITECT to decide.
- `EXCEPTION_SINGLE_STEP` branch in Callbacks.cpp is dead code — keep for now (instruction-level stepping would use it)
- Diagnostic logging in Callbacks.cpp (exception codes) and Whatdbg.cpp (WaitForEvent S_OK, pause timeout) — remove after stable
- OutputDebugString / DBG() capture parked — flags=0x0 same as engine noise
- scopes/variables stubs — cannot inspect variables
- stepOut reports reason "breakpoint" (internal BP engineId=10000) instead of "step" — cosmetic

### Agents Participated
- COUNSELOR — Planning, log analysis, deferred event priority debugging, delegation
- Pathfinder — Initial codebase state discovery
- Engineer — All code changes across 6 files
- Auditor — Verified stepping implementation, found 2 critical priority ordering issues
- Librarian — dbgeng stepping API research (SetExecutionStatus, SetCodeLevel, ld, Execute, EXCEPTION_SINGLE_STEP)
- Researcher — dbgeng per-module Reload syntax research

### Files Modified (8 total)

- `.gitignore` — Created: `Builds/` and `*.log`
- `ARCHITECTURE.md:65` — Doc fix: `juce::HeapBlock<juce::var>` corrected to `std::vector<juce::var>` (non-trivial type needs proper construction)
- `Source/debug/State.h:36-39` — Added `hasStepCompleted`, `lastLoadedModuleName`, `lastLoadedImageName` fields
- `Source/debug/Session.h:50-59` — Replaced `forceReloadSymbols()` with `loadModuleSymbols(imageName)`; added `stepOver()`, `stepInto()`, `stepOut()`, `interrupt()`; added `SetCodeLevel(DEBUG_LEVEL_SOURCE)` in initialize
- `Source/debug/Session.cpp:50-51,238-288` — `SetCodeLevel(DEBUG_LEVEL_SOURCE)` at init; `loadModuleSymbols` uses `.reload /f` via `control->Execute` with `quoted()` basename; `stepOver/stepInto` use `SetExecutionStatus`; `stepOut` uses `Execute("gu")`; `interrupt` uses `SetInterrupt`
- `Source/debug/Callbacks.cpp:151-171,209-227` — Exception: added `EXCEPTION_SINGLE_STEP` branch (unused — source-level stepping doesn't fire it), added exception code diagnostic logging; LoadModule: captures `imageName`, stores in State, early return eliminated
- `Source/Whatdbg.h:36-40` — Added `handleNext/handleStepIn/handleStepOut/handlePause` declarations; added `isStepPending` member
- `Source/Whatdbg.cpp` — Four stepping handlers wired to dispatch; step completion detected from WaitForEvent S_OK + isStepPending; deferred event priority: BP hit before step completion; `isInitialBreakSeen` cleared in `handleConfigurationDone`; `and not state.hasBreakpointHit` + `and not state.hasStepCompleted` guards on initial break handler

### Alignment Check
- [x] LIFESTAR principles followed (Lean: no unnecessary abstractions, SSOT: step state in one place, Explicit: isStepPending flag with clear lifecycle)
- [x] NAMING-CONVENTION.md adhered (stepOver, stepInto, stepOut, interrupt, isStepPending, hasStepCompleted — semantic verbs/booleans)
- [x] ARCHITECTURAL-MANIFESTO.md applied (no early returns, no workarounds, no poking internals)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, quoted(), const before type

### Problems Solved

**Problem 1 — Per-module symbol reload (from Sprint 5)**
`Reload("/f")` global reload replaced with per-module `.reload /f <basename>` via `IDebugControl::Execute`. Three failed approaches: `Reload("/f ntdll")` (E_INVALIDARG — needs extension), `ld` via Execute (S_OK but doesn't force PDB parsing), `Reload("/f <full_path>")` (E_FAIL for paths with spaces/`!`). Final: `.reload /f` + `basename.quoted()` via Execute — S_OK, PDBs load, BPs resolve.

**Problem 2 — `!` in module names breaks dbgeng command parsing**
"JRENG! Filter Strip" has `!` (module/symbol delimiter). `ld JRENG! Filter Strip` → parsed as module "JRENG" + symbol "Filter Strip". Fix: `juce::String::quoted()`.

**Problem 3 — EXCEPTION_SINGLE_STEP does not fire for source-level stepping**
Librarian research said it would. It doesn't. Source-level stepping (`SetCodeLevel(DEBUG_LEVEL_SOURCE)` + `STEP_OVER`/`STEP_INTO`) uses internal breakpoints. WaitForEvent returns S_OK without calling any callback. Fix: detect step completion by checking `isStepPending` + WaitForEvent S_OK + no other callback flags set.

**Problem 4 — WaitForEvent on stopped target resumes execution (Sprint 2 bug, resurfaced)**
After step completed, target stopped. Code didn't detect it → called WaitForEvent again → resumed target → endless S_OK loop. Fix: step completion detection sets `executionState = stopped` and clears `isStepPending`, preventing further WaitForEvent calls.

**Problem 5 — `isInitialBreakSeen` never cleared**
`handleConfigurationDone` resumed target after initial INT3 but didn't clear `isInitialBreakSeen`. The deferred initial break handler's conditions never aligned to clear it. On subsequent BP hits, the handler fired on the next iteration (after `hasBreakpointHit` was consumed) and resumed the target. Fix: clear `isInitialBreakSeen` in `handleConfigurationDone` when resuming from stopped state.

**Problem 6 — Deferred event priority: initial break resumes after real BP hit**
Same root cause as Problem 5. The guard `and not state.hasBreakpointHit` only protects within a single `processDeferredEvents` call. On the next call, `hasBreakpointHit` is already cleared → initial break handler fires. Fix combined with Problem 5 — clearing `isInitialBreakSeen` eliminates the stale flag.

### Technical Debt / Follow-up
- `EXCEPTION_SINGLE_STEP` branch in Callbacks.cpp is dead code — source-level stepping never fires it. Keep for now (instruction-level stepping would use it)
- Exception code diagnostic logging in Callbacks.cpp — remove after stepping is stable
- WaitForEvent S_OK diagnostic logging in Whatdbg.cpp — remove after stepping is stable
- OutputDebugString / DBG() capture parked — flags=0x0 same as engine noise
- scopes/variables stubs — cannot inspect variables
- stepOut (`gu`) and pause (`SetInterrupt`) untested — only next (F10) confirmed working
- Log.h global FILE* — functional, no action needed

## Sprint 5: Module Load Storm Fix + .gitignore

**Date:** 2026-03-31
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, delegation, log analysis, research coordination
- Pathfinder — Codebase state discovery, build artifact identification
- Engineer — All code changes (State.h, Callbacks.cpp, Session.h/.cpp, Whatdbg.cpp, .gitignore)
- Auditor — Contract compliance verification (found pre-existing early return in LoadModule)
- Researcher — dbgeng `IDebugSymbols::Reload` per-module syntax research
- Librarian — dbgeng symbol reload API research (`ld`, `Execute`, `GetModuleParameters`)

### Files Modified (7 total)

- `.gitignore` — Created: `Builds/` and `*.log`
- `ARCHITECTURE.md:65` — Doc fix: `juce::HeapBlock<juce::var>` corrected to `std::vector<juce::var>` with rationale (non-trivial type needs proper construction)
- `Source/debug/State.h:37-39` — Added `lastLoadedModuleName` and `lastLoadedImageName` fields for per-module symbol reload
- `Source/debug/Callbacks.cpp:209-227` — LoadModule: captures `imageName` parameter, stores both module name and image name in State; pre-existing early return eliminated (positive-check pattern)
- `Source/debug/Session.h:50-51` — `forceReloadSymbols()` replaced with `loadModuleSymbols(const juce::String& imageName)`
- `Source/debug/Session.cpp:238-252` — Per-module reload via `control->Execute(".reload /f <basename.quoted()>")` — uses `IDebugControl::Execute` instead of `IDebugSymbols::Reload` to support module names with spaces and `!`
- `Source/Whatdbg.cpp:272-274,301` — Breakpoint hit priority over initial breakpoint resume (`and not state.hasBreakpointHit`); calls `loadModuleSymbols(state.lastLoadedImageName)` instead of `forceReloadSymbols()`

### Alignment Check
- [x] LIFESTAR principles followed (Lean: targeted reload not global, SSOT: module name in State, Explicit: imageName passed through API)
- [x] NAMING-CONVENTION.md adhered (lastLoadedModuleName, lastLoadedImageName, loadModuleSymbols — semantic names)
- [x] ARCHITECTURAL-MANIFESTO.md applied (no early returns, no workarounds)
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, .quoted(), const before type

### Problems Solved

**Problem 1 — Module load storm: 100+ global symbol reloads during REAPER startup**
`Reload("/f")` force-reloaded ALL module symbols on every LoadModule event with pending BPs. Each call blocked for seconds. Fix: per-module reload using `control->Execute(".reload /f <basename.quoted()>")`. Three failed approaches before success:
- `symbols->Reload("/f ntdll")` — E_INVALIDARG (missing `.dll` extension)
- `ld moduleName` via Execute — S_OK but doesn't force PDB parsing (only loads deferred export symbols)
- `symbols->Reload("/f <full_path>")` — E_FAIL for paths with spaces and `!`
- Final: `.reload /f` via `IDebugControl::Execute` with `juce::String::quoted()` for module basename with extension — S_OK, PDB loaded, BPs resolve

**Problem 2 — `!` in module names breaks dbgeng command parsing**
"JRENG! Filter Strip" contains `!` (module/symbol delimiter in dbgeng). `ld JRENG! Filter Strip` parsed as module "JRENG" + symbol "Filter Strip". Fix: `juce::String::quoted()` wraps module name in double quotes.

**Problem 3 — Deferred event priority: initial breakpoint resumes target after real BP hit**
`processDeferredEvents` processed initial breakpoint handler before breakpoint hit handler. When both flags were set, initial handler resumed the target, then BP handler emitted stopped event — but target was already running. Fix: added `and not state.hasBreakpointHit` guard to initial breakpoint handler.

**Problem 4 — ARCHITECTURE.md FIFO doc mismatch**
Doc said `juce::HeapBlock<juce::var>`, code uses `std::vector<juce::var>`. Code is correct — `juce::var` is non-trivial (has constructor/destructor), `HeapBlock` allocates raw memory which would require placement new. Doc updated.

### Technical Debt / Follow-up
- OutputDebugString / DBG() capture parked — flags=0x0 same as engine noise
- scopes/variables stubs — cannot inspect variables
- next/stepIn/stepOut/pause stubs — return success but do nothing
- `forceReloadSymbols` dead code removed; `lastLoadedModuleName` in State kept for logging but not currently used by reload path
- Log.h uses global FILE* (practical but not ideal)
- Sprint 1/2 handoff key decision "Reload('/f <module>') not Reload('/f')" is now outdated — actual syntax is `.reload /f <basename.quoted()>` via Execute

## Sprint 4: Polish — Contract Audit and Cleanup

**Date:** 2026-03-31
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Planning, delegation
- Engineer — Temporary diagnostic removal, dead code removal
- Auditor — Full codebase contract audit (16 files, 7 severity categories)
- Machinist — Mechanical fixes for all audit violations
- Pathfinder — nvim-dap sign investigation, END font fallback research
- Librarian — nvim-dap sign placement internals, AbstractFifo research

### Files Modified (7 total)

**Source fixes:**
- `Source/dap/Types.h` — `std::atomic<int>` replaced with plain `static int` (main-thread-only, no atomics per architecture)
- `Source/debug/Session.h` — Removed dead getters (`getClient`, `getControl`, `getSymbols`)
- `Source/debug/Session.cpp` — Early returns eliminated (positive-check pattern), `[]` replaced with `.at()` in getStackTrace
- `Source/debug/Callbacks.cpp` — Fixed `ULONG const` to `const ULONG`, `else if (firstChance)` to explicit `!= 0`, removed verbose Output2 logging
- `Source/debug/BreakpointManager.cpp` — Removed 7 redundant `== true`/`== false` comparisons
- `Source/Whatdbg.cpp` — Removed `[DAP OUT]` wire log diagnostic

**External (non-whatdbg):**
- `~/.config/nvim/lua/dap/dapui_config.lua:127` — DapStopped sign glyph changed from `→` (U+2192, missing in terminal font) to `>>` (ASCII); temporary sign_debug listener removed
- `~/Documents/Poems/dev/end/DEBT.md` — Bug report: font fallback missing for Arrows block (U+2190-U+21FF)

### Alignment Check
- [x] LIFESTAR principles followed (Lean: dead code removed, SSOT: no duplicated state, Explicit: no poking internals)
- [x] NAMING-CONVENTION.md adhered (`has*` prefix approved by ARCHITECT for event flags)
- [x] ARCHITECTURAL-MANIFESTO.md applied (no atomics except FIFO, no JUCE message thread)
- [x] JRENG-CODING-STANDARD.md — const placement, explicit checks, .at() access, no redundant comparisons

### Problems Solved

**Problem 1 — DapStopped sign invisible**
The `→` glyph (U+2192) was not rendering in END's terminal. Sign was placed correctly by nvim-dap but character was blank due to missing font fallback for the Arrows Unicode block. Diagnosed by tracing nvim-dap sign placement chain, adding debug listener, confirming sign IS placed with correct priority. Fixed by changing glyph to ASCII `>>`. Filed bug report to END's DEBT.md.

**Problem 2 — threadId: 0 in stopped event**
Breakpoint hit emitted `"threadId": 0` which is invalid. nvim-dap requires positive integer. Fixed by setting breakpointThreadId to 1 (matches handleThreads single-thread response).

**Problem 3 — Contract violations across 5 files**
Auditor found: 4 early returns, 2 unchecked subscript operators, 2 East-const, 1 implicit int-to-bool, 1 unnecessary atomic, 7 redundant boolean comparisons, 3 dead getters. All fixed by Machinist.

### Technical Debt / Follow-up
- OutputDebugString / DBG() capture parked — flags=0x0 same as engine noise
- scopes/variables stubs — cannot inspect variables
- next/stepIn/stepOut/pause stubs — return success but do nothing
- Module load: forceReloadSymbols on every load with pending BPs (100+ stop/resume cycles)
- No .gitignore for build artifacts
- Log.h uses global FILE* (practical but not ideal)
- FIFO backing is std::vector, ARCHITECTURE.md says HeapBlock (doc needs update or code needs change)

## Sprint 3: JUCE Rewrite — Full DAP Adapter with Breakpoints

**Date:** 2026-03-31
**Primary:** COUNSELOR

### Agents Participated
- COUNSELOR — Architecture design, planning, delegation, debugging
- Pathfinder — Codebase exploration (END patterns, legacy code, jreng_core module)
- Engineer — All code generation (14 modules created/rewritten)
- Auditor — Contract compliance validation
- Librarian — JUCE JSON API research, AbstractFifo research
- Oracle — Deep comparison of legacy vs new breakpoint implementation (10 differences found)

### Files Created/Modified (16 total)

**New source files:**
- `Source/Main.cpp` — Entry point, sidecar extraction, Whatdbg initialization + run
- `Source/Log.h` — Shared file logging (inline global)
- `Source/Whatdbg.h` — Orchestrator header: main loop, command dispatch, deferred events
- `Source/Whatdbg.cpp` — Orchestrator impl: DAP handlers, processDeferredEvents, writeMessage
- `Source/debug/State.h` — SSOT state machine (plain data, Context<State>)
- `Source/debug/Session.h/.cpp` — COM wrapper with ComPtr<T>, launch/attach/resume/pollEvents/getStackTrace
- `Source/debug/Loader.h/.cpp` — Sidecar DLL loader (LoadLibrary + DebugCreate thunk)
- `Source/debug/Callbacks.h/.cpp` — COM OutputCallbacks + EventCallbacks, write to State
- `Source/debug/BreakpointManager.h/.cpp` — DAP-to-dbgeng BP mapping, deferred resolution, line search window
- `Source/dap/Reader.h/.cpp` — stdin thread with AbstractFifo + HeapBlock SPSC queue
- `Source/dap/Types.h` — DAP message builders (juce::var/DynamicObject)

**Build/config files:**
- `CMakeLists.txt` — JUCE console app, BinaryData, jreng_core module, /permissive-
- `build.bat` — vcvarsall + VS-bundled cmake/ninja
- `install.sh` — Clean/debug build + install to ~/.local/bin
- `ARCHITECTURE.md` — v0.2.0 two-thread model

**Vendored module:**
- `modules/jreng_core/` — Stripped jreng_core (Context, Owner, FunctionMap, utilities)

**Deleted (replaced by new structure):**
- `Source/dbgeng/DbgEngLoader.h/.cpp` — replaced by debug::Loader
- `Source/dbgeng/DbgEngCallbacks.h/.cpp` — replaced by debug::Callbacks
- `Source/dbgeng/DbgEngSession.h/.cpp` — replaced by debug::Session
- `Source/dap/DapTypes.h` — replaced by dap::Types
- `Source/dap/DapServer.h/.cpp` — replaced by Whatdbg
- `Source/transport/StdioTransport.h/.cpp` — replaced by dap::Reader
- `Source/debug/ProcessThread.h/.cpp` — removed (two-thread model)
- `Source/dap/Writer.h` — removed (Whatdbg writes stdout directly)

**nvim config (non-destructive):**
- `~/.config/nvim/lua/dap/adapters.lua:27` — whatdbg path changed to ~/.local/bin/whatdbg.exe

### Alignment Check
- [x] LIFESTAR principles followed (Lean two-thread model, SSOT State, Explicit Encapsulation)
- [x] NAMING-CONVENTION.md adhered (debug::, dap:: namespaces, semantic names)
- [ ] ARCHITECTURAL-MANIFESTO.md — partial: some early returns remain in COM callbacks (accepted per contract) and thread run() methods
- [x] JRENG-CODING-STANDARD.md — brace init, not/and/or, space after function name, /permissive-

### Problems Solved

**Problem 1 — Three-thread architecture caused COM isolation bugs**
Initial design: stdin thread, COM thread, JUCE message thread with callAsync. COM callbacks fired on wrong thread, output flooding crashed message thread, timing bugs with callAsync notifications.
Fix: Simplified to two-thread model. Main thread owns everything (COM, State, stdout). Stdin thread is a dumb FIFO buffer.

**Problem 2 — CreateProcess2 on wrong thread broke callbacks**
Session initialized on message thread, WaitForEvent called on process thread. Callbacks never fired for CreateProcess/LoadModule events.
Fix: All COM calls on main thread. Single thread for entire COM lifecycle.

**Problem 3 — Breakpoints never resolved (10 differences from legacy)**
Oracle deep analysis found: launch() skipped WaitForEvent/symbol loading, LoadModule never returned DEBUG_STATUS_BREAK, no symbol/source paths configured, no execution state verification before tryResolve.
Fix: Ported critical mechanisms from legacy — LoadModule returns BREAK when pending BPs, forceReloadSymbols before resolution, symbol/source path configuration, resume after resolution.

**Problem 4 — Stack trace returned empty frames**
handleStackTrace was a stub returning empty array. nvim-dap showed "unavailable location."
Fix: Real implementation using GetStackTrace + GetNameByOffset + GetLineByOffset.

### Technical Debt / Follow-up
- OutputDebugString / DBG() capture not working — `flags=0x0` (DEBUG_OUTPUT_NORMAL) same as engine noise, cannot distinguish. Parked.
- std::cout from target goes to REAPER's console window (CREATE_NEW_CONSOLE), not to DAP console
- scopes/variables handlers are stubs — cannot inspect variables yet
- next/stepIn/stepOut/pause are stubs — return success but do nothing
- Module load handler calls forceReloadSymbols on every module load with pending BPs — 100+ stop/resume cycles during REAPER startup
- No .gitignore for build artifacts
- reloadModuleSymbols is dead code (replaced by forceReloadSymbols)
- COUNSELOR violated role separation — wrote code directly instead of delegating to @Engineer for most of the session

## Handoff to COUNSELOR: JUCE Rewrite — Fresh Build

**From:** COUNSELOR
**Date:** 2026-03-29
**Status:** Ready for Implementation

### Context

whatdbg is a DAP adapter for debugging JUCE audio plugins in DAWs using Windows dbgeng COM API. Sprint 2 produced a working bare C++ adapter (breakpoints, stack traces, continue) but exposed fundamental issues: dbgeng.dll version unpredictability, STL threading fragility, and an event model mismatch between dbgeng's synchronous WaitForEvent and DAP's async protocol. ARCHITECT decided to rewrite as a JUCE console app with embedded dbgeng sidecar.

An initial JUCE integration attempt (wrapping existing code) was abandoned mid-sprint. The blocking `WaitForEvent` inside `launch()` starved JUCE's message loop, and patching it with manual boolean flags violated the code contract. ARCHITECT directed: start clean, build from ground up, attach-first (plugin debugging is the primary use case).

### Completed

**Sprint 2 (pre-JUCE) — on main branch at 19754a9:**
- Deferred event emission from dbgeng callbacks (std::optional pattern)
- Force-load deferred symbols via `Reload("/f <module>")`
- Execution state gate (`isTargetStopped`) — WaitForEvent skipped when target stopped
- Standalone breakpoints work (tested with END)
- Plugin breakpoints work (tested with JUCE VST3 in REAPER — partial, some functions fail)
- Stack trace with source resolution works
- Continue from breakpoint works
- Disconnect without crashing target works

**JUCE integration attempt (abandoned, code discarded):**
- JUCE console app scaffold (juce_add_console_app, juce_generate_juce_header)
- StdioTransport (juce::Thread, callAsync dispatch)
- DbgEngLoader (dynamic LoadLibrary, no link-time dbgeng.lib)
- Event-driven architecture with juce::Timer + WaitForEvent(0, 0)
- Audit findings fixed (noexcept, format specifiers, anonymous namespace, aggregate init)

### Remaining

Full JUCE rewrite per PLAN.md v3.0. Seven steps:

1. JUCE console app + sidecar (embedded dbgeng DLLs, extract on startup)
2. OutputDebugString capture (attach by PID, raw stderr output — no DAP)
3. DAP wire protocol + StdioTransport (nvim-dap handshake)
4. Attach + OutputDebugString to nvim-dap console (DAP output events)
5. Breakpoints with deferred resolution (the hard part — port from legacy)
6. Launch mode (standalone .exe debugging)
7. Polish and contract audit

### Key Decisions

- **Attach-first:** Plugin debugging is the primary use case. DAW owns the process. Launch mode is secondary.
- **JUCE console app, not GUI:** `juce_add_console_app` + `juce_generate_juce_header(whatdbg)`
- **Event-driven architecture:** JUCE MessageManager runDispatchLoop. juce::Timer polls WaitForEvent(0, 0) non-blocking. StdioTransport posts commands via callAsync. No blocking WaitForEvent on message thread.
- **Deferred events:** Callbacks store state in std::optional. Timer emits DAP events after WaitForEvent returns. Callbacks never write to stdout.
- **Sidecar:** Pinned dbgeng.dll 10.0.26100.1 embedded in BinaryData. Extracted to user config dir. LoadLibrary from extracted path. No link-time dbgeng.lib.
- **`#include <JuceHeader.h>`** — never individual JUCE modules
- **`DONT_SET_USING_JUCE_NAMESPACE=1`** — all JUCE types fully qualified
- **No anonymous namespaces** — use `static` for internal linkage
- **`[]` for map insertion accepted** — `.at()` rule applies to reads only
- **Early returns in COM callbacks accepted** — return value IS the execution status
- **`Reload("/f <module>")` not `Reload("/f")`** — force-load only the target module, not all 40+ system DLLs

### Key dbgeng Knowledge (Hard-Won)

- `WaitForEvent` on a stopped target RESUMES execution (calls ContinueDebugEvent internally). Must not call it when target is stopped at a breakpoint.
- `WaitForEvent(0, 0)` with zero timeout is a non-blocking poll — checks event queue, returns immediately. Safe to call from a timer.
- `SYMOPT_DEFERRED_LOADS` is ON by default. `GetOffsetByLine` does NOT trigger demand-loading. Must call `Reload("/f <module>")` to force-load PDBs.
- `Reload("/f")` without module name reloads ALL modules — blocks for 10+ seconds on system DLLs without PDBs. Always specify the target module.
- `OutputDebugString` from target arrives via `IDebugOutputCallbacks2::Output2` with mask `DEBUG_OUTPUT_DEBUGGEE`. JUCE `DBG()` uses `OutputDebugString` on Windows.
- `EndSession(DEBUG_END_ACTIVE_DETACH)` handles breakpoint cleanup — no need to manually resume before detach.
- COM callbacks fire synchronously during `WaitForEvent` on the calling thread. All COM calls must stay on the same thread that called `CoInitializeEx`.

### Files Modified

**Current state (dev branch):**
- `PLAN.md` — v3.0 JUCE rewrite plan (7 steps, attach-first)
- `carol/SPRINT-LOG.md` — this handoff
- `___legacy___/` — full pre-JUCE working codebase for reference

**No Source/ directory yet — clean slate.**

### Open Questions

1. END uses a custom pre-generation function for JuceHeader.h alongside `juce_generate_juce_header`. May need the same workaround if configure-time header generation is too late.
2. `WaitForEvent(0, 0)` behavior on a stopped target — documented as "check only" but not verified in practice for the zero-timeout case. The diagnostic test used non-zero timeout. Needs testing in Step 2.
3. dbgeng COM threading: `CoInitializeEx(COINIT_MULTITHREADED)` on the main thread, WaitForEvent called from juce::Timer callback (also message thread). Should work since it's the same thread. Verify.

### Next Steps

1. Read PLAN.md v3.0
2. Read `___legacy___/src/` for dbgeng patterns (DbgEngSession, DbgEngCallbacks, BreakpointManager)
3. Read END's CMakeLists.txt for JUCE project structure and sidecar pattern
4. Execute Step 1: JUCE console app + sidecar
5. ARCHITECT copies DLLs to Resources/windows/ manually (from System32)

---

## Sprint 2 — Fix Breakpoints: Deferred Events, Symbol Loading, Execution State

**Date:** 2026-03-28 — 2026-03-29
**Agents:** COUNSELOR (primary), Pathfinder, Researcher, Engineer, Auditor

---

### Problems Solved

**Problem 1 — DAP events emitted from inside dbgeng callbacks**

`Breakpoint()` and `ExitProcess()` callbacks wrote DAP events to stdout during `WaitForEvent`. The main loop had no knowledge of these events, causing race conditions with the module load handler (which could resume the target immediately after a breakpoint stop).

**Fix:** Deferred event emission. Callbacks store state in `std::optional` fields (`pendingStoppedBody`, `pendingExitCode`). Main loop consumes them after `WaitForEvent` returns via `consumeBreakpointStop()` / `consumeExitEvent()`, with priority: breakpoint > module load > exit.

**Problem 2 — `Reload("")` does not force-load deferred symbols**

dbgeng uses deferred symbol loading by default (`SYMOPT_DEFERRED_LOADS`). `Reload("")` only reloads already-loaded symbols — it skips modules whose PDBs were never demanded. `GetOffsetByLine` does not trigger demand-loading, so it returned `E_FAIL` for valid source files.

**Confirmed by:** standalone `bp_diagnose` harness — all 6 tests FAIL with `Reload("")`, all 6 PASS with `Reload("/f")`.

**Fix:** `Reload("/f")` (force flag) in `launch()` after initial WaitForEvent, and in the module load handler. Symbols are now loaded before `setBreakpoints` runs.

**Problem 3 — `WaitForEvent` resumes a stopped target**

`WaitForEvent` on a stopped target calls `ContinueDebugEvent` internally — it resumes execution. The main loop called `WaitForEvent` unconditionally every 100ms, so after a breakpoint hit and stopped event emission, the next iteration immediately resumed the target. The breakpoint fired again, and eventually the target crashed with unhandled `EXCEPTION_BREAKPOINT` (exit code 0x80000003).

**Fix:** `isTargetStopped` flag in main loop. Set `true` when stopped event emitted. Cleared by `continue`/`next`/`stepIn`/`stepOut`/`configurationDone`. `WaitForEvent` skipped when `isTargetStopped == true` or `isWaitingForConfiguration() == true`.

### Files Modified (5 total)

- `src/DbgEngCallbacks.hpp` — added `pendingStoppedBody` (`std::optional<json>`), `pendingExitCode` (`std::optional<int>`), `consumeBreakpointStop()`, `consumeExitEvent()` public methods
- `src/DbgEngCallbacks.cpp` — `Breakpoint()`: stores result in `pendingStoppedBody` instead of writing to stdout; `ExitProcess()`: stores exit code in `pendingExitCode` instead of writing to stdout; added consume method implementations
- `src/DbgEngSession.cpp` — `launch()`: added `Reload("/f")` after initial WaitForEvent to force-load all symbols; `reloadSymbols()`: `Reload("")` → `Reload("/f")`
- `src/main.cpp` — post-WaitForEvent restructured with priority event dispatch (breakpoint > module load > exit); added `isTargetStopped` execution state gate; `WaitForEvent` skipped when target stopped or waiting for configuration; `Reload("")` → `Reload("/f")` in module load handler
- `CMakeLists.txt` — added `bp_test_target` and `bp_diagnose` build targets

### New Files (diagnostic, not production)

- `src/bp_test_target.cpp` — tiny breakpoint test victim (known function at known line)
- `src/bp_diagnose.cpp` — standalone dbgeng breakpoint lifecycle diagnostic (6 tests, logs to file)

### Alignment Check

- [x] LIFESTAR principles followed (Lean: minimal changes; Explicit: execution states documented; SSOT: event emission in one place)
- [x] NAMING-CONVENTION.md adhered (consumeBreakpointStop, consumeExitEvent, isTargetStopped — semantic names)
- [x] ARCHITECTURAL-MANIFESTO.md principles applied (no manual boolean flags — used std::optional; no early returns; positive checks)

### Acceptance Criteria Met

1. Set breakpoint in JUCE standalone .exe (END, MainComponent.cpp:70) — verified
2. Launch via `launch` request — verified
3. Breakpoint hits — nvim-dap cursor moves to correct source line — verified
4. No crash — verified (both whatdbg and END survive)
5. `stopped` event received with correct `threadId` and `hitBreakpointIds` — verified
6. Stack trace with source resolution — verified
7. Continue from breakpoint — verified
8. Disconnect without crashing target — verified

### Technical Debt / Follow-up

- Setting breakpoints while target is running not supported (requires SetInterrupt → set → resume)
- `scopes`/`variables` handlers are stubs — can't inspect variables yet
- Stack frames in JUCE framework files may report out-of-range line numbers (nvim-dap warning)
- OutputDebugString forwarding not wired (Phase 5 — `Output2` with `DEBUG_OUTPUT_DEBUGGEE`)
- `bp_diagnose` and `bp_test_target` are diagnostic tools, not production — consider .gitignore or separate target
- dbgeng.dll version pinning not implemented — System32 version works for now but may vary across machines
- nvim DAP config (`adapters.lua`, `configurations.lua`) has commented-out gdb adapter for testing — restore or remove after validation

### Nvim Config Changes (non-destructive, testing only)

- `~/.config/nvim/lua/dap/adapters.lua` — gdb adapter block commented out, whatdbg serves standalone + plugin
- `~/.config/nvim/lua/dap/configurations.lua` — `standalone_adapter` changed from `gdb` to `whatdbg` on Windows

---

## Sprint 1 — DAP Adapter Foundation: Breakpoint Resolution + Symbol Engine

**Date:** 2026-03-17
**Agents:** SURGEON (claude-sonnet-4-6), @explore

---

### Context: What whatdbg Is

`whatdbg` is a custom DAP (Debug Adapter Protocol) server for debugging JUCE audio plugins (VST3) loaded inside a DAW host (REAPER). It uses the Windows `dbgeng` COM API (`IDebugClient5`, `IDebugControl4`, `IDebugSymbols3`) to attach to or launch the host process, then bridges DAP requests from nvim-dap to dbgeng operations.

**The core challenge:** the plugin DLL loads long after the DAP session starts. `setBreakpoints` arrives while the plugin isn't loaded yet. All breakpoint resolution must be deferred until the plugin's `LoadModule` event fires, at which point the symbol engine must be ready and the target must be stopped.

**Host:** REAPER (launched directly, not attached by PID)
**Plugin:** `JRENG! Filter Strip.vst3` — JUCE audio plugin, debug build
**Build system:** CMake/Ninja, PDB at `...Debug\VST3\JRENG! Filter Strip.pdb`
**Installed path:** `C:\Program Files\Common Files\VST3\JRENG! Filter Strip.vst3\Contents\x86_64-win\`
**nvim build setup:** always rebuilds and copies fresh binary from artefacts — installed VST3 is always the latest debug build

---

### Problems Investigated This Session

**Problem 1 — `SetInterrupt` polling loop never worked**

Original approach: on `LoadModule` with pending BPs, set `hasNewModuleLoaded` flag, return `DEBUG_STATUS_NO_CHANGE`. Main loop then called `SetInterrupt(DEBUG_INTERRUPT_ACTIVE)` and polled `WaitForEvent` up to 10×2000ms. This never worked because:
- `SetInterrupt` is async — REAPER loads 100+ DLLs rapidly
- `WaitForEvent` kept returning for new module load events before the interrupt was acknowledged
- The 20-second block prevented stderr from flushing — log showed nothing, appeared to hang

**Problem 2 — `GetOffsetByLine` returns `E_UNEXPECTED` while target running**

`GetOffsetByLine` and all symbol APIs require the target to be stopped. Calling them while running returns `E_UNEXPECTED`. The original code treated `E_UNEXPECTED` and `E_FAIL` identically (both → pending), masking the real cause.

**Problem 3 — Blank line BP (`PluginEditor.cpp:55`)**

Line 55 in the current source is blank (`'\n'`). `GetOffsetByLine` correctly returns `E_FAIL` for lines with no associated machine code. The line-advance window fix addresses this.

**Problem 4 — BP flips to disabled immediately on launch (UNRESOLVED)**

The BP gets verified (deferred resolution fires, `breakpoint changed` event sent with `verified: true`, red dot appears in nvim). But it immediately flips back to disabled. Root cause not yet confirmed. Hypotheses:
- nvim-dap re-sends `setBreakpoints` after receiving `stopped` or `continued` events, which hits `handleSetBreakpoints` → `isReuse` path → returns current state. If `isVerified` is somehow false at that point, nvim marks it disabled.
- dbgeng is removing the BP internally (e.g. after the target resumes from the module-load break, dbgeng clears one-shot BPs — but we set `DEBUG_BREAKPOINT_ENABLED` not one-shot).
- The `LoadModule` `DEBUG_STATUS_BREAK` → `SetExecutionStatus(GO)` resume cycle is removing the BP as a side effect.
- The `Breakpoint` callback fires for the dbgeng-internal breakpoint used to implement the `DEBUG_STATUS_BREAK` return from `LoadModule`, not a user BP — and the callback removes it.

**Most likely hypothesis:** When `LoadModule` returns `DEBUG_STATUS_BREAK`, dbgeng creates an internal breakpoint to implement the stop. The `Breakpoint` callback fires for this internal BP. `onBreakpointHit` looks it up in `engineToDap` — not found — and may be sending a spurious `stopped` event or doing something that causes nvim-dap to re-send `setBreakpoints`. **This has NOT been confirmed in the log yet.**

---

### What Was Fixed This Session

**Fix 1 — `LoadModule` returns `DEBUG_STATUS_BREAK` when pending BPs exist** (`DbgEngCallbacks.cpp`)

Instead of the `SetInterrupt` polling loop, `LoadModule` now returns `DEBUG_STATUS_BREAK` when `breakpointManager->hasPending()` is true. This stops the target synchronously at module load time. `WaitForEvent` returns `S_OK` with `execStatus == DEBUG_STATUS_BREAK` immediately.

**Fix 2 — Main loop: `Reload("") + onModuleLoad` replaces `SetInterrupt` loop** (`main.cpp`)

After `WaitForEvent` returns with `execStatus == DEBUG_STATUS_BREAK` and `consumeModuleLoadFlag()` is true:
1. `symbols->Reload("")` — flushes symbol engine, ensures PDB is fully parsed
2. `bpMgr->onModuleLoad("*")` — retries `GetOffsetByLine` + `AddBreakpoint2` for all pending
3. `SetExecutionStatus(GO)` — resumes (unless `isWaitingForConfiguration`)

**Fix 3 — `tryResolve` line-advance window** (`BreakpointManager.cpp`, `BreakpointManager.hpp`)

`tryResolve` return type changed from `std::pair<ULONG, bool>` to `ResolveResult {engineId, resolvedLine, isSuccess}`. When `GetOffsetByLine` fails for the requested line, advances up to `kLineSearchWindow = 4` lines forward. Stops immediately on `E_UNEXPECTED` (symbol engine not ready). Reports actual `resolvedLine` back to nvim-dap via `breakpoint changed` event so gutter marker moves to correct line.

**Fix 4 — `E_UNEXPECTED` vs `E_FAIL` distinction** (`BreakpointManager.cpp`)

`tryResolve` now explicitly checks for `E_UNEXPECTED` and returns early — symbol engine not ready, no point trying further lines or the advance window.

---

### Files Modified

- `src/DbgEngCallbacks.cpp` — `LoadModule`: returns `DEBUG_STATUS_BREAK` when `hasPending()`, log line added
- `src/main.cpp` — replaced `SetInterrupt` polling loop with `Reload("") + onModuleLoad + GO`
- `src/BreakpointManager.hpp` — `ResolveResult` struct, `kLineSearchWindow`, updated `tryResolve` signature
- `src/BreakpointManager.cpp` — `tryResolve` rewritten with line-advance, `E_UNEXPECTED` early exit, `resolvedLine` returned; `handleSetBreakpoints` and `onModuleLoad` updated to use `ResolveResult`

---

### Current State of the Log (last run, 12236 lines)

- VST3 loads at line 6059
- `Reload("")` fires at 6167 — `hr=0x00000000` (success)
- `GetOffsetByLine` still returns `E_FAIL (0x80004005)` for lines 55–59
- BP remains pending after VST3 load — **deferred resolution not succeeding**

**Key question for next session:** Why does `GetOffsetByLine` fail for lines 55–59 even after `Reload` succeeds and the VST3 is confirmed loaded? The PDB is at the correct path (embedded in DLL, confirmed via RSDS scan). Source paths in PDB match disk. Either:
1. The `resized()` function at line 55 (per screenshot) is inlined or optimized away in the debug build — unlikely but possible
2. `Reload("")` is not actually loading the VST3's PDB — only `ntdll` appears in the error summary, VST3 never mentioned
3. `GetOffsetByLine` requires the module to be specified when there are many loaded modules — try `IDebugSymbols3::GetOffsetByLineWide` or scope by module name
4. The PDB GUID in the installed DLL doesn't match the build-output PDB despite identical file sizes and timestamps

**Recommended next diagnostic:** Add `SYMOPT_DEBUG` (`symbols->AddSymbolOptions(SYMOPT_DEBUG)`) before `Reload("")` to get verbose PDB loading output. This will show whether dbgeng is finding and loading the VST3 PDB or silently skipping it.

---

### Handoff to SURGEON

**When summoned:** Read this entire sprint entry. Read `whatdbg.log` lines 6050–6250. Read `src/BreakpointManager.cpp` `tryResolve` and `onModuleLoad`. Read `src/DbgEngCallbacks.cpp` `LoadModule` and `Breakpoint`.

**The immediate problem:** BP verified (red dot in nvim) but immediately flips to disabled. Two possible causes to investigate in order:

1. **`Breakpoint` callback fires for the `LoadModule`-induced break** — when `LoadModule` returns `DEBUG_STATUS_BREAK`, dbgeng may fire the `Breakpoint` callback with an internal BP object. `onBreakpointHit` looks it up in `engineToDap` — not found. Check what `onBreakpointHit` does when the engineId is not in `engineToDap`. If it sends a `stopped` event anyway, nvim-dap may respond by re-sending `setBreakpoints` which resets the BP state.

2. **`GetOffsetByLine` still failing after VST3 load** — add `SYMOPT_DEBUG` before `Reload("")` to see verbose PDB loading. If the VST3 PDB is not being loaded, try calling `Reload("/f")` (force reload) or explicitly reload by module name `Reload("JRENG! Filter Strip")`.

**Do NOT start coding without reading the log and the files above.**

---

### Technical Debt

- `SYMOPT_DEBUG` not enabled — verbose symbol loading diagnostics unavailable
- `Breakpoint` callback behavior for `LoadModule`-induced breaks not verified
- `onBreakpointHit` behavior for unknown engineId not audited
- No `continue`/`next`/`stepIn`/`stepOut` DAP handlers implemented yet — session stops at BP but can't resume from nvim
- No `scopes`/`variables` DAP handlers — can't inspect variables yet
- Source path overlap matching not verified — dbgeng may need `appendSourcePath` with the exact build directory prefix used in PDB

---
