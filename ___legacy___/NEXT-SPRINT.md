# WHATDBG — Next Sprint Brief

**For:** COUNSELOR
**Project:** WHATDBG — Windows Host Attachable Tool — DeFuGging
**Goal:** Fix breakpoints (crash or silently disabled)

---

## Context

WHATDBG is a DAP adapter for Neovim nvim-dap on Windows.
It wraps `dbgeng.dll` COM interfaces and speaks the DAP wire protocol over stdin/stdout.
It is a drop-in replacement for `codelldb` on Windows.

**What already works:**
- DAP wire protocol (Content-Length framing, JSON, request/response/event)
- dbgeng COM initialization (IDebugClient5, IDebugControl4, IDebugSymbols3, etc.)
- `launch` and `attach` modes
- `std::cout` output captured and piped to nvim-dap console (launch mode, console apps)
- Deferred breakpoint resolution logic (pending list, onModuleLoad retry)
- Symbol path, source path, SYMOPT_LOAD_LINES, DEBUG_ENGOPT_INITIAL_BREAK all set correctly

**What is NOT working yet (separate from breakpoints):**
- JUCE `DBG()` / `OutputDebugStringA` output NOT forwarded to DAP console
  - Reason: `OutputDebugStringA` goes to kernel debug channel, NOT stdout
  - It arrives via `IDebugOutputCallbacks2::Output2` with `DEBUG_OUTPUT_DEBUGGEE` mask
  - Currently `Output2` only logs to stderr — DAP forwarding not wired yet
  - This is Phase 5 work, do NOT fix in this sprint

**What is broken:**
- Breakpoints either crash the adapter or are silently disabled (never hit)
- Root cause unknown — needs research-driven diagnosis

---

## Research References

Both references are vendored locally in `references/` (gitignored — local only).

### 1. MDbg (smallest, read first)
```
references/mdbg/
```
- Minimal correct `dbgeng.dll` COM sequence
- Focus: how they handle the event loop and breakpoint creation
- Small codebase, easiest to read

### 2. LLVM LLDB Windows backend (most important)
```
references/lldb-src/lldb/source/Plugins/Process/Windows/Common/DebuggerThread.cpp
references/lldb-src/lldb/source/Plugins/Process/Windows/Common/DebuggerThread.h
references/lldb-src/lldb/source/Plugins/Process/Windows/Common/NativeProcessWindows.cpp
references/lldb-src/lldb/source/Plugins/Process/Windows/Common/NativeProcessWindows.h
```

**Critical pattern to find in DebuggerThread.cpp:**
- Where does LLDB emit the stopped event relative to `WaitForEvent` returning?
- How does LLDB handle `IDebugEventCallbacks::Breakpoint` — does it emit the event
  INSIDE the callback, or does it set a flag and emit AFTER `WaitForEvent` returns?
- This is the re-entrancy question. `dbgeng` COM is NOT re-entrant from within
  event callbacks. Any COM call inside a callback that triggers another COM call = crash.

---

## The Suspected Bug

In `DbgEngCallbacks.cpp` line 188-221, `EventCallbacks::Breakpoint` currently:

```cpp
HRESULT STDMETHODCALLTYPE EventCallbacks::Breakpoint (PDEBUG_BREAKPOINT bp)
{
    // ... calls systemObjects->GetCurrentThreadId()  ← COM call inside callback
    // ... calls breakpointManager->onBreakpointHit() 
    // ... calls protocol.writeMessage(std::cout, ...)  ← writes DAP event inside callback
    return DEBUG_STATUS_BREAK;
}
```

**Hypothesis:** Calling `systemObjects->GetCurrentThreadId()` (a COM call) inside
`IDebugEventCallbacks::Breakpoint` may be re-entrancy violation → crash or silent disable.

**The correct pattern (from LLDB):**
```
WaitForEvent fires Breakpoint callback
    → set internal flag / store bp pointer
    → return DEBUG_STATUS_BREAK  (nothing else)
    → WaitForEvent RETURNS to main loop
    → main loop checks flag
    → THEN calls GetCurrentThreadId
    → THEN emits DAP stopped event
```

---

## Files to Modify

```
src/DbgEngCallbacks.hpp   — add pending breakpoint hit flag/storage
src/DbgEngCallbacks.cpp   — Breakpoint() callback: set flag only, no COM calls
src/main.cpp              — main event loop: check flag after WaitForEvent returns,
                            emit stopped event there
```

Do NOT modify:
- `src/BreakpointManager.cpp` — deferred resolution logic is correct
- `src/DbgEngSession.cpp` — COM init sequence is correct
- `src/DapProtocol.cpp` — wire protocol is working
- `src/DapTypes.hpp` — types are correct

---

## Acceptance Criteria

1. Set a breakpoint in a JUCE standalone `.exe`
2. Launch via `launch` request
3. Breakpoint hits — nvim-dap cursor moves to correct source line
4. No crash
5. `stopped` event received by nvim-dap with correct `threadId` and `hitBreakpointIds`

---

## What COUNSELOR Should Produce

1. Read the two reference codebases (MDbg + LLDB)
2. Confirm or refute the re-entrancy hypothesis
3. Produce a precise fix spec:
   - Exact changes to `EventCallbacks::Breakpoint()`
   - Exact changes to the main event loop in `main.cpp`
   - Any new fields needed in `EventCallbacks`
4. Hand off to SURGEON for implementation

Do NOT implement. Research and spec only.

---

## Threading Model (for context)

```
Main thread:  dbgeng event loop
              dequeues commands → executes COM calls → WaitForEvent(100ms)
              
IO thread:    reads stdin → enqueues DAP requests to command queue

Rule:         ALL COM calls happen on main thread ONLY
              WaitForEvent is NOT re-entrant
              IDebugEventCallbacks methods are called ON the main thread
              DURING WaitForEvent — treat as re-entrant context
```

---

*conceived with CAROL*
