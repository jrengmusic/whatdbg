# PLAN — WHATDBG JUCE Rewrite

**Version:** 3.0
**Date:** 2026-03-29
**Author:** COUNSELOR
**Status:** Awaiting ARCHITECT approval

---

## Objective

Build whatdbg from scratch as a JUCE console application.
Target: debug JUCE audio plugins loaded in a DAW (REAPER).
Two MVP features: OutputDebugString capture to nvim-dap console, and breakpoints in plugin code.

---

## Contracts (Non-Negotiable)

- `carol/NAMING-CONVENTION.md`
- `carol/JRENG-CODING-STANDARD.md`
- `carol/ARCHITECTURAL-MANIFESTO.md` (LIFESTAR + LOVE)
- `DONT_SET_USING_JUCE_NAMESPACE=1`
- `#include <JuceHeader.h>` — never individual modules

---

## Reference

- `___legacy___/src/` — working pre-JUCE adapter (dbgeng patterns, callback design, breakpoint manager)
- `~/Documents/Poems/dev/end/` — JUCE project structure, ConPty sidecar pattern, threading model

---

## Target Structure

```
whatdbg/
├── CMakeLists.txt
├── Source/
│   ├── Main.cpp
│   ├── dbgeng/
│   │   ├── DbgEngLoader.cpp / .h       LoadLibrary sidecar, DebugCreate thunk
│   │   ├── DbgEngSession.cpp / .h      COM session lifecycle
│   │   ├── DbgEngCallbacks.cpp / .h    Event + Output callbacks
│   │   └── BreakpointManager.cpp / .h  DAP-to-dbgeng BP mapping
│   ├── dap/
│   │   ├── DapServer.cpp / .h          DAP protocol + dispatch + event loop
│   │   └── DapTypes.h                  JSON builders (juce::var, juce::DynamicObject)
│   └── transport/
│       └── StdioTransport.cpp / .h     juce::Thread, stdin reader, stdout writer
├── Resources/
│   └── windows/
│       ├── dbgeng.dll                  Pinned 10.0.26100.1
│       ├── dbghelp.dll
│       ├── dbgcore.dll
│       └── symsrv.dll
└── carol/
```

---

## Steps

---

### Step 1 — JUCE Console App + Sidecar

**Goal:** JUCE console app that extracts embedded dbgeng DLLs and loads them dynamically. No DAP, no debugging — just sidecar lifecycle.

**Actions:**
1. CMakeLists.txt — `juce_add_console_app`, `juce_generate_juce_header`, BinaryData for DLLs
2. Source/Main.cpp — extract sidecar, load via DbgEngLoader, print version, exit
3. Source/dbgeng/DbgEngLoader — LoadLibrary from extracted path, resolve DebugCreate
4. Resources/windows/ — copy pinned DLLs from System32

**Validation:**
- Builds clean
- DLLs extracted to `~/.config/whatdbg/dbgeng/` (or AppData equivalent)
- `DebugCreate` resolved, prints success, exits

---

### Step 2 — OutputDebugString Capture

**Goal:** Attach to a running DAW by PID, capture `OutputDebugString` / `DBG()` output, print to stderr. No DAP protocol yet — raw console output.

**Actions:**
1. Source/dbgeng/DbgEngSession — CoInitializeEx, DebugCreate (via loader), QI interfaces, AttachProcess
2. Source/dbgeng/DbgEngCallbacks — OutputCallbacks with `Output2`, route `DEBUG_OUTPUT_DEBUGGEE` to stderr
3. Source/Main.cpp — parse PID from argv, attach, WaitForEvent loop, print captured output

**Validation:**
- Build. Run: `whatdbg.exe <reaper_pid>`
- Load a JUCE plugin in REAPER that calls `DBG("hello")`
- "hello" appears in whatdbg's stderr
- Ctrl+C detaches cleanly

---

### Step 3 — DAP Wire Protocol + StdioTransport

**Goal:** DAP JSON framing over stdin/stdout. nvim-dap handshake works.

**Actions:**
1. Source/transport/StdioTransport — juce::Thread, binary mode, stdin reader via callAsync, stdout writer with CriticalSection
2. Source/dap/DapTypes.h — makeResponse, makeErrorResponse, makeEvent, makeCapabilities
3. Source/dap/DapServer — dispatch by command name, `initialize` handler returns capabilities
4. Source/Main.cpp — event-driven: MessageManager runDispatchLoop, StdioTransport posts commands, Timer polls dbgeng

**Validation:**
- nvim-dap connects, sends `initialize`, receives capabilities
- Session starts (then fails on `attach` — expected)

---

### Step 4 — Attach + OutputDebugString to nvim-dap Console

**Goal:** `attach` handler connects to DAW. OutputDebugString arrives in nvim-dap console panel as DAP `output` events.

**Actions:**
1. DapServer: `attach` handler — takes `pid`, calls session.attach()
2. DbgEngCallbacks: OutputCallbacks routes `DEBUG_OUTPUT_DEBUGGEE` → DAP `output` event with `category: "console"` via transport.writeMessage
3. Timer: polls WaitForEvent(0, 0), non-blocking
4. DapServer: `disconnect` handler — EndSession, stopDispatchLoop

**Validation:**
- nvim-dap: Attach to DAW (VST3) config
- Load plugin, trigger `DBG("test from plugin")`
- "test from plugin" appears in nvim-dap console panel
- Disconnect clean

---

### Step 5 — Breakpoints (Deferred Resolution)

**Goal:** Set breakpoints in plugin source. Plugin DLL loads after attach — breakpoints resolve on LoadModule.

**Actions:**
1. Source/dbgeng/BreakpointManager — port from legacy (handleSetBreakpoints, tryResolve, onModuleLoad, onBreakpointHit)
2. DbgEngCallbacks: EventCallbacks — Breakpoint, Exception, LoadModule, CreateProcess, ExitProcess callbacks
3. DapServer: `setBreakpoints` handler, `configurationDone` handler
4. Deferred event pattern: callbacks store state, timer emits DAP events after WaitForEvent returns
5. Module load: Reload("/f <module>"), resolve pending BPs, emit breakpoint changed events
6. Breakpoint hit: emit `stopped` event, set target-stopped state, timer skips WaitForEvent

**Validation:**
- Set breakpoint in plugin source (e.g., `processBlock` or `resized`)
- Attach to DAW, load plugin
- Breakpoint hits, cursor moves to line in nvim
- Stack trace shows plugin call stack
- Continue works
- Disconnect clean

---

### Step 6 — Launch Mode (Standalone)

**Goal:** Launch a standalone .exe under the debugger. Extends Step 5 for non-DAW use.

**Actions:**
1. DapServer: `launch` handler — CreateProcess2, non-blocking (no internal WaitForEvent)
2. Timer: process initial events (CreateProcess, LoadModule, initial INT3) via WaitForEvent(0, 0)
3. Symbol loading: detect initial breakpoint, force-load target module symbols
4. configurationDone: resume target

**Validation:**
- Launch END standalone from nvim-dap
- Breakpoint in MainComponent constructor hits
- Stack trace, continue, disconnect — all work

---

### Step 7 — Polish

**Goal:** Production quality. Audit contracts. Clean build.

**Actions:**
1. Remove diagnostic logging (keep essential stderr output)
2. Audit JRENG-CODING-STANDARD compliance
3. Audit NAMING-CONVENTION compliance
4. Audit ARCHITECTURAL-MANIFESTO compliance (LIFESTAR + LOVE)
5. .gitignore for build artifacts, extracted sidecar, logs

**Validation:**
- Clean build from scratch, zero warnings
- Plugin attach + OutputDebugString + breakpoints — all work
- Standalone launch + breakpoints — works
- ARCHITECT review

---

## Dependency Map

```
Step 1 (sidecar)
  └→ Step 2 (OutputDebugString capture — raw, no DAP)
      └→ Step 3 (DAP wire protocol + transport)
          └→ Step 4 (attach + output to nvim console)
              └→ Step 5 (breakpoints — the hard part)
                  └→ Step 6 (launch mode)
                      └→ Step 7 (polish)
```

---

## Key Design Decisions

**Event-driven, not polling:**
- JUCE MessageManager runs the dispatch loop
- StdioTransport (juce::Thread) posts commands via callAsync
- juce::Timer polls WaitForEvent(0, 0) — non-blocking, no message thread starvation

**Deferred events:**
- Callbacks store state (std::optional), never write DAP events directly
- Timer consumes deferred state after WaitForEvent returns, emits events on message thread

**Sidecar:**
- Pinned dbgeng.dll set embedded in BinaryData
- Extracted to user config directory on startup
- LoadLibrary from extracted path — deterministic version

**Attach-first:**
- Plugin debugging is the primary use case
- Launch mode (Step 6) is secondary, built on top of attach

---

**End of PLAN v3.0**

**JRENG!**
