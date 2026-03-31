# SPRINT-LOG.md

**Project:** whatdbg  
**Repository:** /c/Users/jreng/Documents/Poems/dev/whatdbg  
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
