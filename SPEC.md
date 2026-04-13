# whatdbg Specification v1.0
## Windows Host Abstraction Translator for dbgeng

**Repository:** https://github.com/jrengmusic/whatdbg

## Overview

**Purpose:** whatdbg is a DAP debug adapter for Windows C/C++ development with neovim. No existing DAP adapter for Windows works reliably with nvim + mason. whatdbg fills that gap using Microsoft's dbgeng (the engine behind WinDbg) as its debug backend.
**Target End-User:** C/C++ developer using neovim + nvim-dap on Windows.
**Core Workflow:** Launch or attach to any Windows executable (standalone apps, DAW-hosted plugins, services), set breakpoints, step through code, inspect variables, evaluate expressions — all from nvim-dap.

---

## Technology Stack

- **Language:** C++17
- **Framework:** JUCE (console application, no GUI)
- **Debug Engine:** Microsoft dbgeng COM API (sidecar — pinned version via BinaryData)
- **Protocol:** Debug Adapter Protocol (DAP) over stdin/stdout
- **DAP Client:** nvim-dap + nvim-dap-ui
- **Build:** CMake + Ninja, MSVC toolchain
- **Platform:** Windows 10+ (x64, ARM64)

---

## Core Principles

1. **Two-Thread Model:** Main thread owns COM, DAP dispatch, and stdout. Stdin thread is a dumb FIFO buffer. No cross-thread writes.
2. **Deferred Events:** COM callbacks store flags on State. Main loop consumes flags and emits DAP events. Callbacks never write to stdout.
3. **Sidecar Isolation:** dbgeng DLLs are extracted from BinaryData at startup. LoadLibrary from extracted path ensures deterministic version.
4. **BLESSED Compliance:** All code follows Bound, Lean, Explicit, SSOT, Stateless, Encapsulation, Deterministic principles.
5. **Debug-Only Logging:** File logging via `logWrite()` compiled out in Release builds (`#if JUCE_DEBUG`).

---

## Features

### Feature 1: Launch Mode

**User Flow (Happy Path):**
1. User starts debug session in nvim-dap with launch configuration
2. whatdbg receives DAP `launch` request with program path
3. whatdbg calls `CreateProcess2` with `DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE`
4. DAW launches in a new console window
5. whatdbg captures PID from `CreateProcess` callback via `GetProcessId(handle)`
6. Initial `EXCEPTION_BREAKPOINT` (INT3) fires — whatdbg holds target stopped
7. User's DAP client sends `configurationDone`
8. whatdbg resumes target, emits DAP `thread` event with `reason: "started"`
9. DAW loads normally, user works with it

**Edge Cases:**
- Program path with spaces: quoted automatically (`"C:\Program Files\..."`)
- Program not found: `CreateProcess2` returns failure, DAP error response sent
- DAW crashes during launch: `ExitProcess` callback fires, DAP `exited` + `terminated` events emitted

**Error Handling:**

| Condition | User Sees | System Action |
|-----------|-----------|---------------|
| Invalid program path | DAP error response | `CreateProcess2` fails, logged |
| CreateProcess2 failure | DAP error response | HRESULT logged, session not started |

---

### Feature 2: Attach Mode

**User Flow (Happy Path):**
1. User starts debug session with attach configuration including PID
2. whatdbg calls `AttachProcess(0, pid, 0)` — invasive attach
3. PID stored on `State::targetProcessId`
4. Target stops at initial breakpoint
5. User continues from configurationDone

**Edge Cases:**
- Invalid PID: `AttachProcess` returns failure
- Process exits during attach: `ExitProcess` callback fires

---

### Feature 3: Breakpoints

**User Flow (Happy Path):**
1. User sets breakpoint in plugin source file via nvim-dap
2. DAP `setBreakpoints` request received with source path and line numbers
3. whatdbg calls `getOffsetByLine` to resolve source:line to address
4. If resolved: `addBreakpoint` creates code breakpoint, response has `verified: true`
5. If not resolved (module not loaded yet): added to pending list, response has `verified: false`
6. When module loads: `LoadModule` callback fires, pending BPs retried after per-module `.reload /f`
7. When BP hits: `Breakpoint` callback fires, DAP `stopped` event with `reason: "breakpoint"`

**Deferred Resolution:**
- BPs set before module load: pending, resolved on `LoadModule` event
- BPs set after module load: `forceReloadAllSymbols()` (global `.reload /f`) called once, then retry
- Resolution strategy: full Windows path first, basename fallback with reverse-verify via `getLineByOffset`

**Edge Cases:**
- BP on function signature line: resolves to first executable line inside body (PDB/MSVC behavior)
- BP on blank/comment line: `kLineSearchWindow` (4 lines) searches nearby executable lines
- Module with `!` in name (e.g., "JRENG! Filter Strip"): `juce::String::quoted()` handles dbgeng command parsing
- Plugin removed and re-added: `UnloadModule` + `LoadModule` — pending BPs re-resolve on new `LoadModule`
- BP removed while pending: cleaned from pending list in `handleSetBreakpoints`

**Error Handling:**

| Condition | User Sees | System Action |
|-----------|-----------|---------------|
| Source file not in any loaded module | `verified: false`, pending message | BP added to pending list |
| `getOffsetByLine` E_UNEXPECTED | BP stays pending | Logged, no crash |
| Module load storm (100+ modules) | Brief delay during startup | Per-module `.reload /f` (not global) |

---

### Feature 4: Stepping

**User Flow (Happy Path):**
1. Target stopped at breakpoint or after pause
2. User presses F10 (next), F11 (stepIn), or stepOut keybinding
3. whatdbg calls `SetExecutionStatus(STEP_OVER/STEP_INTO)` or `Execute("gu")` for stepOut
4. `SetCodeLevel(DEBUG_LEVEL_SOURCE)` ensures source-level stepping (not instruction-level)
5. `WaitForEvent` returns `S_OK` — step completion detected by `isStepPending` flag + no other callback flags
6. DAP `stopped` event emitted with `reason: "step"`

**stepOut Detection:**
stepOut (`gu`) uses an internal breakpoint at the return address. `Breakpoint` callback fires with an engineId NOT in `engineToDap`. When `isStepPending` is true and `isUserBreakpoint(engineId)` returns false: treated as step completion, emits `reason: "step"` instead of `"breakpoint"`.

**Edge Cases:**
- Step into system code (no source): cursor shows disassembly location in stack, no source mapping
- Step during module load: handled by deferred event priority guards

---

### Feature 5: Pause

**User Flow (Happy Path):**
1. Target running, user presses pause in nvim-dap
2. DAP `pause` request received
3. whatdbg calls `OpenProcess(PROCESS_ALL_ACCESS, FALSE, targetProcessId)` + `DebugBreakProcess(handle)` + `CloseHandle(handle)`
4. Injected `EXCEPTION_BREAKPOINT` arrives via `Exception` callback
5. `WaitForEvent` returns `S_OK` — pause detected by `isPausePending` flag + no other callback flags
6. DAP `stopped` event emitted with `reason: "pause"`
7. Stack trace shows current location (typically system code, not plugin code)
8. User can continue, set breakpoints, step, or inspect variables

**PID Source:**
- Launch mode: captured from `CreateProcess` callback via `GetProcessId(handle)`
- Attach mode: stored directly from `handleAttach` parameter

**Edge Cases:**
- Pause shows `ntdll!DbgBreakPoint` in stack: expected — injected break-in thread
- Continue after pause: `SetExecutionStatus(DEBUG_STATUS_GO)` resumes normally
- Breakpoints after pause: work normally, `.reload /f` ensures symbols loaded
- nvim-dap "No thread to stop": fixed by emitting DAP `thread` event with `reason: "started"` on target start

---

### Feature 6: Variable Inspection

**User Flow (Happy Path):**
1. Target stopped at breakpoint/step/pause
2. nvim-dap sends `scopes` request with `frameId`
3. whatdbg decodes frameId → (threadSystemId, frameIndex) from `frameIdMap`
4. Sets thread context via `setCurrentThreadBySystemId`
5. Returns "Locals" scope with `variablesReference`
6. nvim-dap sends `variables` request with `variablesReference`
7. whatdbg calls `getLocals(frameIndex)` or `getVariableChildren(frameIndex, symbolIndex)`
8. Variables displayed with name, value, type, expand triangle for composite types

**Symbol Group:**
- `IDebugSymbolGroup2` via `GetScopeSymbolGroup2(DEBUG_SCOPE_GROUP_ALL)`
- Cached per frame via `getOrCreateSymbolGroup`, invalidated on every stop event
- Expansion via `ExpandSymbol` — children persist in cached group across requests

**Value Formatting (formatSymbolValue):**
- `0n877` → `877` (dbgeng decimal prefix stripped)
- `0x00000000\`10db01b0` → `0x0000000010db01b0` (backtick stripped)
- `0x... class Foo *` → `0x...` (trailing type stripped from pointers)
- `class juce::String` / `struct Foo` → empty (type column + triangle sufficient)

**Pretty-Printing (4 types):**
- `juce::String`: expand text → data → `ReadMultiByteStringVirtual` → `"actual content"`
- `std::basic_string<char>`: SSO detection (_Myres <= 15 → _Buf, else _Ptr) → `"content"`
- `std::unique_ptr<T>`: _Mypair._Myval2 → address or `null`
- `std::vector<T>`: _Myfirst/_Mylast pointer diff / element type size → `size=N`

**Filtered Symbols:**
- `<begin>$L0`, `<end>$L0`, `<range>$L0` — MSVC range-for internals
- `leakDetectorNNN` — JUCE leak detector members
- `__vfptr` — vtable pointers

**variablesReference Scheme:**
- `nextVariablesRef` counter, resets on every stop event
- Scope ref: maps to (frameIndex, -1) — top-level locals
- Child ref: maps to (frameIndex, symbolIndex) — expanded variable's children
- `variablesRefMap: std::unordered_map<int, std::pair<int, int>>`

**Frame ID Scheme:**
- `nextFrameId` counter, resets on every stop event
- `handleStackTrace` assigns unique IDs per frame across all threads
- `frameIdMap: std::unordered_map<int, std::pair<ULONG, int>>` — frameId → (threadSystemId, frameIndex)

**Edge Cases:**
- `static constexpr` members: `<Value unavailable error>` — compiler optimized out, no runtime address
- Variables in optimized frames: `<unavailable>` — register not live
- `GetSymbolValueText` E_FAIL: displayed as `<unavailable>`

---

### Feature 7: Expression Evaluation

**User Flow (Happy Path):**
1. Target stopped, user types expression in nvim-dap REPL
2. DAP `evaluate` request received with expression string and optional frameId
3. whatdbg creates secondary client via `client->CreateClient`
4. `.symopt- 100` enables unqualified local variable resolution
5. `Execute("?? expression")` via secondary client, output captured by `CaptureOutputCallback`
6. Result parsed: backtick stripped, `0n` prefix stripped
7. If result contains `juce::String`: additionally evaluates `(expr).text.data` or `(expr)->text.data` via `Evaluate`, reads string content via `ReadMultiByteStringVirtual`
8. DAP evaluate response with formatted result

**Supported Expressions:**
- Local variable names: `parameterID`, `this`, `newValue`
- Member access: `this->processor`, `p->paramID`
- Pointer dereference: `*ptr`
- Array subscript: `arr[i]`
- Arithmetic: `x + y`, `a * b`
- Type casts: `(int*)addr`, `static_cast<float>(x)`
- sizeof: `sizeof(MyStruct)`

**Not Supported:**
- Method calls: `str.toRawUTF8()`, `vec.size()` — dbgeng cannot execute code in target
- Error text returned for unsupported expressions

**Edge Cases:**
- Hover evaluation: `supportsEvaluateForHovers: true` in capabilities
- Empty result: DAP error response "Could not evaluate: expression"

---

### Feature 8: OutputDebugString Capture

**User Flow (Happy Path):**
1. Plugin calls `DBG("message")` or `OutputDebugString("message")`
2. dbgeng intercepts via `Output2` callback with `arg & DEBUG_OUTPUT_DEBUGGEE`
3. Text accumulated on `State::debuggeeOutputText` (deferred pattern)
4. Main loop emits DAP `output` event with `category: "console"`
5. Message appears in nvim-dap REPL panel

**Output Mask:**
`SetOutputMask(DEBUG_OUTPUT_NORMAL | DEBUG_OUTPUT_WARNING | DEBUG_OUTPUT_ERROR | DEBUG_OUTPUT_DEBUGGEE)` configured at initialization.

**Edge Cases:**
- Engine diagnostic output (`DEBUG_OUTPUT_NORMAL`): filtered out — only `DEBUG_OUTPUT_DEBUGGEE` captured
- Output during module loading: captured normally
- DAP Console vs REPL routing: output events go to dap-repl (nvim-dap-ui limitation #306, not configurable)

---

### Feature 9: Multi-Thread Support

**User Flow (Happy Path):**
1. Target stopped, nvim-dap sends `threads` request
2. whatdbg enumerates all threads via `IDebugSystemObjects::GetThreadIdsByIndex`
3. Thread names resolved via `GetThreadDescription` (Win10 1607+), fallback to "Thread <TID>"
4. OS TID used as DAP threadId
5. nvim-dap shows thread list in stacks panel
6. User clicks different thread → `stackTrace` request with that threadId
7. whatdbg calls `setCurrentThreadBySystemId` before `getStackTrace`

**Thread Context:**
- `handleScopes` decodes frameId → threadSystemId, sets thread context
- `handleVariables` restores from `lastScopesThreadId`
- Stopped events use `getEventThreadSystemId()` for real event thread OS TID

---

### Feature 10: Terminate / Disconnect

**User Flow:**
- `terminate` command or `disconnect` with `terminateDebuggee: true` → `DEBUG_END_ACTIVE_TERMINATE` — kills target process
- `disconnect` without `terminateDebuggee` → `DEBUG_END_ACTIVE_DETACH` — detaches, target continues

---

## Architecture Constraints

- Zero early returns — positive nested checks only
- No raw owning pointers — ComPtr for COM objects
- No magic numbers — named constants
- `not`/`and`/`or` — never `!`/`&&`/`||`
- Brace initialization everywhere
- `#include <JuceHeader.h>` — never individual JUCE modules
- Agents never run git commands
- Ignore all LSP errors (JUCE module system false positives)

---

## Success Criteria

A user can:
- [x] Launch REAPER from nvim-dap and debug a loaded JUCE plugin
- [x] Set breakpoints in plugin source code before or after module load
- [x] Hit breakpoints and see correct source location + stack trace
- [x] Step through code (next, stepIn, stepOut) at source level
- [x] Pause a running target and see current location
- [x] Inspect local variables with values at any stack frame
- [x] Expand structs/classes to see member values
- [x] See human-readable values for juce::String, std::string, std::unique_ptr, std::vector
- [x] Evaluate expressions in the REPL (member access, arithmetic, casts)
- [x] See OutputDebugString / DBG() output in nvim-dap
- [x] Switch between threads and inspect their stack traces
- [x] Terminate the debug session and kill the target process
- [x] Continue after pause and breakpoints still work

The system:
- [x] Compiles with MSVC, zero behavior-affecting warnings
- [x] Follows BLESSED principles throughout
- [x] Has comprehensive doxygen documentation on all public APIs
- [x] Has debug-only file logging (zero overhead in Release)
- [x] Handles module load storms without freezing (per-module reload)
- [x] Caches symbol groups for stepping performance

---

**End of SPEC v1.0**

**JRENG!**
