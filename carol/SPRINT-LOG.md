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

## Sprint 16: macOS Port — Phase 0 (liblldb sidecar) + Phase 1 (header detox)

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
