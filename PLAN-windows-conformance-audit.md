# PLAN: Windows Conformance — Audit Resolution

**RFC:** `RFC.md` (TUI/CLI Debuggee Terminal Attachment) — included by ARCHITECT: *"Include"*; mechanism ruled: *"B on both platforms"*
**Date:** 2026-11-14
**BLESSED Compliance:** verified
**Language Constraints:** C++17 / JUCE 8.0.17 / JAM (jam_core, jam_terminal); Lua (nvim headless smoke harness)
**File at lock:** this plan is copied to `D:\Poems\dev\whatdbg\PLAN-windows-conformance-audit.md` (Step 0)

## Overview
The Windows-conformance sprint reached 21/21 on `tests/smoke` and an epoch of 29.06 s (old binary 77.4 s), but the wholesale audit returned NEEDS_WORK (63 findings). This plan resolves every validated finding (CAROL DCF §5), moves the overlapped pipe reader into JAM as `jam::AsyncOutputStream`, renames `jam::ComApartment`, and implements RFC.md Option B (`runInTerminal`) on both platforms.

## Context — ARCHITECT rulings this plan executes (verbatim)
| # | Ruling | Applies to |
|---|---|---|
| R1 | *"Defer the request"* (break-in for `setBreakpoints` while running) | Step 7 |
| R2 | *"Move into JAM"* (overlapped pipe reader) | Steps 2-3 |
| R3 | *"why create new semantics again? isnt this AsyncOutputStream?"* | Step 2 name |
| R4 | *"ComApartment need better names adherence to @carol/NAMES.md … meaning updating call site fold into the plan"* | Step 1 |
| R5 | Wake race 2.4 → *"@MANIFESTO.md"*: MANIFESTO S "Orchestrator tells, never tracks"; violation signature "A getter called by a caller to make a decision" | Step 8 |
| R6 | RFC: *"Include"*, *"B on both platforms"* | Steps 14-17 |
| R7 | Earlier this session: *"Silent break-in"*, *"B2 attach-suspended"*, *"Adapter-side fix"* (13), `debug::OutputStream` (superseded by R2/R3) | D-rows, Step 11 |
| R8 | *"FULLY EVENT DRIVEN, no wait, no lock at hot path, ISOMORPHIC to mac"* | Steps 7, 8, 16 |

## Language / Framework Constraints
- CODING.md TEMPLATE/TU placement: one object, one TU; JAM submodule `.h`/`.cpp` carry zero includes except platform headers (jam_TerminalTTY_windows.h:90 precedent).
- JAM platform files: `#if JUCE_WINDOWS` guard inside the header, `.cpp` included under `#if JUCE_WINDOWS` in the module unity file (jam_terminal.cpp:32-33; jam_core.cpp:8).
- CAROL Code Hygiene: no comments/doxygen until the post-audit pass (Step 20).
- Builds only through cast; profiling only through scout.

## Dependency & API Inventory
- **JUCE:** `juce::File` converts `/`→`\` on Windows (`juce_File.cpp:149` `p.replaceCharacter ('/', '\\')`); `juce::ComSmartPtr` enabled by `project-info.md` macro `juceCoreIncludeComSmartPtr`; `juce::ScopedJuceInitialiser_GUI` (`juce_Initialisation.h:81`) is the naming family for scoped library initialisation.
- **JAM:** `jam::AsyncUpdater::Context` = `PTP_POOL` on Windows / `dispatch_queue_t` on macOS (`jam_AsyncUpdater.h:67,72`), `getContext()` public (`:76`); `jam::TerminalWindowsTTY` overlapped read (`jam_TerminalTTY_windows.cpp:686-701, 812-880`); `jam::debug::Log::Scope` RAII family (`jam_Log.h:33`); module unity includes (`jam_core.h:122`, `jam_core.cpp:8`).
- **dbgeng:** `CreateProcess2`/`AttachProcess` flags `DEBUG_ATTACH_INVASIVE_RESUME_PROCESS`, `DEBUG_ATTACH_INVASIVE_NO_INITIAL_BREAK` (`DbgEng.h:1867-1871`); `GetContextStackTrace` (`:12735`); `SetScope` (`:21770`); session "inaccessible while the targets are executing" (learn.microsoft.com debugging-session-and-execution-model).
- **DAP / lldb-dap / nvim-dap (runInTerminal):** request `kind/cwd/args/env` (spec 882-922); client capability `supportsRunInTerminalRequest` (spec 1071-1074; nvim-dap `session.lua:1957` sends `true`); lldb-dap launcher `--comm-file <path> --launch-target <program> args…` (`JSONUtils.cpp:379-409`), pid message `{"kind":"pid","pid":N}`, `{"kind":"didAttach"}`, `{"kind":"error","error":…}` (`RunInTerminal.cpp:44-60`); Windows launcher `CreateProcessW (CREATE_SUSPENDED)` → NotifyPid → wait didAttach → `ResumeThread` → wait exit (`lldb-dap.cpp:395-504`); POSIX launcher NotifyPid(getpid) → wait didAttach → `execvp` (`:524-539`); Windows comm channel named pipe `\\.\pipe\…` (`RunInTerminal.cpp:170-176`, `FifoFiles.cpp:97-112`); lldb-dap ignores the response body and takes the pid from the comm channel (`RequestHandler.cpp:126-131`; nvim-dap's `processId` is the terminal job pid, `session.lua:299-312`, `:151`).
- **Established whatdbg patterns:** `cancellations` table in `Processor::addRequest` (Processor.cpp:34-35, 60-61) — family for a per-command table; lanes `requests`/`messages` (`dap::FIFO`, Processor.h:146-152); `std::function` member set by owner (`dap::Reader::onMessage`, Reader.h:37); stop drains in `StopManager` (StopManager.cpp:19-25), `drainModuleLoaded` resumes silently (:146-156); `Id::` rows in `cast/identifiers.md`.

## Names (locked with this plan; each from an established family unless marked)
| Name | Family / source |
|---|---|
| `jam::AsyncOutputStream` | R3 (ARCHITECT) |
| `jam::ScopedComInitialiser` | `juce::ScopedJuceInitialiser_GUI` (juce_Initialisation.h:81); R4 |
| `pendingRequests` (lane) | `requests` / `messages` lanes (Processor.h:148,152) |
| `deferrals` (table) | `cancellations` (Processor.h:156), NAMES Rule 8 noun |
| `onBreakIn` (StopManager member) | `onMessage` (Reader.h:37), `onBytesReady` (jam_TerminalTTY.h:60) |
| `drainOutput` (Session public) | `drain*` family (StopManager drains) |
| `setScope` (Session private) | NAMES Verb Contract `set` |
| `getContextStackTrace` (Session.cpp static) | dbgeng method name |
| `Id::runInTerminal`, `kind`, `integrated`, `external`, `integratedTerminal`, `externalTerminal`, `processId`, `supportsRunInTerminalRequest`, `didAttach`, `pid` | DAP / lldb-dap protocol words (same rule as Decision 4: DAP words become `Id::` rows) |
| `--comm-file`, `--launch-target` | lldb-dap launcher arguments (JSONUtils.cpp:381,405) |
| `Launcher` (whatdbg class) | lldb-dap `RunInTerminalLauncherCommChannel` (RunInTerminal.cpp:104) — **new domain term, ARCHITECT approves with this plan** |
| `isRunInTerminalSupported` (Model row) | `is*` row family (`isPausePending`, WHATDBGModel.cpp:22) |

## Validation Gate
Each step is validated by COUNSELOR before the next — against MANIFESTO.md (BLESSED), NAMES.md, ~/.carol/CODING.md, and this locked PLAN (no deviation, no scope drift). COUNSELOR reads every changed file at the reported lines (Design by Contract Filter). @Auditor runs ONCE, after Step 19. Builds: cast only; compile checks batch at Steps 6, 13, 18.

## Steps

### Step 0: Lock the plan on disk
**Scope:** `PLAN-windows-conformance-audit.md` (new, copy of this file); `PLAN-audio-architecture-discipline-0-tracker.md` Status table.
**Action:** COUNSELOR copies this plan to the project root and adds a Status row pointing at it.
**Validation:** File exists, identical content; tracker row present.

### Step 1: Rename `jam::ComApartment` → `jam::ScopedComInitialiser` (R4)
**Scope:** `jam/jam_core/native/jam_ComApartment_windows.h/.cpp` → `jam_ScopedComInitialiser_windows.h/.cpp` (delete old, create new); `jam_core.h:122`; `jam_core.cpp:8`; `jam/user-modules-info.md:264,269`; `jam/jam_terminal/image/native/jam_TerminalImageSequence_windows.cpp:18`; `end/Source/plugins/Nexus.cpp:72`.
**Action:** Same behaviour (`CoInitializeEx (nullptr, COINIT_MULTITHREADED)` / `CoUninitialize` when it succeeded). `result` becomes a private member (no reader exists: search of D:\Poems\dev). `class`, non-copyable (`JUCE_DECLARE_NON_COPYABLE`). Every call site updated.
**Validation:** No `ComApartment` token remains under D:\Poems\dev outside docs/xml and SPRINT-LOGs; files follow the `#if JUCE_WINDOWS` header guard of jam_ComApartment_windows.h:3. END compile: ARCHITECT's machine (END is not built by this sprint).

### Step 2: `jam::AsyncOutputStream` (R2, R3) — resolves audit 3.1-3.4, 3.6, 2.12, 2.13
**Scope:** `jam/jam_core/native/jam_AsyncOutputStream_windows.h/.cpp`, `jam/jam_core/native/jam_AsyncOutputStream_mac.cpp` (POSIX/GCD body, header shared), `jam_core.h`, `jam_core.cpp`, `user-modules-info.md`.
**Action:** One class, platform bodies:
- Windows: owns one overlapped pipe handle (taken by `open`), its read `OVERLAPPED`, manual-reset event, thread-pool wait on the caller's `jam::AsyncUpdater::Context` (`PTP_POOL`; `nullptr` = default pool), and the read buffer. Handle/event/wait owned by `std::unique_ptr` deleters (no reset in a destructor — audit 3.2). Close order: unarm wait, `CancelIoEx` + `GetOverlappedResult (…, TRUE)` for a pending read, wait callbacks, then handles (audit 3.3).
- macOS: owns one file descriptor read with a `dispatch_source` (`DISPATCH_SOURCE_TYPE_READ`) on the caller's `dispatch_queue_t` Context.
- API: `bool open (native handle, Context, int bufferSize)`; `std::function<void (int)> onBytesReady` (numBytes, as jam_TerminalTTY.h:60); `std::optional<std::string_view> read ()` — a view into the owner buffer of the completed read (no `memcpy`, no out-parameter, no `-1` sentinel: `std::nullopt` = stream ended; empty view = nothing ready). `jassert (onBytesReady != nullptr)` before arming (jam_TerminalTTY_windows.cpp:686 precedent). No getter of the raw handle (audit 3.6).
**Validation:** CODING one object/one TU; zero includes in submodule files except platform headers; no `readPending` shadow of OS state where `HasOverlappedIoCompleted` answers (audit 3.4); read path ≤30 lines per function.

### Step 3: `jam::TerminalWindowsTTY` reads through `jam::AsyncOutputStream`
**Scope:** `jam/jam_terminal/tty/native/jam_TerminalTTY_windows.h/.cpp`.
**Action:** Delete the TTY's own read members (`readOverlapped`, `readEvent`, `readWait`, `readBuffer`, `readBufferBytes`, `readBufferOffset`, `readPending`, `consumeReadBuffer`) first. TTY gives the reader a `DuplicateHandle` of its duplex pipe (write keeps the original). The reader's `onBytesReady` lambda keeps the TTY clock sequence (`clock.process (n)`, `onBytesReady (n)`, `clock.wait ()`, jam_TerminalTTY_windows.cpp:697-700). `TerminalWindowsTTY::read (buf, maxBytes)` (TerminalTTY interface, jam_TerminalTTY.h:125) copies from the view into the caller buffer — the interface's own contract.
**Validation:** Behaviour of `read` return values unchanged (>0 bytes, 0 none, -1 end — interface contract jam_TerminalTTY.h:122-123); `close()` sequence keeps steps 1-6 order of jam_TerminalTTY_windows.h:72-77 with the reader's close at step 5.

### Step 4: whatdbg Session uses `jam::ScopedComInitialiser` and `juce::ComSmartPtr` (audit 9.1)
**Scope:** `Source/debug/Session.h`, `Session.cpp`, `Callbacks.h`, `OutputStream.h` (deleted in Step 5).
**Action:** Delete `isComOwned`, the `CoInitializeEx`/`CoUninitialize` code (Session.cpp:38-43, 398-402); Session holds a `jam::ScopedComInitialiser` member declared before `client`. Replace `Microsoft::WRL::ComPtr` with `juce::ComSmartPtr` (framework API fully; macro `juceCoreIncludeComSmartPtr` already on). Remove the direct `#include <windows.h>` / `<wrl/client.h>` where `juceCoreIncludeNativeHeaders` provides them (CODING "No Redundant TU Includes").
**Validation:** No `CoInitializeEx`, `isComOwned`, `Microsoft::WRL` left in Source/; teardown order: callbacks unset → interfaces released → COM uninitialised (member order).

### Step 5: whatdbg stdout/stderr through `jam::AsyncOutputStream` (delete `debug::OutputStream`)
**Scope:** delete `Source/debug/OutputStream.h/.cpp`; `Session.h/.cpp`; `Source/debug/State.h` (Events alias); `Processor.cpp:131-135`; `Session_mac.cpp:67-88`.
**Action:** Session owns two `jam::AsyncOutputStream` (stdout, stderr) on Processor's Context; `onBytesReady` → `wake ()`. `Events` gains `void (std::string_view)`; `Id::standardOutput` / `Id::standardError` fire with the view (macOS `drainProcessStdio` also passes a view of its buffer). `Processor` handler builds the output event from the view (one copy: the message creation). `drainOutput` becomes public on both platforms (macOS: `drainProcessStdio` for both streams).
**Validation:** Copy count from pipe to JSON = OS-boundary read + message creation (MANIFESTO E permitted copies 1 and creation); no stack staging buffer.

### Step 6: Launch path corrections (audit 2.1, 2.2, 2.3, 2.14, 2.15) + compile check 1
**Scope:** `Session.cpp` `createSuspendedProcess`, `launch`.
**Action:**
- `createSuspendedProcess` returns the owned process handle (`std::unique_ptr<void, decltype (&CloseHandle)>`; empty on failure; pid via `GetProcessId`). When `AttachProcess` fails, `launch` calls `TerminateProcess` on it (audit 2.1).
- `internalConsole` always takes the captured path; `jassert` both streams open; no fallback to `CreateProcess2` (audit 2.2).
- `juce::Identifier { console } == Id::internalConsole` (sibling Processor.cpp:674; audit 2.3).
- `std::array` + `.at()` for the handle list; `jassert` on `InitializeProcThreadAttributeList`/`UpdateProcThreadAttribute`; `commandLineBuffer` only on the `CreateProcess2` branch; log text names the call made (audit 2.14).
- Then COUNSELOR runs `cast cast/spell.md --debug` (compiles whatdbg + jam_core + jam_terminal).
**Validation:** Compiler output clean; `tests/smoke` 07 passes.

### Step 7: Deferred break-in (R1, R8) — resolves audit 1.1-1.5
**Scope:** `Source/dap/FIFO.h`, `Processor.h/.cpp`, `StopManager.h/.cpp`, `Session.cpp` (`interrupt`), `State.h`.
**Action:**
- Delete `Processor::breakIn` and the `addMessages (stopManager.onStop (…))` at Processor.cpp:464 first.
- `FIFO::applyPendingData` hands each slot as `MessageType&&` (the slot is cleared after the call today, FIFO.h:65-66 — a move replaces the copy).
- `Processor`: `deferrals` table (command → true), filled under `#if JUCE_WINDOWS` with `Id::setBreakpoints` (precedent Processor.cpp:329-341); lane `pendingRequests` (engine context only). In `process()`, a request whose command is in `deferrals` while `executionState == running` moves into `pendingRequests`; the first such request stores `isBreakInPending 1` and calls `session.interrupt (pid)`.
- `Id::breakIn` handler stores `executionState stopped` (audit 1.4) and `isBreakInPending 0`.
- `StopManager::onBreakIn` (`std::function<void ()>`, set by Processor to apply `pendingRequests` through `onCommand`); `drainBreakIn`: `stopReason none`, `onBreakIn ()`, `resumeExecution ()`.
- `drainProcessExited` also applies `pendingRequests` (no request stays unanswered).
- `Session::interrupt`: `jassert` on `OpenProcess` and `DebugBreakProcess` results (audit 1.1 silent fail).
- `StopReason::breakIn` moves to the end of the enum (audit 5.2).
**Validation:** No wait on the engine context in any request body; `disconnect` while a break-in is pending answers at once; MANIFESTO S: the deferral state lives in the Model row `isBreakInPending` and the lane.

### Step 8: Wake always (R5) — resolves audit 2.4
**Scope:** `Session.cpp` `wake`; tracker Decision 5.
**Action:** `wake` calls `SetInterrupt (DEBUG_INTERRUPT_EXIT)` on every request; delete the Model read. `pollEvents` keeps `GetInterrupt` after each wait, so a wake while stopped costs one `E_PENDING` return. Tracker Decision 5 Windows bullet rewritten with R5 quoted.
**Validation:** `Session::wake` reads no Model parameter; 04 and 16 pass.

### Step 9: `pollEvents` / `onExitProcess` (audit 4.1, 2.6 rejected)
**Scope:** `Callbacks.h/.cpp`, `Session.cpp`, `Processor.cpp` (`Id::exited` handler).
**Action:** Delete `EventCallbacks::onExitProcess`; Processor's `Id::exited` handler calls `session.drainOutput ()` before `drainProcessExited ()` (tell; both platforms). Audit 2.6 rejected: the save/restore names its threat — `SetInterrupt (DEBUG_INTERRUPT_EXIT)` during INFINITE `WaitForEvent` changes status GO→BREAK (run evidence, ode_14.log this session). Audit 2.5 rejected: `timeoutMs` is used (Session.cpp:348).
**Validation:** No `std::function` member in Callbacks.h; output lines precede `exited` in 07 transcript.

### Step 10: Threads not reported by dbgeng — test first (audit 2.10, R7 "Adapter-side fix")
**Scope:** `Callbacks.cpp` `GetInterestMask`; `Session.cpp` thread path.
**Action:** Probe: add `DEBUG_EVENT_CREATE_THREAD | DEBUG_EVENT_EXIT_THREAD` to the interest mask (Callbacks.cpp:258-262) and run 13 with a log of `GetNumberThreads` at the stop. If the worker is listed: delete the Toolhelp/context path and `unreportedThreadId`. If not: keep it and apply Step 11.
**Validation:** Decision follows the logged count, cited in the step report.

### Step 11: Session thread/scope corrections (audit 2.7, 2.8, 2.9, 2.11) — only if Step 10 keeps the path
**Scope:** `Session.cpp`, `Session.h`.
**Action:** One static `getContextStackTrace` used by stack and scope (2.8); private `setScope (int frameIndex)` called by `applyToLocals`, `applyToVariableChildren`, `evaluateExpression` — `getOrCreateSymbolGroup` sets nothing (2.7); `setCurrentThreadBySystemId` resets the symbol-group cache when the current thread changes (2.9); one thread-object appender and one `"Thread "` constant (2.11).
**Validation:** No duplicated block; `getOrCreate` has one side effect.

### Step 12: Breakpoint path and path conversion (audit 6.2-6.4)
**Scope:** `BreakpointManager.cpp/.h`, `Session.cpp`, `Processor.cpp:327-343`.
**Action:** `addBreakpoint` calls `setBreakpointLocation` (6.2). Native path form is Session's: `Session::addBreakpointByLocation`, `appendSourcePath`, `appendSymbolPath`, `normalizeCommandLine` take the raw path and use `juce::File (path).getFullPathName ()` (juce_File.cpp:149); delete `toWindowsPath` and the `#if` in BreakpointManager (6.3, 6.4) and in `Processor::addSearchPaths`.
**Validation:** One conversion site per direction; no `#if JUCE_WINDOWS` in BreakpointManager.cpp.

### Step 13: Processor tidy (audit 1.6, 1.2) + compile check 2 + suite
**Scope:** `Processor.cpp`.
**Action:** One private `bool isPolling () const` for the condition at Processor.cpp:84-86/95-97. Then `cast --debug`, `tests/smoke` (21 scenarios).
**Validation:** 21/21; transcripts equal `baseline/windows/` except scenarios whose sequence this plan changes (re-recorded in Step 18).

### Step 14: runInTerminal — vocabulary and initialize (R6)
**Scope:** `cast/identifiers.md`, `WHATDBGModel.cpp`, `Processor.cpp` `onInitialize`.
**Action:** Add the `Id::` rows of the Names table. Model row `isRunInTerminalSupported` stores the client's `supportsRunInTerminalRequest`.
**Validation:** Rows alphabetical; Model row follows the `is*` family.

### Step 15: runInTerminal — launcher mode of whatdbg
**Scope:** `Source/Main.cpp` (argument dispatch beside `--help`, Main.cpp:5 precedent), new `Source/Launcher.h/.cpp` (class `Launcher`, one object one TU; Main.cpp stays a dispatcher).
**Action:** `whatdbg --comm-file <path> --launch-target <program> [args…]`: Windows — open the named pipe, `CreateProcessW (CREATE_SUSPENDED)`, send `{"kind":"pid","pid":N}`, block for `{"kind":"didAttach"}` (separate launcher process, not the adapter), `ResumeThread`, wait for exit, return its code. macOS — open the FIFO, send own pid, block for `didAttach`, `execvp`. Errors send `{"kind":"error","error":…}`. Mirrors lldb-dap.cpp:395-539.
**Validation:** Launcher is a separate process: the adapter's no-wait rule (R8) holds; JSON through `jam::JsonWriter`/`jam::JsonDocument`.

### Step 16: runInTerminal — adapter side
**Scope:** `Processor.cpp/.h`, `Session.h/.cpp`, `Session_mac.cpp`, `Source/dap/Types.cpp/.h`.
**Action:** `launch` with `console` `integratedTerminal` or `externalTerminal` (and `isRunInTerminalSupported`): Session creates the comm channel (Windows named pipe / macOS `mkfifo`) read by a `jam::AsyncOutputStream`; Processor sends the reverse request (`kind` integrated/external, `cwd`, `args` = [whatdbg path, `--comm-file`, path, `--launch-target`, program, …]) on the `messages` lane. The launch response waits for the pid message (event-driven: `onBytesReady` → `wake` → engine drains): `session.attach (pid)`, send `didAttach`, then the launch response, `executionState launching`. The client's `runInTerminal` response (type `response`) is routed by type, logged on failure (lldb-dap `LogFailureResponseHandler` behaviour). Windows `CREATE_NEW_CONSOLE` path deleted. `internalConsole` keeps Step 6.
**Validation:** No wait in the adapter; same flow on both platforms (R8); client without the capability → launch error response naming `internalConsole`.

### Step 17: runInTerminal — smoke scenario
**Scope:** `tests/smoke/dap_client.lua`, new `scenario_terminal.lua`, `run_smoke.lua`.
**Action:** `dap_client` answers a `runInTerminal` reverse request by spawning `args` headless (`vim.system`, cwd/env) and replying `processId`; advertises `supportsRunInTerminalRequest`. Scenario `22_run_in_terminal`: launch `integratedTerminal`, breakpoint at `SMOKE_BREAKPOINT_LINE` stops, continue, `exited`.
**Validation:** 22 passes on Windows; macOS baseline recorded by ARCHITECT.

### Step 18: Tests and manifest (audit 7.1-7.3, 8.1) + compile check 3 + baselines
**Scope:** `scenario_terminate.lua`, `run_smoke.lua`, scenario launch tables, `benchmark/smoke-windows.md`, PLAN Part 1 Step 4 text.
**Action:** Windows `countZombies` → `report:info` "not applicable on Windows", no check (7.1); drop `secondLine` (7.2); `DapClient.getLaunchArguments (program)` for the 17 tables (7.3); manifest `epochs 11` (Part 1 Step 4), `## toolchain` and `baseline` kept and recorded in Part 1 Step 4 with ARCHITECT's *"always build with cast, profile with scout"*. Then `cast --debug`, re-record `baseline/windows/` (`--record`), two check runs, scout.
**Validation:** 22/22 twice; scout report against `smoke-windows-report-baseline.md`.

### Step 19: Documents (audit 10.1-10.5, 5.1, 6.1, 4.2)
**Scope:** tracker (D-rows, Decision 5, Decision 6, thread table, Status), `SPEC.md`, `CLAUDE.md`, `Source/HELP.md`, `RFC.md` status, `README.md` Windows output line.
**Action:** D-rows for: silent break-in (R1, R7), stdout/stderr capture (R7 "B2"), Toolhelp threads (if kept), `GetSourceEntriesByLine` resolution, wake always (R5), runInTerminal (R6) — each quoting ARCHITECT. Decision 6 adds `isBreakInPending`. Thread table adds the thread-pool callback that calls `wake`. SPEC feature flows (1, 8, 9, breakpoints, events table, build line), CLAUDE.md (Status, Layer Order, tests/smoke row, Critical Path), HELP.md console values. RFC.md Status → Implemented (Option B).
**Validation:** Every path and class named exists on disk; every decision line quotes ARCHITECT.

### Step 20: Audit, then doxygen pass
**Action:** @Auditor once (whole sprint). COUNSELOR filters findings (read + clause), resolves accepted ones. Then @Pathfinder doxygen pass with the target table (audit "Doxygen pass targets" + new classes `jam::AsyncOutputStream`, `jam::ScopedComInitialiser`), regenerate indexes (`ninja doxygen` for whatdbg; `<leader>bd` for JAM is ARCHITECT's).
**Validation:** Each block read against the code it documents.

## BLESSED Alignment
- **B:** every handle, wait, process and stream has one RAII owner (Steps 2, 4, 6); a failed attach terminates the suspended child (2.1).
- **L:** one pipe reader in JAM instead of two copies (Steps 2-3); launch split by responsibility (Step 6).
- **E:** no silent fallback (2.2), asserts on Win32 results (1.1, 2.14); `wake` tells (R5).
- **S (SSOT):** one path conversion per direction (Step 12); one context stack walk (2.8).
- **S (Stateless):** deferral state in the Model row and the lane (Step 7); no `unreportedThreadId` if Step 10 shows dbgeng reports the thread.
- **E (Encapsulation):** native path form and output drain are Session's (Steps 9, 12); no callback channel beside `Events` (4.1).
- **D:** no wait in a request body; no race between wake and drain (Steps 7-8).

## Rejected audit findings (with the read that refutes them)
- 3.5 name: superseded by R3 (`jam::AsyncOutputStream`).
- 10.4 first half: CLAUDE.md:79 already lists `spell.md`.
- 2.5: `timeoutMs` used at Session.cpp:348.
- 2.6: threat named — GO→BREAK on `E_PENDING` (ode_14.log).

## Risks / Open Questions
- None open. END compile (Step 1 call site at Nexus.cpp:72) is verified on ARCHITECT's build of END.
