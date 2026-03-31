# WHATDBG Specification v1.1

**Full Name:** Windows Host Attachable Tool — DeFuGging  
**Purpose:** A DAP-compliant debug adapter for Windows that enables MSVC C++17 debugging in Neovim, replacing the broken codelldb/GDB situation on Windows with a single reliable binary backed by `dbgeng.dll`.  
**Target User:** Terminal-native C++17/JUCE developers on Windows using Neovim + nvim-dap + Mason.  
**Core Workflow:** Drop-in replacement for codelldb on Windows. Identical surface API. Zero changes to existing nvim-dap Lua configuration except adapter name.

---

## Technology Stack

- **Language:** C++17
- **Build System:** CMake + Ninja
- **Platform:** Windows 10 Pro 22H2 (OS Build 19045.6466) and above, 64-bit only
- **Debug Engine:** `dbgeng.dll` (Windows inbox, Debugging Tools for Windows COM interfaces)
- **JSON:** nlohmann/json (single header)
- **DAP Framing:** Hand-rolled (stdin/stdout, `Content-Length` header protocol)
- **Distribution:** Single `.exe` binary, no installer, no runtime dependencies beyond inbox Windows DLLs
- **Mason Integration:** Proper `mason-registry` entry, versioned GitHub releases with pre-built binary and checksum verification

---

## Core Principles

1. **codelldb API Parity:** Every DAP field, request, response, and event WHATDBG handles must behave identically to codelldb on macOS. If codelldb accepts it, WHATDBG accepts it. If codelldb rejects it, WHATDBG rejects it with an equivalent error.
2. **Single Binary:** No side binaries, no Node.js shim, no DLL alongside the exe. `dbgeng.dll` is resolved from the system at runtime. The exe is the entire adapter.
3. **Fail Fast, Fail Loud:** Any misconfiguration surfaces immediately as a clear DAP error output — no silent failures, no hangs. Match codelldb's exact hard-fail vs warn-and-continue behavior per condition.
4. **MSVC/PDB Native:** Only MSVC-built binaries with PDB symbols are supported. No MinGW, no DWARF. This is a deliberate constraint, not a limitation.
5. **Stdio DAP Transport:** Communication with nvim-dap is exclusively over stdin/stdout using the DAP wire protocol. No TCP mode in MVP.

---

## Architecture Overview

```
nvim-dap  ──stdin/stdout──►  whatdbg.exe  ──COM calls──►  dbgeng.dll
          ◄── DAP JSON ──────              ◄── events ────
```

**Internal threading model:**
- **Main thread:** `dbgeng` event loop — calls `IDebugControl::WaitForEvent()`, blocks until debug event fires
- **IO thread:** stdin reader — reads DAP requests, enqueues to command queue
- **Dispatch:** Main thread dequeues ALL pending commands BEFORE calling `WaitForEvent` (WaitForEvent is NOT re-entrant — no COM calls during wait). Writes DAP responses/events to stdout.

**Key COM interfaces used:**
- `IDebugClient5` — process launch, attach, event callbacks, output callbacks
- `IDebugControl4` — execution control, breakpoints, expression evaluation, step commands
- `IDebugSymbols3` — symbol loading, source line mapping, PDB resolution
- `IDebugRegisters2` — register read/write
- `IDebugDataSpaces4` — memory read/write
- `IDebugSystemObjects4` — thread/process enumeration
- `IDebugBreakpoint2` — software breakpoints
- `IDebugSymbolGroup2` — variable scope enumeration and lazy expansion
- `IDebugAdvanced3` — source file resolution across build/debug machine paths

---

## DAP Surface API (codelldb-compatible)

### Adapter Registration (nvim-dap Lua)

```lua
dap.adapters.whatdbg = {
  type = 'executable',
  command = vim.fn.stdpath('data') .. '/mason/packages/whatdbg/whatdbg.exe',
}
```

### Launch Request Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `request` | `"launch"` | yes | |
| `program` | string | yes | Absolute path to `.exe` binary |
| `args` | string[] | no | Command line arguments |
| `cwd` | string | no | Working directory |
| `stopOnEntry` | bool | no | Break on entry point (default: false) |
| `console` | string | no | `"integratedTerminal"` or `"internalConsole"` |
| `env` | object | no | Environment variable overrides |

### Attach Request Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `request` | `"attach"` | yes | |
| `pid` | number | yes | Target process PID |
| `program` | string | yes | Absolute path to `.dll`/`.exe` for symbol loading |
| `cwd` | string | no | Working directory |
| `stopOnEntry` | bool | no | Break immediately on attach (default: false) |

### Supported DAP Requests (MVP)

| Request | Description |
|---|---|
| `initialize` | Capabilities handshake |
| `launch` | Spawn and debug a process |
| `attach` | Attach to running process by PID |
| `configurationDone` | Signal that all initial configuration is complete |
| `disconnect` | Detach / terminate session |
| `setBreakpoints` | Set/clear line breakpoints per source file |
| `setExceptionBreakpoints` | Break on C++ exceptions (caught/uncaught) |
| `continue` | Resume execution |
| `pause` | Break into debugger |
| `next` | Step over |
| `stepIn` | Step into |
| `stepOut` | Step out |
| `stackTrace` | Get call stack for a thread |
| `scopes` | Get variable scopes for a stack frame |
| `variables` | Get variables for a scope (lazy, paginated) |
| `evaluate` | Evaluate expression in frame context |
| `threads` | Enumerate all threads |
| `dataBreakpointInfo` | Hardware watchpoint support info |
| `setDataBreakpoints` | Set hardware watchpoints |

### Post-MVP DAP Requests

| Request | Description |
|---|---|
| `setVariable` | Modify variable value (deferred — read-only inspection covers 95% of use) |
| `source` | Retrieve source content fallback (deferred — nvim always has source open) |
| `completions` | Expression autocomplete in REPL (deferred — unclear dbgeng API support) |

### Supported DAP Events (MVP)

| Event | Trigger |
|---|---|
| `initialized` | After `initialize` request |
| `stopped` | Breakpoint hit, step complete, pause, exception |
| `continued` | Execution resumed |
| `exited` | Process exited |
| `terminated` | Debug session ended |
| `thread` | Thread created/exited |
| `module` | Module loaded/unloaded |
| `process` | Process launched/attached (name, PID) |
| `breakpoint` | Breakpoint status changed (verified/unverified after module load) |
| `output` | stdout, stderr, DBG() output, adapter log |

---

## Feature Specifications

### Feature 1: Launch Mode

**Happy Path:**
1. nvim-dap sends `initialize` request
2. WHATDBG responds with capabilities
3. nvim-dap sends `launch` request with `program` path
4. WHATDBG calls `IDebugClient::CreateProcess` with the target binary
5. WHATDBG sends `initialized` event
6. nvim-dap sends pending `setBreakpoints` requests
7. WHATDBG sends `configurationDone`
8. Process runs, stops at first breakpoint
9. WHATDBG sends `stopped` event with reason `"breakpoint"`

**Error Handling:**

| Condition | Behavior | DAP Response |
|---|---|---|
| `program` field missing | Hard fail | `ErrorResponse`: `"WHATDBG: 'program' field is required"` |
| `program` path does not exist | Hard fail | `ErrorResponse`: `"WHATDBG: executable not found: {path}"` |
| `CreateProcess` fails (permissions) | Hard fail | `ErrorResponse`: `"WHATDBG: failed to launch process: {win32 error}"` |
| PDB not found | Warn, continue | `output` event (category: `console`): `"WHATDBG: no PDB found for {module} — symbols unavailable"` |
| PDB found but mismatched | Warn, continue | `output` event: `"WHATDBG: PDB mismatch for {module} — rebuild recommended"` |

---

### Feature 2: Attach Mode

**Happy Path:**
1. nvim-dap sends `initialize` request
2. WHATDBG responds with capabilities
3. nvim-dap sends `attach` request with `pid` and `program`
4. WHATDBG calls `IDebugClient::AttachProcess` with the PID
5. WHATDBG loads symbols from `program` path via `IDebugSymbols3`
6. WHATDBG sends `initialized` event
7. nvim-dap sends pending `setBreakpoints`
8. WHATDBG sends `configurationDone`
9. Target process continues running (no implicit stop on attach unless `stopOnEntry: true`)

**Error Handling:**

| Condition | Behavior | DAP Response |
|---|---|---|
| `pid` field missing | Hard fail | `ErrorResponse`: `"WHATDBG: 'pid' field is required"` |
| `program` field missing | Hard fail | `ErrorResponse`: `"WHATDBG: 'program' field is required for symbol loading"` |
| PID not found / process not running | Hard fail | `ErrorResponse`: `"WHATDBG: process not found: PID {pid}"` |
| Access denied (not elevated) | Hard fail | `ErrorResponse`: `"WHATDBG: access denied — run as Administrator or check SeDebugPrivilege"` |
| `program` path does not exist | Hard fail | `ErrorResponse`: `"WHATDBG: symbol binary not found: {path}"` |
| PDB not found | Warn, continue | `output` event: `"WHATDBG: no PDB found for {module} — symbols unavailable"` |
| PDB mismatch | Warn, continue | `output` event: `"WHATDBG: PDB mismatch for {module} — rebuild recommended"` |
| Already debugged by another debugger | Hard fail | `ErrorResponse`: `"WHATDBG: cannot attach — process already has a debugger attached"` |

---

### Feature 3: Breakpoints

**Line Breakpoints:**
- Set via `setBreakpoints` request (source file + line array)
- Resolved via `IDebugSymbols3::GetOffsetByLine`
- WHATDBG maintains a `std::unordered_map<DapBreakpointId, IDebugBreakpoint2*>` internally
- Each `setBreakpoints` call for a source file replaces all previous breakpoints for that file
- Verified breakpoints report `verified: true` with resolved line number
- Unresolved breakpoints (no symbols, line outside code) report `verified: false` with message: `"WHATDBG: could not resolve breakpoint at {file}:{line}"`

**Deferred Breakpoint Resolution:**
- Breakpoints set before a module loads are stored as pending (`verified: false`)
- When `LoadModule` callback fires for a new DLL, all pending breakpoints are re-resolved against the new module
- Successfully resolved breakpoints emit a DAP `breakpoint` event with `reason: "changed"` and `verified: true`
- This is critical for the attach-to-DAW workflow where the plugin DLL loads after the debug session starts

**Conditional Breakpoints:**
- `condition` field on breakpoint evaluated via `IDebugControl::SetBreakpointExpression`
- If expression is invalid: breakpoint set as unverified, `output` event: `"WHATDBG: invalid breakpoint condition: {expression}"`

**Logpoints:**
- `logMessage` field on breakpoint
- On hit: evaluate any `{}` expressions in message via `IDebugControl::Evaluate`, emit `output` event (category: `console`) with formatted message, continue execution automatically

**Hardware Watchpoints (Data Breakpoints):**
- `dataBreakpointInfo` request returns hardware watchpoint capability
- `setDataBreakpoints` maps to `IDebugBreakpoint2` with `BREAKPOINT_DATA` flag
- Maximum 4 concurrent hardware watchpoints (x64 hardware limit)
- If limit exceeded: `ErrorResponse`: `"WHATDBG: hardware watchpoint limit reached (max 4)"`

---

### Feature 4: Execution Control

All step/continue commands map directly to dbgeng execution control:

| DAP Request | dbgeng Call |
|---|---|
| `continue` | `IDebugControl::SetExecutionStatus(DEBUG_STATUS_GO)` |
| `pause` | `IDebugControl::SetExecutionStatus(DEBUG_STATUS_BREAK)` |
| `next` (step over) | `IDebugControl::SetExecutionStatus(DEBUG_STATUS_STEP_OVER)` |
| `stepIn` | `IDebugControl::SetExecutionStatus(DEBUG_STATUS_STEP_INTO)` |
| `stepOut` | `IDebugControl::SetExecutionStatus(DEBUG_STATUS_STEP_OUT)` |

**Thread targeting:** All step commands operate on the thread specified in the DAP request's `threadId` field via `IDebugSystemObjects::SetCurrentThreadId`.

---

### Feature 5: Stack Frames

- `stackTrace` request returns frames for the specified `threadId`
- Each frame includes: frame id, function name, source file path, line number, column
- Frames without source info (system DLLs, no PDB) return frame with name only, no source location
- `startFrame` and `levels` pagination fields respected
- Inlined frames reported as separate entries with `presentationHint: "inline"`

---

### Feature 6: Variables and Scopes

**Scopes per frame:**
- `Locals` — local variables in current function scope
- `Arguments` — function parameters
- `Registers` — CPU register values (x64: rax, rbx, rcx, rdx, rsp, rbp, rip, etc.)

**Variable inspection:**
- Primitive types (int, float, double, bool, char, pointers) — direct value display
- `std::string` — display string content, not internal struct
- `std::vector<T>` — display size + indexed children
- `std::array<T,N>` — display indexed children
- `std::map`, `std::unordered_map` — display key/value pairs
- `std::optional` — display `nullopt` or contained value
- `std::unique_ptr`, `std::shared_ptr` — display pointed-to value
- Structs/classes — display named members as children (lazy, expanded on demand)
- Raw pointers — display address + dereferenced value as child
- Arrays — display indexed children up to 256 elements (configurable via `variablesReference`)
- JUCE `String` — display as UTF-8 string content
- JUCE `Array<T>` — display size + indexed children

**Variable modification:**
- `setVariable` request modifies value via `IDebugDataSpaces::WriteVirtual`
- Invalid value expression: `ErrorResponse`: `"WHATDBG: invalid value expression: {value}"`

---

### Feature 7: Expression Evaluation (REPL + Watch)

- `evaluate` request routes to `IDebugControl::Evaluate` in context of specified `frameId`
- Supports: variable names, pointer dereference (`*ptr`), member access (`obj.field`, `ptr->field`), array indexing (`arr[n]`), arithmetic expressions, cast expressions
- Result returned as DAP `EvaluateResponse` with `result` string and `variablesReference` for structured types
- Invalid expression: `result` = `"<error: {dbgeng error string}>"`, `variablesReference` = 0
- `context` field respected: `"watch"`, `"repl"`, `"hover"`, `"clipboard"`

**Limitation:** dbgeng's `Evaluate` supports variable access, member access, pointer dereference, array indexing, arithmetic, and casts. It does NOT support calling functions in the debuggee (e.g., `obj.method()` will not work). This is a deliberate constraint of the dbgeng evaluator, not a WHATDBG limitation.

---

### Feature 8: Output Capture (stdout / stderr / DBG())

This is a first-class feature, not an afterthought.

- WHATDBG registers `IDebugOutputCallbacks2` with dbgeng
- All output from the debugged process is captured and forwarded as DAP `output` events
- Output categories:

| Source | DAP `output` category | Mode |
|---|---|---|
| `OutputDebugString` / JUCE `DBG()` | `console` | Launch + Attach |
| Process stdout (console apps only) | `stdout` | Launch only |
| Process stderr (console apps only) | `stderr` | Launch only |
| WHATDBG internal log messages | `telemetry` | Always |

**Note:** stdout/stderr capture is only reliable for console applications in launch mode. GUI applications (JUCE standalone, DAWs) do not have stdout/stderr handles. `OutputDebugString` / `DBG()` is the primary and universal output mechanism — it works in both launch and attach modes regardless of application type.

- Output is forwarded in real time, not buffered until session end
- No output is silently dropped

---

### Feature 9: Multi-Thread Inspection

- `threads` request enumerates all threads via `IDebugSystemObjects4::GetNumberThreads`
- Each thread reports: thread id, name (if available via TEB)
- JUCE audio thread identified by name if set via `juce::Thread::setCurrentThreadName`
- All threads visible in nvim-dap's thread panel simultaneously
- Stepping and frame inspection can target any thread independently

---

### Feature 10: Exception Breakpoints

- `setExceptionBreakpoints` request with filters:
  - `"cpp_throw"` — break on any C++ exception throw
  - `"cpp_unhandled"` — break on unhandled exceptions only
- Mapped to dbgeng first-chance / second-chance exception handling
- When exception breakpoint fires: `stopped` event with reason `"exception"`, description contains exception type and message

---

## Error Handling Summary

| Condition | Behavior |
|---|---|
| `program` field missing (launch or attach) | Hard fail |
| Executable/binary path not found | Hard fail |
| PID not found | Hard fail |
| Access denied on attach | Hard fail with privilege hint |
| PDB not found | Warn in output console, continue |
| PDB mismatch (stale build) | Warn in output console, continue |
| Invalid breakpoint condition | Breakpoint unverified, warn in output |
| Hardware watchpoint limit exceeded | Hard fail per watchpoint |
| Expression evaluation error | Return error string as result, no hard fail |
| Already debugged by another debugger | Hard fail with message |
| WHATDBG crash / unexpected exit | Send `terminated` event, clean exit |
| dbgeng COM call failure | Log to `telemetry` output, surface as DAP error where applicable |

---

## Mason Integration

- **Package name:** `whatdbg`
- **Registry entry:** Standard mason-registry manifest format
- **Binary:** `whatdbg.exe` at package root
- **Version scheme:** Semantic versioning (`v1.0.0`)
- **Release artifacts:** Pre-built `whatdbg.exe` per GitHub release tag
- **Checksum:** SHA256 verified by Mason on install
- **Install path:** `{mason_data}/packages/whatdbg/whatdbg.exe`

**nvim-dap adapter config (final):**
```lua
local is_windows = vim.fn.has('win32') == 1
local standalone_adapter = is_windows and 'whatdbg' or 'codelldb'
local plugin_adapter = is_windows and 'whatdbg' or 'codelldb'
```

No other changes to `configurations.lua`, `adapters.lua`, or `dapui_config.lua`.

---

## CMake Project Structure

```
whatdbg/
├── CMakeLists.txt
├── src/
│   ├── main.cpp              -- entry point, stdin/stdout loop
│   ├── DapProtocol.hpp/cpp   -- DAP framing (Content-Length parser/writer)
│   ├── DapDispatcher.hpp/cpp -- request routing table
│   ├── DapTypes.hpp          -- nlohmann/json DAP message structs
│   ├── DbgEngSession.hpp/cpp -- IDebugClient/Control/Symbols wrapper
│   ├── DbgEngCallbacks.hpp/cpp -- IDebugEventCallbacks, IDebugOutputCallbacks2
│   ├── BreakpointManager.hpp/cpp -- DAP bp id <-> IDebugBreakpoint2 mapping
│   ├── VariableResolver.hpp/cpp  -- type-aware variable inspection
│   └── ThreadManager.hpp/cpp     -- thread enumeration and targeting
├── third_party/
│   └── nlohmann/json.hpp
└── .github/
    └── workflows/
        └── release.yml       -- build + upload exe + SHA256 on tag push
```

---

## Success Criteria

A developer can:
- [ ] `:MasonInstall whatdbg` on Windows 10 22H2+ and get a working binary
- [ ] Launch a JUCE standalone `.exe` and hit a breakpoint with full symbol resolution
- [ ] Attach to REAPER/Live/Bitwig with a loaded VST3/VST/AAX plugin and hit a breakpoint in the plugin code
- [ ] Inspect locals, arguments, `std::vector`, `std::string`, and JUCE `String` in the variables panel
- [ ] Evaluate member access expressions (e.g., this->sampleRate) in the REPL panel
- [ ] See `DBG()` and `std::cout`/`std::cerr` output in the nvim-dap console panel in real time
- [ ] Set conditional breakpoints and logpoints
- [ ] Inspect multiple threads simultaneously (audio thread + message thread)
- [ ] Set hardware watchpoints on memory addresses
- [ ] Step in, step over, step out reliably across JUCE/STL call boundaries
- [ ] Swap `codelldb` → `whatdbg` in `adapters.lua` with no other config changes

The system:
- [ ] Never hangs nvim-dap on misconfiguration — always fails with a clear message
- [ ] Warns on missing/mismatched PDB without aborting the session
- [ ] Sends a clean `terminated` event if the adapter crashes
- [ ] Produces a single self-contained `whatdbg.exe` with no side dependencies
- [ ] Passes Mason registry checksum verification

---

*SPEC v1.1 — WHATDBG (Windows Host Attachable Tool — DeFuGging)*
