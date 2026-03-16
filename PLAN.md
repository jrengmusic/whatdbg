# WHATDBG Build Plan v1.1

Reference: SPEC.md v1.1  
Build system: CMake + Ninja + MSVC  
Target: Windows 10 22H2+ x64

---

## Phase Overview

```
Phase 0 → Scaffold
Phase 1 → DAP Wire Protocol
Phase 2 → dbgeng Bootstrap
Phase 2.5 → Attach Smoke Test (validate against real DAWs before building breakpoints)
Phase 3 → Launch + Attach + Breakpoints + Deferred Resolution  ← first usable milestone
Phase 4 → Execution Control + Stack Frames + Threads
Phase 5 → Output Capture (moved up — high value, lower complexity)
Phase 6a → Variables (basic: primitives, structs, pointers, raw member enumeration)
Phase 6b → Variables (STL/JUCE type-aware formatting)
Phase 7 → Expression Evaluation + Remaining Features
Phase 8 → Mason + CI/CD
```

Phases 0–3 = minimal viable debugger (hit a breakpoint, that's it).  
Phases 4–6 = daily driver quality.  
Phases 7–8 = ship it.

---

## Phase 0: Project Scaffold

**Goal:** CMake builds a `whatdbg.exe` that compiles clean and links `dbgeng.lib`.

### Tasks

- [ ] `CMakeLists.txt` — C++17, MSVC, x64, `/W4`, link `dbgeng.lib` + `ole32.lib` + `oleaut32.lib`
- [ ] `src/main.cpp` — entry point, prints `WHATDBG v0.1.0` to stderr, exits 0
- [ ] `third_party/nlohmann/json.hpp` — vendored single header
- [ ] `.github/workflows/build.yml` — CI build on push (no release yet)
- [ ] `README.md` — one paragraph: what it is, build instructions
- [ ] Verify: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug .. && ninja` produces `whatdbg.exe`
- [ ] Verify: `dbgeng.dll` resolves at runtime (implicit load via COM, no explicit `LoadLibrary`)

**Deliverable:** Compiling, linking, running stub binary.

---

## Phase 1: DAP Wire Protocol

**Goal:** WHATDBG reads DAP requests from stdin and writes DAP responses to stdout correctly. No debug engine yet.

### Files
- `src/DapProtocol.hpp/cpp`
- `src/DapTypes.hpp`
- `src/DapDispatcher.hpp/cpp` (stub routing only)

### Tasks

- [ ] `DapProtocol` — `readMessage(std::istream&) → nlohmann::json` — parse `Content-Length` header, read exact N bytes of JSON body
- [ ] `DapProtocol` — `writeMessage(std::ostream&, nlohmann::json)` — write `Content-Length` header + JSON body, flush
- [ ] `DapTypes.hpp` — structs for all request/response/event types from SPEC (use nlohmann `from_json`/`to_json`)
- [ ] `DapDispatcher` — `dispatch(json request) → json response` stub — routes by `command` field, returns `ErrorResponse` for unimplemented
- [ ] `main.cpp` — IO loop: read message → dispatch → write response, repeat until stdin closes
- [ ] Stdout is binary-mode (`_setmode(_fileno(stdout), _O_BINARY)`) — critical on Windows
- [ ] Stderr used exclusively for internal WHATDBG diagnostics (never mixed into stdout DAP stream)
- [ ] `initialize` request → return hardcoded capabilities response (no dbgeng yet)
- [ ] Unknown command → `ErrorResponse` with `"WHATDBG: unsupported command: {command}"`

### Verification
- [ ] Manual test: pipe a raw `initialize` JSON request, verify correct `Content-Length` framed response
- [ ] nvim-dap connects, sends `initialize`, receives capabilities, session starts (then fails on `launch` — expected at this phase)

**Deliverable:** DAP framing works. nvim-dap can shake hands with WHATDBG.

---

## Phase 2: dbgeng Bootstrap

**Goal:** dbgeng COM interfaces initialized, event callbacks wired, threading model in place.

### Files
- `src/DbgEngSession.hpp/cpp`
- `src/DbgEngCallbacks.hpp/cpp`

### Tasks

- [ ] `DbgEngSession` — `CoInitializeEx` + `DebugCreate(__uuidof(IDebugClient5), ...)` on main thread
- [ ] `DbgEngSession` — acquire `IDebugControl4`, `IDebugSymbols3`, `IDebugRegisters2`, `IDebugDataSpaces4`, `IDebugSystemObjects4` from `IDebugClient5`
- [ ] `DbgEngCallbacks` — implement `IDebugEventCallbacks` — `GetInterestMask`, `Breakpoint`, `Exception`, `CreateThread`, `ExitThread`, `CreateProcess`, `ExitProcess`, `LoadModule`, `UnloadModule`
- [ ] `DbgEngCallbacks` — implement `IDebugOutputCallbacks2` — `Output2` routes to DAP `output` events by mask
- [ ] `DbgEngSession` — register both callback objects via `SetEventCallbacks` + `SetOutputCallbacks`
- [ ] Threading: IO thread owns stdin reader + command queue (`std::queue` + `std::mutex` + `std::condition_variable`)
- [ ] Main thread: event loop — dequeue ALL pending commands first (COM calls happen here), THEN call `WaitForEvent(timeout=100ms)`. WaitForEvent is NOT re-entrant — no COM calls during wait.
- [ ] Event loop states: `Idle` (no target), `Running` (target executing, WaitForEvent polling), `Stopped` (target broken in, ready for commands). State transitions must be explicit and logged.
- [ ] Pause handling: when target is running and `pause` request arrives, IO thread enqueues it. Main thread dequeues after WaitForEvent timeout (S_FALSE), calls `SetInterrupt(DEBUG_INTERRUPT_ACTIVE)`. Maximum pause latency = timeout value (100ms).
- [ ] Thread-safe DAP response writer — `std::mutex`-guarded stdout writes
- [ ] `DbgEngSession::shutdown()` — `IDebugClient::EndSession(DEBUG_END_ACTIVE_DETACH)` + COM release order

### Verification
- [ ] Session initializes without crashing
- [ ] `WaitForEvent` loop runs without spinning at 100% CPU when idle
- [ ] Clean shutdown on `disconnect` request

**Deliverable:** dbgeng initialized, threading model running, ready to receive debug targets.

---

## Phase 2.5: Attach Smoke Test

**Goal:** Validate that `AttachProcess` works against real DAWs with JUCE plugins loaded. This is the single highest-risk validation point — if this fails, everything else is moot.

### Tasks

- [ ] Manual test: attach to REAPER (x64) with a JUCE VST3 plugin loaded
- [ ] Manual test: verify `LoadModule` callback fires for the plugin DLL
- [ ] Manual test: verify `IDebugSymbols3::GetOffsetByLine` resolves a line in the plugin source
- [ ] Document any DAW-specific issues (anti-debug, anti-tamper, UAC)
- [ ] Test: already-debugged process → verify clean error message

### Verification
- [ ] Attach succeeds, module list includes plugin DLL, symbol resolution works
- [ ] If any DAW blocks attach, document the limitation

**Deliverable:** Confidence that the core attach use case works before investing in breakpoint infrastructure.

---

## Phase 3: Launch + Attach + Breakpoints

**Goal:** Hit a breakpoint in a JUCE standalone `.exe` and in a plugin loaded in a DAW. First real milestone.

### Files
- `src/BreakpointManager.hpp/cpp`
- Extend `DbgEngSession`, `DapDispatcher`

### Tasks

**Launch:**
- [ ] `launch` handler — validate `program` field (hard fail per SPEC)
- [ ] `IDebugClient5::CreateProcessAndAttach2` with `DEBUG_ATTACH_DEFAULT`
- [ ] `initialized` event sent after `CreateProcess` callback fires
- [ ] `configurationDone` handler — `IDebugControl::SetExecutionStatus(DEBUG_STATUS_GO)`
- [ ] `stopOnEntry` — if true, do not `GO` after `configurationDone`, emit `stopped` with reason `"entry"`

**Attach:**
- [ ] `attach` handler — validate `pid` + `program` fields (hard fail per SPEC)
- [ ] `IDebugClient5::AttachProcess(0, pid, DEBUG_ATTACH_DEFAULT)`
- [ ] `IDebugSymbols3::AddSyntheticModule` or `AppendImagePath` for `program` path symbol hint
- [ ] Access denied → hard fail with `SeDebugPrivilege` hint message
- [ ] `initialized` event after attach completes

**Breakpoints:**
- [ ] `BreakpointManager` — `std::unordered_map<uint32_t, IDebugBreakpoint2*>` (DAP id → COM object)
- [ ] `setBreakpoints` handler — clear existing BPs for source file, resolve each line via `IDebugSymbols3::GetOffsetByLine`, call `IDebugControl4::AddBreakpoint`
- [ ] Verified breakpoints → `verified: true` + resolved line
- [ ] Unresolved → `verified: false` + `"WHATDBG: could not resolve breakpoint at {file}:{line}"`
- [ ] `IDebugEventCallbacks::Breakpoint` → emit DAP `stopped` event, reason `"breakpoint"`, `threadId` from `IDebugSystemObjects::GetCurrentThreadId`
- [ ] PDB warn-and-continue on module load (from `LoadModule` callback)
- [ ] PDB mismatch detection via `IDebugSymbols3::GetModuleVersionInformation`

**Deferred Breakpoint Resolution:**
- [ ] `BreakpointManager` maintains a pending list: `std::vector<PendingBreakpoint>` for unresolved breakpoints
- [ ] `LoadModule` callback → scan pending breakpoints, attempt `GetOffsetByLine` against new module
- [ ] Successfully resolved → create `IDebugBreakpoint2`, emit DAP `breakpoint` event with `reason: "changed"`, `verified: true`
- [ ] `process` event emitted after launch/attach with process name and PID

**Disconnect:**
- [ ] `disconnect` handler — `IDebugClient::EndSession`, emit `terminated` + `exited` events, exit process

### Verification
- [ ] `:MasonInstall` (local path) → nvim-dap launches `whatdbg.exe`
- [ ] `Launch Standalone` config → JUCE standalone `.exe` launches, breakpoint hits, nvim cursor moves to line
- [ ] `Attach to DAW` config → attach to REAPER with VST3 loaded, breakpoint in plugin code hits

**Deliverable:** ✅ First real working debugger. Replaces GDB for standalone, fills the gap for plugin attach.

---

## Phase 4: Execution Control + Stack Frames + Threads

**Goal:** Step through code, inspect call stack, see all threads.

### Tasks

**Execution control:**
- [ ] `continue` → `DEBUG_STATUS_GO`, emit `continued` event
- [ ] `pause` → `DEBUG_STATUS_BREAK`, emit `stopped` reason `"pause"`
- [ ] `next` → `DEBUG_STATUS_STEP_OVER` on current thread
- [ ] `stepIn` → `DEBUG_STATUS_STEP_INTO`
- [ ] `stepOut` → `DEBUG_STATUS_STEP_OUT`
- [ ] All step commands: set current thread via `IDebugSystemObjects::SetCurrentThreadId` from `threadId` field
- [ ] Step complete → `IDebugEventCallbacks` fires `StepComplete` → emit `stopped` reason `"step"`

**Stack frames:**
- [ ] `stackTrace` handler — `IDebugControl4::GetStackTrace` for specified thread
- [ ] Each frame: `IDebugSymbols3::GetLineByOffset` for source + line, `IDebugSymbols3::GetNameByOffset` for function name
- [ ] Frames without PDB → name only, no source location, `presentationHint: "subtle"`
- [ ] `startFrame` + `levels` pagination
- [ ] Inlined frames via `IDebugSymbols3::GetScopeSymbolGroup` — `presentationHint: "inline"`

**Threads:**
- [ ] `threads` handler — `IDebugSystemObjects4::GetNumberThreads` + `GetThreadIdsByIndex`
- [ ] Thread name via TEB (`NtQueryInformationThread` or `GetThreadDescription` on Win10 1607+)
- [ ] `thread` events emitted from `CreateThread`/`ExitThread` callbacks
- [ ] `exited` event with exit code from `ExitProcess` callback

### Verification
- [ ] Step in/over/out works across JUCE + STL call boundaries
- [ ] Call stack shows correct source lines
- [ ] Thread panel shows JUCE audio thread + message thread by name

---

## Phase 5: Output Capture

**Goal:** `DBG()` and `OutputDebugString` appear in nvim-dap console panel in real time. This is moved up from Phase 6 because it has high daily-driver value and lower complexity than variable inspection.

### Tasks

- [ ] `IDebugOutputCallbacks2::Output2` — route by `Mask`:
  - `DEBUG_OUTPUT_DEBUGGEE` → category `console` (OutputDebugString / DBG())
  - `DEBUG_OUTPUT_NORMAL` → category `console` (engine output)
  - `DEBUG_OUTPUT_ERROR` → category `stderr` (engine errors)
- [ ] In attach mode: all `DEBUG_OUTPUT_DEBUGGEE` is `OutputDebugString` — route to `console`
- [ ] In launch mode (console apps only): stdout/stderr pipe capture via separate reader threads → `stdout`/`stderr` categories
- [ ] GUI app stdout/stderr: document as unsupported (no handles exist). `OutputDebugString`/`DBG()` is the universal mechanism.
- [ ] All output forwarded in real time (no buffering)
- [ ] WHATDBG internal messages → category `telemetry` only

### Verification
- [ ] `DBG("hello from plugin")` appears in nvim-dap console panel while attached to DAW
- [ ] `OutputDebugString("test")` appears in console panel
- [ ] No output lost between breakpoints

---

## Phase 6a: Variables — Basic Inspection

**Goal:** Inspect locals, arguments, registers. Raw member enumeration for structs/classes. No STL/JUCE type awareness yet.

### Files
- `src/VariableResolver.hpp/cpp`

### Tasks

**Scopes:**
- [ ] `scopes` handler — return 3 scopes: `Locals` (id=1000+frameId), `Arguments` (id=2000+frameId), `Registers` (id=3000+frameId)
- [ ] `variablesReference` encoding: `(frameId << 20) | (scopeType << 16) | symbolIndex`

**Variables:**
- [ ] `VariableResolver` — `IDebugSymbolGroup2::GetScopeSymbolGroup` for locals/args
- [ ] Primitive types — `IDebugSymbols3::GetSymbolTypeId` + `IDebugDataSpaces4::ReadVirtual`
- [ ] Raw pointer — address string + dereferenced value as child
- [ ] Struct/class — member enumeration via `IDebugSymbolGroup2::ExpandSymbol`, lazy expand
- [ ] Arrays — display indexed children up to 256 elements
- [ ] Registers scope — `IDebugRegisters2::GetValues` for x64 GP registers
- [ ] Symbol group cache management: release old groups on frame/thread switch

### Verification
- [ ] Locals panel shows correct values for int, float, bool, char, raw pointers
- [ ] Struct members expand correctly
- [ ] Registers visible

---

## Phase 6b: Variables — STL/JUCE Type-Aware Formatting

**Goal:** Display STL and JUCE types with human-readable formatting instead of raw internal members.

### Tasks

- [ ] Use `IDebugSymbols3::GetTypeId` + `IDebugSymbols3::GetFieldOffset` for version-proof member resolution (NOT hardcoded paths)
- [ ] `std::string` — detect SSO vs heap, display string content
- [ ] `std::vector<T>` — resolve size from `_Mylast - _Myfirst`, expose indexed children lazily
- [ ] `std::array<T,N>` — indexed children
- [ ] `std::map` / `std::unordered_map` — tree/bucket walk, key-value pairs as children
- [ ] `std::optional` — `_Has_value` bool + `_Value` member
- [ ] `std::unique_ptr` / `std::shared_ptr` — dereference pointer member
- [ ] `juce::String` — resolve internal `StringHolder` → UTF-8 display
- [ ] `juce::Array<T>` — resolve `data.elements`, size from `numUsed`

### Verification
- [ ] `std::string` shows string content, not internal struct
- [ ] `std::vector<int>` shows size + indexed children
- [ ] JUCE `String` shows UTF-8 content

---

## Phase 7: Expression Evaluation + Remaining Features

**Expression Evaluation:**
- [ ] `evaluate` handler — `IDebugControl4::Evaluate` in frame context
- [ ] Set frame context via `IDebugSymbols3::SetScopeFrameByIndex`
- [ ] Supports: variable names, member access, pointer dereference, array indexing, arithmetic, casts
- [ ] Does NOT support function calls (dbgeng limitation — document clearly)
- [ ] Structured result → `variablesReference` pointing into `VariableResolver` cache
- [ ] Error → `result: "<error: {message}>"`, `variablesReference: 0`

**Conditional Breakpoints + Logpoints:**
- [ ] `condition` field on breakpoint → `IDebugBreakpoint2::SetCommandWide` with condition expression
- [ ] Invalid expression → unverified breakpoint + output warn
- [ ] `logMessage` field → on hit, evaluate `{}` expressions, emit `output` event, auto-continue

**Exception Breakpoints:**
- [ ] `setExceptionBreakpoints` — `cpp_throw` → first-chance, `cpp_unhandled` → second-chance
- [ ] `stopped` event reason `"exception"` with type + message

**Hardware Watchpoints:**
- [ ] `dataBreakpointInfo` + `setDataBreakpoints` → `IDebugBreakpoint2` with `BREAKPOINT_DATA`
- [ ] Max 4 enforcement

**Post-MVP Features (deferred):**
- [ ] `setVariable` — variable modification via `WriteVirtual`
- [ ] `source` — source content retrieval fallback
- [ ] `completions` — REPL autocomplete

**Crash Safety:**
- [ ] `std::set_terminate` + `SetUnhandledExceptionFilter` → write `terminated` + `exited` events before exit

---

## Phase 8: Mason + CI/CD

**Goal:** `:MasonInstall whatdbg` works for any Windows nvim user.

### Tasks

**GitHub Actions release pipeline:**
- [ ] `.github/workflows/release.yml` — trigger on `v*` tag push
- [ ] Build step: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release` + `ninja`
- [ ] SHA256 checksum of `whatdbg.exe` → `whatdbg.exe.sha256`
- [ ] Upload both as GitHub release assets

**Mason registry manifest:**
- [ ] Fork `mason-org/mason-registry`
- [ ] `packages/whatdbg/package.yaml`:
  ```yaml
  name: whatdbg
  description: Windows Host Attachable Tool — DeFuGging. DAP adapter for MSVC C++17 debugging in Neovim.
  homepage: https://github.com/jrengmusic/whatdbg
  licenses:
    - MIT
  languages:
    - C++
  categories:
    - DAP
  source:
    id: pkg:github/jrengmusic/whatdbg@v1.0.0
    asset:
      - target: win_x64
        file: whatdbg.exe
        bin: whatdbg.exe
  ```
- [ ] PR to mason-registry

**Verification:**
- [ ] Fresh Windows nvim install, `:MasonInstall whatdbg` succeeds
- [ ] Binary at correct path, checksum verified
- [ ] `adapters.lua` with `whatdbg` adapter → full debug session works

---

## Milestone Summary

| Milestone | Phase | What works |
|---|---|---|
| **M0** | 0–1 | Compiles, DAP handshake |
| **M1** | 2–2.5 | dbgeng initialized, attach validated against real DAWs |
| **M2** | 3 | Hit a breakpoint ← *unblock your workflow* |
| **M3** | 4 | Step, stack frames, threads |
| **M4** | 5 | Output capture (DBG, OutputDebugString) |
| **M5** | 6a | Basic variable inspection (primitives, structs, registers) |
| **M6** | 6b | STL/JUCE type-aware variable display |
| **M7** | 7 | Expressions, exceptions, watchpoints, conditional breakpoints |
| **M8** | 8 | Mason installable, shipped |

---

*PLAN v1.1 — WHATDBG*
