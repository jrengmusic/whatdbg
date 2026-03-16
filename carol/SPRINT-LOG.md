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
