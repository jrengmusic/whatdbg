# whatdbg Specification v1.0
## Cross-Platform Debug Adapter

**Repository:** https://github.com/jrengmusic/whatdbg

## Overview

**Purpose:** whatdbg is a DAP debug adapter for C/C++ development with neovim. On Windows it uses Microsoft's dbgeng (the engine behind WinDbg). On macOS it uses LLVM's liblldb (the engine behind Xcode's debugger). Both backends are embedded as BinaryData and extracted at runtime (sidecar pattern).
**Target End-User:** C/C++ developer using neovim + nvim-dap on Windows or macOS.
**Core Workflow:** Launch or attach to any native executable (standalone apps, DAW-hosted plugins, services), set breakpoints, step through code, inspect variables, evaluate expressions — all from nvim-dap.

---

## Technology Stack

- **Language:** C++17
- **Framework:** JUCE (console application, no GUI)
- **Debug Engine:** Microsoft dbgeng COM API (Windows) / LLVM liblldb SB API (macOS) — both sidecar, embedded via BinaryData
- **Protocol:** Debug Adapter Protocol (DAP) over stdin/stdout
- **DAP Client:** nvim-dap + nvim-dap-ui
- **Build:** CMake + Ninja, MSVC toolchain (Windows) / Xcode clang toolchain (macOS)
- **Platform:** Windows 10+ (x64, ARM64) / macOS 12+ Monterey (arm64, x86_64)

---

## Core Principles

1. **Two-Thread Model:** Main thread owns the debug engine (COM on Windows, liblldb on macOS), DAP dispatch, and stdout. Stdin thread is a dumb FIFO buffer. No cross-thread writes.
2. **Deferred Events:** the platform backend (COM callbacks on Windows, `pollEvents`'s stop dispatch on macOS) stores flags on State. Main loop consumes flags and emits DAP events. The backend never writes to stdout directly.
3. **Sidecar Isolation:** Debug engine DLLs/dylibs are extracted from BinaryData at startup. LoadLibrary/dlopen from extracted path ensures deterministic version. On macOS, a re-exec trampoline sets DYLD_LIBRARY_PATH before the second invocation.
4. **BLESSED Compliance:** All code follows Bound, Lean, Explicit, SSOT, Stateless, Encapsulation, Deterministic principles.
5. **Debug-Only Logging:** File logging via `jam::debug::Log`, installed by a `jam::debug::Log::Scope` compiled out in Release builds (`#if JUCE_DEBUG`).

---

## Features

### Feature 1: Launch Mode

**User Flow (Happy Path), Windows:**
1. User starts debug session in nvim-dap with launch configuration
2. whatdbg receives DAP `launch` request with program path
3. whatdbg calls `CreateProcess2` with `DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE`
4. DAW launches in a new console window
5. whatdbg captures PID from `CreateProcess` callback via `GetProcessId(handle)`
6. Initial `EXCEPTION_BREAKPOINT` (INT3) fires — whatdbg holds target stopped
7. User's DAP client sends `configurationDone`
8. whatdbg resumes target, emits DAP `thread` event with `reason: "started"`
9. DAW loads normally, user works with it

**User Flow (Happy Path), macOS:**
1. User starts debug session in nvim-dap with launch configuration
2. whatdbg receives DAP `launch` request with program path
3. whatdbg builds an `SBLaunchInfo` with `eLaunchFlagDebug | eLaunchFlagStopAtEntry` and calls `target.Launch`
4. whatdbg captures PID from the returned `SBProcess`
5. The target stops at entry (liblldb's own stop-at-entry, not an injected breakpoint)
6. User's DAP client sends `configurationDone`
7. whatdbg resumes target, emits DAP `thread` event with `reason: "started"`
8. The program runs normally, user works with it

**Edge Cases:**
- Program path with spaces: quoted automatically (`"C:\Program Files\..."`) on Windows; macOS passes argv entries unquoted through `SBLaunchInfo`
- Program not found: `CreateProcess2` (Windows) / `target.Launch` (macOS) returns failure, DAP error response sent
- Target crashes during launch: exit is observed via the platform's own event path (Windows `ExitProcess` callback, macOS `onProcessStateStopped`/`eStateExited`), DAP `exited` + `terminated` events emitted

**Error Handling:**

| Condition | User Sees | System Action |
|-----------|-----------|---------------|
| Invalid program path | DAP error response | Launch call fails, logged |
| Launch failure | DAP error response | Error logged, session not started |

---

### Feature 2: Attach Mode

**User Flow (Happy Path):**
1. User starts debug session with attach configuration including PID
2. whatdbg attaches to the running process — Windows: `AttachProcess(0, pid, 0)` (invasive attach); macOS: `SBTarget::AttachToProcessWithID`
3. PID stored on `State::targetProcessId`
4. Target stops at the current instruction
5. User continues from configurationDone

**Edge Cases:**
- Invalid PID: attach call returns failure on both platforms
- Process exits during attach: observed via the platform's own event path (Windows `ExitProcess` callback, macOS `onProcessStateStopped`/`eStateExited`)

---

### Feature 3: Breakpoints

**User Flow (Happy Path):**
1. User sets breakpoint in a source file via nvim-dap
2. DAP `setBreakpoints` request received with source path and line numbers
3. whatdbg calls `Session::addBreakpointByLocation`, which internally calls `getOffsetStatus`, then `getOffset`, then `addBreakpoint` on both platforms
4. If resolved: response has `verified: true`
5. If not resolved (module not loaded yet): the registry entry keeps `engineId == 0`, response has `verified: false`
6. When a module loads: pending BPs are retried — Windows via the `LoadModule` callback after a per-module `.reload /f`; macOS via `onProcessStateStopped`'s module-list check
7. When a BP hits: DAP `stopped` event with `reason: "breakpoint"` — Windows via the `Breakpoint` callback, macOS via `onProcessStateStopped`'s `lldb::eStopReasonBreakpoint` case

**Deferred Resolution:**
- BPs set before module load: `engineId == 0`, resolved once the owning module loads
- Windows: BPs set after module load call `forceReloadAllSymbols()` (global `.reload /f`) once, then retry
- Resolution strategy: `getSourceOffset` (`Source/debug/Session.cpp`, a file-local `static` function — not a `Session` member) tries the full Windows path first. If that fails it retries with the basename, then matches the stored PDB path against the requested path before it returns the address. `Session` owns this match. The caller does not re-check it. macOS resolves via `getLineEntry` (`Session_mac.cpp`, file-local `static`), which scans every loaded module's compile units with `SBCompileUnit::FindLineEntryIndex` against an `SBFileSpec` for the requested path.
- `State::hasPendingBreakpoints` is the one truth for unresolved work. `BreakpointManager::hasUnresolvedBreakpoints()` derives it from the registry.

**Edge Cases:**
- BP on function signature line: resolves to first executable line inside body (PDB/MSVC behavior)
- BP on blank/comment line: `lineSearchWindow` (4 lines) searches nearby executable lines
- Module with `!` in name (e.g., "JRENG! Filter Strip"): `juce::String::quoted()` handles dbgeng command parsing
- Plugin removed and re-added: `UnloadModule` + `LoadModule` — pending BPs re-resolve on new `LoadModule`
- BP removed while unresolved: `removeOrphanedBreakpoints` erases it from the registry

**Error Handling:**

| Condition | User Sees | System Action |
|-----------|-----------|---------------|
| Source file not in any loaded module | `verified: false`, pending message | Registry entry keeps `engineId == 0` |
| `getOffsetStatus` E_UNEXPECTED | BP stays unresolved | `engineBusy` stops the line search, no crash |
| Module load storm (100+ modules) | Brief delay during startup | Per-module `.reload /f` (not global) |

---

### Feature 4: Stepping

**User Flow (Happy Path), Windows:**
1. Target stopped at breakpoint or after pause
2. User presses F10 (next), F11 (stepIn), or stepOut keybinding
3. whatdbg calls `SetExecutionStatus(STEP_OVER/STEP_INTO)` or `Execute("gu")` for stepOut
4. `SetCodeLevel(DEBUG_LEVEL_SOURCE)` ensures source-level stepping (not instruction-level)
5. `WaitForEvent` returns `S_OK` — step completion detected by `isStepPending` flag + no other callback flags
6. DAP `stopped` event emitted with `reason: "step"`

**User Flow (Happy Path), macOS:**
1. Target stopped at breakpoint or after pause
2. User presses F10 (next), F11 (stepIn), or stepOut keybinding
3. whatdbg calls `Session::stepOver` (`SBThread::StepOver`), `Session::stepInto` (`SBThread::StepInto`), or `Session::stepOut` (`SBThread::StepOutOfFrame`)
4. `session.pollEvents` dispatches the resulting stop to `onStepStop` on `lldb::eStopReasonTrace` or `lldb::eStopReasonPlanComplete`, setting `hasStepCompleted`
5. DAP `stopped` event emitted with `reason: "step"`

**stepOut Detection (Windows only):**
stepOut (`gu`) uses an internal breakpoint at the return address. The `Breakpoint` callback fires with an `engineId` not tracked by `BreakpointManager`. When `isStepPending` is true and `BreakpointManager::isUserBreakpoint(engineId)` returns false: treated as step completion, emits `reason: "step"` instead of `"breakpoint"`. macOS's `SBThread::StepOutOfFrame` reports its completion directly as `eStopReasonPlanComplete` — no internal breakpoint or `isUserBreakpoint` check is needed on that platform.

**Edge Cases:**
- Step into system code (no source): cursor shows disassembly location in stack, no source mapping
- Step during module load: handled by deferred event priority guards

---

### Feature 5: Pause

**User Flow (Happy Path), Windows:**
1. Target running, user presses pause in nvim-dap
2. DAP `pause` request received
3. whatdbg calls `OpenProcess(PROCESS_ALL_ACCESS, FALSE, targetProcessId)` + `DebugBreakProcess(handle)` + `CloseHandle(handle)`
4. Injected `EXCEPTION_BREAKPOINT` arrives via `Exception` callback
5. `WaitForEvent` returns `S_OK` — pause detected by `isPausePending` flag + no other callback flags
6. DAP `stopped` event emitted with `reason: "pause"`
7. Stack trace shows current location (typically system code, not plugin code)
8. User can continue, set breakpoints, step, or inspect variables

**User Flow (Happy Path), macOS:**
1. Target running, user presses pause in nvim-dap
2. DAP `pause` request received
3. whatdbg calls `Session::interrupt`, which calls `SBProcess::SendAsyncInterrupt()` on the bound process
4. `session.pollEvents` dispatches the resulting stop to `onInterruptStop` on `lldb::eStopReasonInterrupt`, setting `hasPauseCompleted`
5. DAP `stopped` event emitted with `reason: "pause"`
6. User can continue, set breakpoints, step, or inspect variables

**PID Source:**
- Launch mode: captured from `CreateProcess` callback via `GetProcessId(handle)` (Windows), or the launched `SBProcess` (macOS)
- Attach mode: stored directly from the `attach` request's `pid` argument in `Whatdbg::onAttach`

**Edge Cases:**
- Windows: pause shows `ntdll!DbgBreakPoint` in stack — expected, the injected break-in thread
- Continue after pause: resumes normally on both platforms (`SetExecutionStatus(DEBUG_STATUS_GO)` on Windows, `Session::resume`'s `SBProcess::Continue` on macOS)
- Breakpoints after pause: work normally; Windows additionally runs `.reload /f` to ensure symbols are loaded
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

**Symbol Group (Windows):**
- `IDebugSymbolGroup2` via `GetScopeSymbolGroup2(DEBUG_SCOPE_GROUP_ALL)`
- Cached per frame via `getOrCreateSymbolGroup`, invalidated on every stop event
- Expansion via `ExpandSymbol` — children persist in cached group across requests

**Variable Values (macOS):**
- `SBFrame::GetVariables` returns the frame's `SBValue`s directly; each is formatted through `getVariableObject`, which reads name/value/type and recurses into `SBValue::GetChildAtIndex` for expandable variables — no separate symbol-group cache is needed, liblldb owns that caching internally

**Value Formatting (formatSymbolValue, Windows):**
- `0n877` → `877` (dbgeng decimal prefix stripped)
- `0x00000000\`10db01b0` → `0x0000000010db01b0` (backtick stripped)
- `0x... class Foo *` → `0x...` (trailing type stripped from pointers)
- `class juce::String` / `struct Foo` → empty (type column + triangle sufficient)

**Pretty-Printing (Windows, 4 types):**
- `juce::String`: expand text → data → `ReadMultiByteStringVirtual` → `"actual content"`
- `std::basic_string<char>`: SSO detection on `_Mysize < 16` (not `_Myres`) → `_Buf` if true, else `_Ptr` → `"content"`
- `std::unique_ptr<T>`: `_Mypair._Myval2` → address or `null`
- `std::vector<T>`: `_Myfirst`/`_Mylast` pointer diff / element type size → `size=N`

**Pretty-Printing (macOS):** `SessionPrettyPrint_mac.cpp` formats `juce::String`, `std::unique_ptr<T>`, and the same filtered-symbol set below through liblldb's own `SBValue` summary/child API rather than raw memory reads.

**Filtered Symbols (both platforms, `debug::shouldSkipSymbol`, `Source/debug/PrettyPrint.h`):**
- Any name starting with `<` — covers MSVC range-for internals (`<begin>$L0`, `<end>$L0`, `<range>$L0`) and unavailable-value sentinels
- `leakDetectorNNN` — JUCE leak detector members
- `__vfptr` — vtable pointers
- `juce::compileUnitMismatchSentinel` — per-TU compile-unit mismatch guard

**variablesReference Scheme:**
- `nextVariablesRef` counter, resets on every stop event
- Scope ref: maps to (frameIndex, -1) — top-level locals
- Child ref: maps to (frameIndex, symbolIndex) — expanded variable's children
- `variablesRefMap: std::unordered_map<int, std::pair<int, int>>`

**Frame ID Scheme:**
- `nextFrameId` counter, resets on every stop event
- `onStackTrace` assigns unique IDs per frame across all threads
- `frameIdMap: std::unordered_map<int, std::pair<std::uint32_t, int>>` — frameId → (threadSystemId, frameIndex)

**Edge Cases:**
- Windows: `static constexpr` members: `<Value unavailable error>` — compiler optimized out, no runtime address
- Variables in optimized frames: `<unavailable>` — register not live
- Windows: `GetSymbolValueText` E_FAIL displayed as `<unavailable>`

---

### Feature 7: Expression Evaluation

**User Flow (Happy Path), Windows:**
1. Target stopped, user types expression in nvim-dap REPL
2. DAP `evaluate` request received with expression string and optional frameId
3. whatdbg creates secondary client via `client->CreateClient`
4. `.symopt- 100` enables unqualified local variable resolution
5. `Execute("?? expression")` via secondary client, output captured by `CaptureOutputCallback`
6. Result parsed by `evaluateStringValue`: backtick stripped, `0n` prefix stripped
7. If result contains `juce::String`: additionally evaluates `(expr).text.data` or `(expr)->text.data` via `Evaluate`, reads string content via `ReadMultiByteStringVirtual`
8. DAP evaluate response with formatted result

**User Flow (Happy Path), macOS:**
1. Target stopped, user types expression in nvim-dap REPL
2. DAP `evaluate` request received with expression string and optional frameId
3. whatdbg calls `Session::evaluateExpression`, which selects the requested frame and calls `SBFrame::EvaluateExpression`
4. The resulting `SBValue` is formatted through the same `getVariableObject`/pretty-print path used for variable inspection
5. DAP evaluate response with formatted result

**Supported Expressions:**
- Local variable names: `parameterID`, `this`, `newValue`
- Member access: `this->processor`, `p->paramID`
- Pointer dereference: `*ptr`
- Array subscript: `arr[i]`
- Arithmetic: `x + y`, `a * b`
- Type casts: `(int*)addr`, `static_cast<float>(x)`
- sizeof: `sizeof(MyStruct)`

**Not Supported, Windows:**
- Method calls: `str.toRawUTF8()`, `vec.size()` — dbgeng's `??` cannot execute code in the target
- Error text returned for unsupported expressions

**macOS:** `SBFrame::EvaluateExpression` is Clang-based and can execute code in the
target, so method-call support differs from Windows — not independently verified
by this document's audit pass; treat as untested until confirmed against `tests/smoke`.

**Edge Cases:**
- Hover evaluation: `supportsEvaluateForHovers: true` in capabilities
- Empty result: DAP error response "Could not evaluate: expression"

---

### Feature 8: Debuggee Output Capture

**User Flow (Happy Path), Windows:**
1. Target calls `DBG("message")` or `OutputDebugString("message")`
2. dbgeng intercepts via `Output2` callback with `arg & DEBUG_OUTPUT_DEBUGGEE`
3. Text accumulated on `State::debuggeeOutputText` (deferred pattern)
4. Main loop emits DAP `output` event with `category: "console"`
5. Message appears in nvim-dap REPL panel

**User Flow (Happy Path), macOS:**
1. Target writes to stdout or stderr
2. `drainProcessStdio` (`Session_mac.cpp`, called from `pollEvents`) polls `SBProcess::GetSTDOUT`/`GetSTDERR`
3. Text accumulated on `State::debuggeeOutputText`, `State::debuggeeOutputCategory` set to `"stdout"`/`"stderr"`
4. Main loop's `drainDebuggeeOutput` emits DAP `output` event with the captured category
5. Message appears in nvim-dap REPL panel

**Output Mask (Windows):**
`SetOutputMask(DEBUG_OUTPUT_NORMAL | DEBUG_OUTPUT_WARNING | DEBUG_OUTPUT_ERROR | DEBUG_OUTPUT_DEBUGGEE)` configured at initialization.

**Edge Cases:**
- Windows: engine diagnostic output (`DEBUG_OUTPUT_NORMAL`) is filtered out — only `DEBUG_OUTPUT_DEBUGGEE` captured
- Output during module loading: captured normally
- DAP Console vs REPL routing: output events go to dap-repl (nvim-dap-ui limitation #306, not configurable)
- macOS has no `os_log` capture — only the debuggee's own stdout/stderr streams are drained

---

### Feature 9: Multi-Thread Support

**User Flow (Happy Path):**
1. Target stopped, nvim-dap sends `threads` request
2. whatdbg enumerates all threads — Windows: `IDebugSystemObjects::GetThreadIdsByIndex`; macOS: `SBProcess::GetThreadAtIndex`
3. Thread names resolved — Windows: `GetThreadDescription` (Win10 1607+), fallback to "Thread <TID>"; macOS: `SBThread::GetName()`, which has no Windows-equivalent OS thread-description API behind it and returns liblldb's own fallback for unnamed threads
4. OS TID used as DAP threadId
5. nvim-dap shows thread list in stacks panel
6. User clicks different thread → `stackTrace` request with that threadId
7. whatdbg calls `setCurrentThreadBySystemId` before `getStackTrace`

**Thread Context:**
- `onScopes` decodes frameId → threadSystemId, sets thread context
- `onVariables` restores from `lastScopesThreadId`
- Stopped events use `getEventThreadSystemId()` for the real event thread's OS TID

---

### Feature 10: Terminate / Disconnect

**User Flow:**
- `terminate` command, or `disconnect` with `terminateDebuggee: true` — kills the target process. Windows: `DEBUG_END_ACTIVE_TERMINATE`. macOS: `process.Signal (SIGKILL)` followed by `resume()`, so the kill's exit event is observed through the normal `pollEvents` path rather than torn down blind (see the zombie-termination fix below).
- `disconnect` without `terminateDebuggee` — detaches, target continues running. Windows: `DEBUG_END_ACTIVE_DETACH`. macOS: `process.Detach()`.

**Two call sites, same `EndMode`:** `Whatdbg::onDisconnect` sends the kill/detach
signal immediately and moves `executionState` to `running` so the main loop keeps
polling for the debuggee's exit; `Session::shutdown (EndMode)` runs once more at
`run()`'s exit (via `getEndModeForExit()`) to finish tearing down the session —
`EndMode::passive` if the debuggee has already exited, `EndMode::terminate` if
`shouldTerminateOnExit` was set, otherwise `EndMode::detach`.

**macOS zombie-termination fix:** Terminating a launched (not attached) macOS
debuggee previously left it as an unreapable zombie (`STAT=Z`, reparented to
`launchd`, which never reaps a zombie it inherits) — a raw `::kill()` was
followed immediately by `SBDebugger::Destroy()` with no reap of the debuggee's
exit status in between, severing `debugserver`'s ptrace connection before the
kernel collected the exit. The fix sends `SIGKILL` through `SBProcess::Signal`
and then calls `resume()` so the kill's resulting exit event is observed via
`pollEvents`/`onProcessStateStopped` — the same path every other stop already
uses — before the session is torn down. `Session::shutdown` also guards
`SBDebugger::Destroy`/`Terminate` behind `debugger.IsValid()`, making a second
`shutdown()` call (the destructor calls it unconditionally after `run()` already
called it explicitly) a no-op instead of a double-terminate. Covered by
`tests/smoke/scenario_terminate.lua`'s `scenarioTerminateNoZombie`.

---

### Feature 11: Target Crash / Exception Info Surfacing

**Trigger, Windows:** Second-chance unhandled exception in the debuggee process (dbgeng `EXCEPTION` event with `firstChance == 0`).

**Trigger, macOS:** A POSIX signal or Mach exception stop (`lldb::eStopReasonSignal` or `lldb::eStopReasonException`) reported through `onProcessStateStopped`.

**Detection:**
- Windows: `onUnknownException` (`Source/debug/Callbacks.cpp`) populates `State::hasExceptionStopped`, `State::exceptionCode`, `State::exceptionAddress` on second-chance, sets `State::executionState = stopped`, returns `DEBUG_STATUS_BREAK`
- macOS: `onSignalStop`/`onExceptionStop` (`Source/debug/Session_mac.cpp`) populate the same `State` fields plus `State::isMachException`, distinguishing a Mach exception from a POSIX signal

**DAP Surfacing (`drainExceptionStopped` in `Source/Whatdbg.cpp`):**
- Emits `stopped` event with `reason: "exception"`, `text: <exception name>`, `description: "0x<code> at 0x<address>"`, `threadId: <event thread system id>`, `allThreadsStopped: true`
- Emits `output` event with `category: "stderr"` containing formatted crash summary
- Clears `hasExceptionStopped`; preserves `exceptionCode` and `exceptionAddress` for subsequent `exceptionInfo` request

**`exceptionInfo` DAP Request Handler (`Whatdbg::onExceptionInfo`, `Source/WhatdbgHandlers.cpp`):**
- Responds with `exceptionId` (name or hex fallback), `description`, `breakMode: "unhandled"`

**Capability:** `supportsExceptionInfoRequest: true` advertised in `Source/dap/Types.h`.

**Exception-Name Lookup:**
- Windows: NTSTATUS/SEH codes mapped to short names in `Source/debug/Callbacks.cpp`, accessed via `debug::getExceptionName`
- macOS: two tables in `Source/debug/Session_mac.cpp` — `machExceptionNames` (Mach exception types) and `signalNames` (POSIX signals, several entries reusing a Mach exception's name for the signal that maps to it), selected by `isMachException` and accessed via the same `debug::getExceptionName (code, isMachException)`
- Both platforms fall back to `"0x<hex>"` for an unrecognized code

---

## Architecture Constraints

- No bail-out guards — positive nested checks; result returns (the value determined at that point) are correct and preferred, per MANIFESTO **E**
- No raw owning pointers — `ComPtr<T>` for COM objects (Windows); liblldb SB API value types own their own lifetime (macOS)
- No magic numbers — named constants
- `not`/`and`/`or` — never `!`/`&&`/`||`
- Brace initialization everywhere
- `#include <JuceHeader.h>` — never individual JUCE modules
- Agents never run git commands
- Ignore all LSP errors (JUCE module system false positives)

---

## Success Criteria

A user can, on both Windows and macOS unless noted:
- [x] Launch or attach to a target and debug it (REAPER + JUCE plugin on Windows; any native macOS binary)
- [x] Set breakpoints in source code before or after module load
- [x] Hit breakpoints and see correct source location + stack trace
- [x] Step through code (next, stepIn, stepOut) at source level
- [x] Pause a running target and see current location
- [x] Inspect local variables with values at any stack frame
- [x] Expand structs/classes to see member values
- [x] See human-readable values for juce::String, std::string (Windows), std::unique_ptr
- [x] Evaluate expressions in the REPL (member access, arithmetic, casts)
- [x] See debuggee stdout/OutputDebugString/DBG() output in nvim-dap
- [x] Switch between threads and inspect their stack traces
- [x] Terminate the debug session and kill the target process without leaving a zombie (macOS)
- [x] Continue after pause and breakpoints still work
- [x] Disconnect and leave the target running (both platforms)

The system:
- [ ] Windows: compiles with MSVC — the Windows code written this sprint has not
      been compiled; MSVC compilation is unverified as of this document
- [x] macOS: compiles with Xcode clang
- [x] Follows BLESSED principles throughout
- [x] Header doxygen present across `Source/Whatdbg.h`, `debug/State.h`,
      `debug/Session.h`, `debug/BreakpointManager.h`, `debug/Callbacks.h`,
      `debug/Loader.h`, `dap/Reader.h`, `dap/Types.h`, `debug/PrettyPrint.h` —
      completeness against every public API is a documentation-pass concern,
      not re-verified line-by-line here
- [x] Has debug-only file logging (zero overhead in Release)
- [x] Handles module load storms without freezing (per-module reload, Windows)
- [x] Caches symbol groups for stepping performance (Windows; liblldb owns
      equivalent caching internally on macOS)
- [x] `tests/smoke/` — ten scenarios (`run_smoke.lua`, `scenario_breakpoint.lua`,
      `scenario_process.lua`, `scenario_terminate.lua`) covering launch+breakpoint,
      step, attach, pause, variables, evaluate, output, crash,
      terminate-without-zombie, and disconnect-detach; all ten pass on macOS

---

**End of SPEC v1.0**

**JRENG!**
