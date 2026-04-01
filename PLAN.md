# PLAN — WHATDBG JUCE Rewrite

**Version:** 4.0
**Date:** 2026-04-01
**Author:** COUNSELOR
**Status:** Awaiting ARCHITECT approval

---

## Objective

Build whatdbg as a JUCE console application.
Target: debug JUCE audio plugins loaded in a DAW (REAPER).
MVP features: breakpoints in plugin code, stepping, pause, OutputDebugString capture to nvim-dap console, variable inspection.

---

## Contracts (Non-Negotiable)

- `carol/NAMES.md`
- `carol/MANIFESTO.md` (BLESSED)
- `DONT_SET_USING_JUCE_NAMESPACE=1`
- `#include <JuceHeader.h>` — never individual modules

---

## Completed Work (Sprints 1-6)

Steps 1-6 from PLAN v3.0 are functionally complete. Built non-linearly across Sprints 3-6.

| Original Step | What Was Built | Sprint |
|---------------|----------------|--------|
| Step 1 — Sidecar | BinaryData extraction, LoadLibrary, DebugCreate thunk | 3 |
| Step 2 — OutputDebugString | Callbacks wired, but **parked** — flags=0x0 indistinguishable from engine noise | 3 |
| Step 3 — DAP Wire Protocol | stdin Reader (AbstractFifo), DAP framing, command dispatch | 3 |
| Step 4 — Attach | attach handler, Timer poll, disconnect | 3 |
| Step 5 — Breakpoints | Deferred resolution on module load, per-module symbol reload, hit detection, stack traces | 3-5 |
| Step 5+ — Stepping | next (F10), stepIn (F11), stepOut — source-level via SetCodeLevel | 6 |
| Step 6 — Launch Mode | CreateProcess2, initial INT3 handling, configurationDone resume | 3 |
| Sprint 4 — Polish | Contract audit, dead code removal, sign fix, naming compliance | 4 |

### Known Issues Carried Forward

- `logWrite()` vs `juce::Logger::writeToLog` inconsistency (BreakpointManager.cpp)
- `kMaxStackFrames` defined locally inside `getStackTrace`, not at class/file scope
- Diagnostic logging throughout Callbacks.cpp and Whatdbg.cpp
- Dead `EXCEPTION_SINGLE_STEP` branch in Callbacks.cpp (keep for instruction-level stepping)
- stepOut reports `reason: "breakpoint"` instead of `"step"` (internal BP engineId=10000)

---

## Remaining Steps

---

### Step 8 — Pause (Architectural Fix)

**Goal:** Functional pause command. Currently non-functional — `SetInterrupt` requires a blocked `WaitForEvent` on a separate thread.

**Problem:** Single-thread polling architecture calls `WaitForEvent(50ms)` then `SetInterrupt` from the same thread. `SetInterrupt` only works when another thread is blocked inside `WaitForEvent(INFINITE)`.

**Options (ARCHITECT to decide):**

- **Option A — Dedicated engine thread:** Move `WaitForEvent(INFINITE)` to a dedicated thread. Main thread sends `SetInterrupt` to break it out. Callbacks fire on engine thread, post deferred flags to main thread for DAP emission. Matches Ghidra/DbgShell architecture.

- **Option B — DebugBreakProcess:** Store process handle from CreateProcess/AttachProcess. Call `DebugBreakProcess(hProcess)` from main thread. Injects `EXCEPTION_BREAKPOINT` into target. Simpler but less clean — requires distinguishing injected break from real breakpoints.

**Actions (after ARCHITECT decides):**
1. Implement chosen approach
2. Wire `handlePause` to working interrupt mechanism
3. Detect pause completion, emit DAP `stopped` event with `reason: "pause"`
4. Verify: pause during running target, continue after pause, breakpoints still work

**Validation:**
- Plugin running in REAPER, hit pause in nvim-dap
- Execution stops, cursor shows current location
- Continue resumes, breakpoints still hit
- Step after pause works

---

### Step 9 — OutputDebugString Capture

**Goal:** `DBG()` / `OutputDebugString` from plugin code appears in nvim-dap console panel as DAP `output` events.

**Problem:** Output2 callback receives all engine output with `flags=0x0` (`DEBUG_OUTPUT_NORMAL`). Plugin `OutputDebugString` arrives with same flags as dbgeng's own diagnostic messages. Cannot distinguish.

**Research needed:** How to filter plugin OutputDebugString from engine noise. Possible approaches:
- Filter by content (prefix convention in plugin code)
- Filter by timing (only emit during running state, not during symbol loading)
- Use `DEBUG_OUTCBI_ANY_FORMAT` flag in Output2
- Separate OutputDebugString from `IDebugOutputCallbacks` — use a different mechanism entirely

**Actions:**
1. Research filtering approaches (delegate to @Librarian)
2. Implement chosen filter
3. Route filtered output to DAP `output` event with `category: "console"`
4. Verify with JUCE `DBG()` macro in plugin

**Validation:**
- Load plugin in REAPER with `DBG("hello from plugin")`
- "hello from plugin" appears in nvim-dap console
- Engine noise (symbol loading messages, etc.) does NOT appear
- Works during attach and launch modes

---

### Step 10 — Variable Inspection (scopes + variables)

**Goal:** Inspect local variables when stopped at a breakpoint or after a step.

**Actions:**
1. Research dbgeng variable inspection API (delegate to @Librarian)
   - `IDebugSymbolGroup`, `GetScopeSymbolGroup`, `GetSymbolValueText`
   - Or: `IDebugAdvanced::GetSymbolInformation`
2. Implement `handleScopes` — return "Locals" scope with variablesReference
3. Implement `handleVariables` — enumerate locals at current scope
4. Handle nested types (structs, pointers) with recursive variablesReference
5. Handle JUCE types (juce::String, juce::Array) — readable display

**Validation:**
- Stop at breakpoint in plugin code
- nvim-dap variables panel shows local variables with values
- Expanding structs shows members
- Values update after stepping

---

### Step 11 — Polish

**Goal:** Production quality. Clean build. All diagnostics removed.

**Actions:**
1. Remove diagnostic logging (WaitForEvent S_OK, exception codes, pause timeout)
2. Fix `logWrite()` vs `juce::Logger::writeToLog` inconsistency
3. Fix stepOut `reason: "breakpoint"` → `reason: "step"` (detect internal BP engineId)
4. Move `kMaxStackFrames` to appropriate scope
5. Audit NAMES.md compliance
6. Audit MANIFESTO.md compliance (BLESSED)
7. Clean build, zero warnings

**Validation:**
- Clean build from scratch
- Plugin attach + breakpoints + stepping + pause + OutputDebugString — all work
- Standalone launch + breakpoints — works
- No diagnostic output in normal operation
- ARCHITECT review

---

## Dependency Map

```
Step 8 (pause — architectural fix)
  └→ Step 9 (OutputDebugString capture)
      └→ Step 10 (variable inspection)
          └→ Step 11 (polish)
```

Steps 9 and 10 are independent of each other but both depend on Step 8 (pause confirms the execution control model is solid before adding more features). Step 11 is last.

---

## Architecture (Current — Two-Thread Model)

```
Main Thread (owns everything):
  ├── COM lifecycle (Session, Callbacks)
  ├── DAP command dispatch (Whatdbg)
  ├── Deferred event processing
  ├── stdout (DAP responses/events)
  └── WaitForEvent polling (50ms timeout)

Stdin Thread (dumb buffer):
  └── dap::Reader → AbstractFifo → main thread consumes
```

**Note:** Step 8 may change this to a three-thread model if Option A is chosen.

---

## Key Design Decisions (Unchanged)

**Deferred events:**
- Callbacks store state (flags on `debug::State`), never write DAP events directly
- Main loop consumes deferred state after WaitForEvent returns, emits events

**Sidecar:**
- Pinned dbgeng.dll set embedded in BinaryData
- Extracted to user config directory on startup
- LoadLibrary from extracted path — deterministic version

**Attach-first:**
- Plugin debugging is the primary use case
- Launch mode is secondary, built on top of attach

---

**End of PLAN v4.0**

**JRENG!**
