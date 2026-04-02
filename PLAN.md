# PLAN — whatdbg
## Windows Host Abstraction Translator for dbgeng

**Repository:** https://github.com/jrengmusic/whatdbg
**Version:** 5.0
**Date:** 2026-04-02
**Author:** COUNSELOR
**Status:** Complete — all planned features implemented

---

## Objective

DAP debug adapter for Windows C/C++ development with neovim.
Built as a JUCE console application using Microsoft's dbgeng COM API.

---

## Contracts (Non-Negotiable)

- `carol/NAMES.md`
- `carol/MANIFESTO.md` (BLESSED)
- `carol/JRENG-CODING-STANDARD.md`
- `DONT_SET_USING_JUCE_NAMESPACE=1`
- `#include <JuceHeader.h>` — never individual modules

---

## Completed Work (Sprints 3-12)

| Feature | Sprint | Key Implementation |
|---------|--------|--------------------|
| Sidecar dbgeng | 3 | BinaryData extraction, LoadLibrary, DebugCreate thunk |
| DAP Wire Protocol | 3 | stdin Reader (AbstractFifo), DAP framing, dispatch table |
| Launch Mode | 3 | CreateProcess2, initial INT3 handling, configurationDone resume |
| Attach Mode | 3 | AttachProcess, PID storage |
| Breakpoints | 3-5 | Deferred resolution on module load, per-module symbol reload, hit detection, stack traces |
| Contract Audit | 4 | Dead code removal, sign fix, naming compliance |
| Per-Module Symbol Reload | 5 | `.reload /f <basename.quoted()>` via Execute — eliminates module load storm |
| Stepping | 6 | next (F10), stepIn (F11), stepOut — source-level via SetCodeLevel |
| Pause | 7 | DebugBreakProcess via OpenProcess + stored PID from CreateProcess callback |
| BP Resolution After Load | 7 | `.reload /f` (global) retry in handleSetBreakpoints when pending |
| RAII DynObj | 7 | `ReferenceCountedObjectPtr<DynamicObject>` replaces naked `new` |
| OutputDebugString Capture | 8 | Output2 callback, `arg & DEBUG_OUTPUT_DEBUGGEE`, deferred to DAP output event |
| Variable Inspection | 9 | IDebugSymbolGroup2, scopes/variables/expansion, variablesReference registry |
| stepOut Reason Fix | 10 | isUserBreakpoint distinguishes internal BP from user BP |
| Type Pretty-Printing | 10 | juce::String, std::string, std::unique_ptr, std::vector via child expansion + ReadMultiByteStringVirtual |
| Debug-Only Logging | 10 | `#if JUCE_DEBUG` guard on logWrite and file I/O |
| Value Formatting | 10 | 0n strip, backtick strip, pointer type truncation, composite empty |
| Expression Evaluation | 11 | Secondary client + `Execute("?? expr")` with CaptureOutputCallback, juce::String auto-resolve |
| Multi-Thread Support | 11 | IDebugSystemObjects, real thread enumeration, GetThreadDescription, frame ID mapping |
| Symbol Group Caching | 11 | getOrCreateSymbolGroup per frame, invalidated on stop |
| Terminate vs Disconnect | 11 | DEBUG_END_ACTIVE_TERMINATE vs DEBUG_END_ACTIVE_DETACH based on DAP command |
| Comprehensive Audit | 12 | Auditor sweep: 45 findings, all addressed |
| File Splitting | 12 | Session.cpp 3-way, BreakpointManager.cpp 2-way, Whatdbg.cpp 2-way |
| Dispatch Table | 12 | 16-branch else-if replaced with std::unordered_map |
| Early Return Elimination | 12 | tryResolve 4 early returns fixed, Reader.cpp 3 early returns fixed |
| Doxygen Documentation | 12 | All 10 header files — every public class, method, field documented |
| Exception Lookup Table | 12 | 6-branch if-else replaced with static handler map |
| Dead Code Removal | 12 | pendingStoppedBody, breakpointThreadId, getInt() removed |
| Logging Unification | 12 | All juce::Logger::writeToLog → logWrite |
| leakDetector/vfptr Filter | 12 | Filtered from variable expansion |

---

## Architecture (Current — Two-Thread Model)

```
Main Thread (owns everything):
  +-- COM lifecycle (Session)
  +-- DAP command dispatch (Whatdbg, dispatch table)
  +-- Deferred event processing
  +-- stdout (DAP responses/events)
  +-- WaitForEvent polling (50ms timeout)
  +-- Variable inspection (symbol group, pretty-print)
  +-- Expression evaluation (secondary client)

Stdin Thread (dumb buffer):
  +-- dap::Reader -> AbstractFifo -> main thread consumes
```

---

## File Structure

```
Source/
  Main.cpp                          Entry point, sidecar extraction
  Log.h                             Debug-only file logging (JUCE_DEBUG)
  Whatdbg.h                         Orchestrator header
  Whatdbg.cpp                       Main loop, dispatch table, deferred events
  WhatdbgHandlers.cpp               16 DAP command handlers
  dap/
    Reader.h / Reader.cpp           stdin thread, AbstractFifo SPSC queue
    Types.h                         DAP message builders, DynObj alias
  debug/
    State.h                         SSOT flag store (Context<State>)
    Session.h                       COM wrapper header (all public API)
    Session.cpp                     Lifecycle, stepping, breakpoints, threads
    SessionInspection.cpp           Stack trace, locals, variables, evaluate
    SessionPrettyPrint.cpp          Type formatters (4 types + helpers)
    PrettyPrint.h                   Shared declarations (debug::detail)
    Callbacks.h / Callbacks.cpp     COM OutputCallbacks + EventCallbacks
    Loader.h / Loader.cpp           Sidecar DLL loader
    BreakpointManager.h             BP lifecycle header
    BreakpointManager.cpp           Core BP logic (tryResolve, onHit)
    BreakpointManagerHandlers.cpp   handleSetBreakpoints, onModuleLoad
```

---

## Key Design Decisions

**Deferred events:**
Callbacks store flags on `debug::State`, never write DAP events directly. Main loop consumes deferred state after WaitForEvent returns, emits events.

**Sidecar:**
Pinned dbgeng.dll set embedded in BinaryData. Extracted to user config directory on startup. LoadLibrary from extracted path.

**Pause (Option B — DebugBreakProcess):**
Option A (dedicated engine thread) was attempted and abandoned — everything broke, couldn't even launch DAW. Option B uses OpenProcess + DebugBreakProcess from main thread. PID captured from CreateProcess callback (launch) or attach parameter.

**Variable inspection:**
IDebugSymbolGroup2 via GetScopeSymbolGroup2. Cached per frame, invalidated on stop. Pretty-print via child expansion + ReadMultiByteStringVirtual for known types. NatVis via `dx` command attempted but reverted — contaminates session-global scope. DbgModel.h C++ API attempted but header won't compile (C++20/WinRT constructs).

**Multi-thread:**
OS TID as DAP threadId. Unique frame IDs via counter + map to (threadSystemId, frameIndex). Thread context set in handleScopes/handleVariables before symbol access.

**Expression evaluation:**
Secondary client via CreateClient. `Execute("?? expr")` with CaptureOutputCallback. `.symopt- 100` enables unqualified local resolution. juce::String auto-resolve via dot/arrow Evaluate + ReadMultiByteStringVirtual.

---

## Remaining Technical Debt

| Item | Severity | Notes |
|------|----------|-------|
| `fopen`/`fclose` raw C I/O | Low | Should be juce::FileLogger. Works, not idiomatic. |
| Dead EXCEPTION_SINGLE_STEP branch | Keep | Needed for future instruction-level stepping |
| debuggeeOutputText += accumulation | Low | String allocation pressure at high ODS frequency. Debug builds only. |
| NatVis in variables panel | Deferred | dx contaminates scope, DbgModel.h won't compile. Tier 2 future. |
| dap-repl routing | External | nvim-dap-ui limitation #306. ODS output goes to REPL not Console. |
| BreakpointManagerHandlers.cpp 326 lines | Borderline | handleSetBreakpoints ~230 lines. Could be split further. |
| WhatdbgHandlers.cpp 355 lines | Borderline | 16 small handlers. Acceptable as-is. |
| No tests | Significant | No regression safety net |
| No error recovery | Medium | If dbgeng fails mid-session, no graceful handling |

---

**End of PLAN v5.0**

**JRENG!**
