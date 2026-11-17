# SPRINT-LOG.md
## whatdbg — Windows Host Abstraction Translator for dbgeng

**Project:** whatdbg  
**Repository:** https://github.com/jrengmusic/whatdbg  
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

## Sprint 37: Windows runInTerminal — the launch completes without a later message; the attached target exits in under a second ✅

**Date:** 2026-11-17
**Duration:** one session
**Scope:** `RFC-windows-run-in-terminal-wake.md` (MACHINIST), through `PLAN-windows-run-in-terminal-wake.md` Steps 1-8. Both files were deleted at the end of the sprint. ARCHITECT's rulings: *"machinist rfc told you current whatdbg failed, jam has changed. need to be fixed. you need to gather facts, write plan to fix it. that's simple."*; *"are there options? or only ONE SOLUTION?"*; on the RFC options, *"option b contract violation"* and *"option c revert everything ... BREAK EVERYTHING ELSE. break mac, break other project."*; on the relative target path, *"what's the point of this sprint"*; on the slow exit, *"exit attached app, DAP not immediately killed. it takes ~30s until eventually kill itself. why so long? why not instant?"*, then *"profile with scout"*, *"eve"*, *"release build not debug"*, *"eve's table probably stale, just conform to skill"*, then `/go`; *"plan and rfc hygiene. what are already completed/supersed? delete all"*.

### Agents Participated
- COUNSELOR: the plan, every brief, the validation of each return by read, the launcher-path and exit-time diagnoses, the scout workloads and manifests, both audit filters, the doxygen validation, ARCHITECTURE.md and CLAUDE.md, this log.
- Pathfinder (haiku): the inventory reads of whatdbg and jam, the git history of the launch path, the launcher probe, the scout runs, the doxygen passes and regenerations, `git status`.
- Engineer (haiku): Steps 1, 2, 2b and 6, both audit fixes, the builds and the smoke runs, the file deletions.
- Librarian (haiku): the Windows debug heap: when ntdll selects it, `_NO_DEBUG_HEAP`, the dbgeng create and attach options, and what other debuggers do.
- Auditor (opus): two audits, one of Steps 1-2b (2 findings) and one of Step 6 and the workloads (5 findings).

### Files Modified (by `git status`: 5 source, 2 documents, `docs/` regenerated, 13 benchmark files added, 13 RFC and PLAN files deleted)
- `Source/debug/Session.cpp` — `createSuspendedProcess` takes the client handles; `launchCaptured` connects each output pipe's client before its stream opens. `createCommFile` starts an overlapped `ConnectNamedPipe` and opens `launcherOutput` in `connectSource.onReady`, when the Launcher connects (`:358-377`). The source asserts a valid `connectEvent` (`:369`). `drainLauncherOutput` closes `connectSource` before `launcherOutput` (`:444-448`). `shutdown` closes `connectSource` first, cancels and waits for a pending connect, and resets `launcherInput` and `connectEvent` (`:604-620`).
- `Source/debug/Session.h` — the Windows members `connectEvent`, `connectOverlapped` and `connectSource`, with doxygen. The `createCommFile`, `shutdown`, `drainLauncherOutput`, `launchCaptured` and `launcherOutput` docs now state the connect order.
- `Source/Launcher.cpp:19-28` — the Windows constructor resolves the first target element against the working directory, the same expression as `Session.cpp:148-151`.
- `Source/Launcher.h` — the constructor doc states the path resolution.
- `Source/Main.cpp` — `enabledValue` (`:41`) and `noDebugHeapVariable` (`:47-49`); `main` sets `_NO_DEBUG_HEAP=1` on Windows before the dispatch (`:241-245`); `setenv (reexecMarker, enabledValue, 1)`; `#ifdef _WIN32` becomes `#if JUCE_WINDOWS`; the `@file` startup list gains the Windows step, and its Loader sentence names `dbgeng.dll`.
- `ARCHITECTURE.md:440` — the `Main.cpp` line names the `_NO_DEBUG_HEAP` step and `runLauncher`.
- `CLAUDE.md` — Current State; the table rows of the deleted RFC and PLAN files removed.
- `docs/` — regenerated XML and `tagfile.xml`.
- `benchmark/exit-windows.lua`, `.cmd`, `.md`, `exit-windows-sample.md` — the EVE close-and-exit workload through nvim-dap and whatdbg, on the `smoke.cmd` / `smoke-windows.md` / `smoke-sample.md` pattern. `exit-windows-control.lua`, `.cmd`, `.md` — the same close with no debugger.
- `benchmark/exit-windows-report.md`, `-report-baseline.md` (the 24.94 s run), `-sample-report.md`, `-control-report.md`, `-no-debug-heap-report.md`, `-samples.md` — scout outputs.
- Deleted: `RFC.md`, `RFC-message-loop-model.md`, `RFC-windows-run-in-terminal-wake.md`, `PLAN-audio-architecture-discipline-0` to `-7`, `PLAN-windows-conformance-audit.md`, `PLAN-windows-run-in-terminal-wake.md`. Each was implemented or completed, except `PLAN-windows-conformance-audit.md`: its Step 20 targets code that Sprints 35 and 36 rewrote.

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied
- [ ] The Sprint 37 code is inside `#if JUCE_WINDOWS`, except `enabledValue`, which the macOS `setenv` call now uses (`Main.cpp:169`). The macOS build did not run in this session.

### Problems Solved
- The launch response waited for the next client message. `jam::AsyncOutputStream::open` arms at once (`jam_AsyncOutputStream_windows.cpp:168-171`). On a pipe with no client, the zero-byte read fails and the count is -1 (`:73-77`, `:99-101`). `onReady` arms again only on a count of 0 (`:38-45`), so the source stopped for good. `Session` now connects each pipe before the stream opens on it. jam is unchanged.
- The Windows Launcher could not start a relative target. `CreateProcessW ("build/fixture.exe")` failed with error 2 on every binary. The absolute path worked. The adapter launch already resolved the path (`Session.cpp:148-161`).
- The attached EVE took about 20 s to exit. A target that starts with the debugger attached gets the Windows debug heap. dbgeng's `DEBUG_CREATE_PROCESS_NO_DEBUG_HEAP` does not apply to `AttachProcess`. `_NO_DEBUG_HEAP=1` in `main` reaches each target through the null environment of `CreateProcessW` (`Launcher.cpp:137`, `Session.cpp:270-272`).
- Audit 1: `connectEvent` not asserted (`Session.cpp:369`); `launcherInput` not reset in `shutdown` (`:618`). Both fixed. The shared resolve-and-quote helper and the base directory of a relative program were not findings.
- Audit 2: the dead `exit-windows-no-debug-heap` workload deleted (its report kept); the control baseline set to `exit-windows-report-baseline.md`; the default `vim.wait` interval removed; `#if JUCE_WINDOWS` throughout `Main.cpp`. The doxygen pass found the `"1"` literal twice (`Main.cpp:169`, `:242`), now `enabledValue`.

### Verification
- Windows `tests/smoke`: pass=22 fail=0 after each code step and after each audit fix. Scenario 22 matches `baseline/windows/22_run_in_terminal.txt`.
- scout, Release, 5 epochs, 5 s settle in each epoch: EVE under whatdbg 24.94 s (user 21.45 s) before, 6.05 s (user 1.19 s, −75.8 %) after; EVE with no debugger 5.60 s (user 0.80 s).
- The sampling run before the fix put EVE's threads in the ntdll heap frames (`rtl size heap`, `rtl free heap`, `rtl get user info heap`, `rtl compare memory ulong`).
- Doxygen: 0 warnings. The docs config does not define `JUCE_WINDOWS`, so the Windows-only blocks of `Session.h` are not in the XML.

### State for Continuation
- macOS build and `tests/smoke` against `baseline/macos/` are not run after Sprint 37. Check `Main.cpp:41`, `:169`.
- `_NO_DEBUG_HEAP=1` also reaches the children of each target, and it overrides a user's `_NO_DEBUG_HEAP=0` (`Main.cpp:241-245`).
- The cast build copies the artefact to `~/.local/bin/whatdbg.exe`. `~/.local/bin/whatdbg.old` is the 03:12 binary that the RFC tested.

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 36: jam::AsyncUpdater owns the native events — no native type in a public signature; the macOS launcher starts the target suspended ✅

**Date:** 2026-11-17
**Duration:** one session, continued across several context compactions
**Scope:** `ling/PLAN-async-updater-events.md` Steps 1-9, for jam (jam_core, jam_terminal), eve and whatdbg. Then the whatdbg launcher and the smoke baseline, which ARCHITECT added after the audit. ARCHITECT's rulings: *"FIX THE CONTRACT VIOLATIONS"*; *"why is our AsyncUpdater not simply wrap juce::ThreadPool and manage the events, whatever we need without needed to poke native OS API and create our own branch?"*; the AskUserQuestion answer **"Updater owns events"**; *"follow established pattern VERBATIM"*; *"FULLY EVENT DRIVEN. no manual callbacks, no manual state machine management, use framework API to its fullest extent. DESIGN BY CONTRACT. NO HANDROLL"*; *"exact same result at runtime"*; *"as long as the runtime result and speed on parity with old binary"*; *"windows will conform when we get there"*; *"make it RAII … why by hand?"*; *"we cant wrap this sprint if whatdbg unresolved. investigate"*; the AskUserQuestion answer **"Baseline + launcher"**; on the launcher wait: *"what's the most DETERMINISTIC? most BLESSED approach?"*, then `/go`.

### Agents Participated
- COUNSELOR: the plan, every brief, the validation of each return by read, the audit filter, the whatdbg CPU investigation, the scout manifests, the doxygen validation and corrections, this log.
- Pathfinder (haiku): the inventory reads, the scout runs (EVE replay and live, whatdbg smoke, the test-22 split, the baseline record), the doxygen passes.
- Engineer (haiku): the jam, eve and whatdbg code steps, the audit fixes, the launcher change.
- Librarian (haiku): the lldb `darwin-debug` launch path, the debugserver attach and resume path, the XNU `ptrace`, `wait4`, `reap_child_locked` and `EVFILT_PROC` sources.
- Auditor (opus): one audit of jam, eve and whatdbg — 22 findings.

### Files Modified (36 total: jam 22, eve 7, whatdbg 6, ling 1, by `git status`)
- jam `jam_core/concurrency/jam_AsyncUpdater.h`, `.cpp` — the `Context` alias and the native members leave the header. `struct Native` holds the GCD queue and DATA_OR source (macOS), or the one-thread pool and work (Windows). `getContext()` returns `AsyncUpdater*`. The new nested event API: `Handle`, `Source` (readiness of one handle with its own one-shot timer: `open`, `close`, `arm`, `startTimer`, `onReady`, `onTimer`), `Signal` (macOS: `open`, `close`, `onSignal`), `Work` (`open`, `close`, `submit`, `onWork`). Each native body moved from its consumer. Each destructor calls `close()`.
- jam `jam_core/jam_core.mm:1`, `jam_terminal/jam_terminal.mm:1` — `#define OS_OBJECT_USE_OBJC 0` deleted. Each file is its unity include only.
- jam `jam_terminal/tty/jam_AsyncOutputStream.h`, `native/jam_AsyncOutputStream_mac.cpp`, `native/jam_AsyncOutputStream_windows.cpp` — `using Handle = AsyncUpdater::Handle;`; `open (Handle, AsyncUpdater*, int, double)`; each Native holds an `AsyncUpdater::Source` and keeps the clock logic of HEAD. Windows: `~Native()` deleted; the member `juce::ErasedScopeGuard readCancellation` (`_windows.cpp:169-184`) cancels and waits for the overlapped read, and member destruction orders it after `source` (audit finding 3, ARCHITECT: *"make it RAII"*).
- jam `jam_terminal/tty/jam_TerminalTTY.h`, `native/jam_TerminalTTY_posix.h`, `.cpp`, `native/jam_TerminalTTY_windows.h`, `.cpp` — `open` takes `AsyncUpdater*`; Windows `resizeWork` becomes `AsyncUpdater::Work`, `writeWait` becomes `AsyncUpdater::Source` on `writeEvent`.
- jam `jam_terminal/display/jam_TerminalDisplay.h`, `native/jam_TerminalDisplay_posix.h`, `.cpp`, `native/jam_TerminalDisplay_windows.h`, `.cpp` — `open` takes `AsyncUpdater*`; `addSources`, `resizeSource` and `cancelled` become one `AsyncUpdater::Signal resize`; `<dispatch/dispatch.h>` leaves the posix header.
- jam `jam_terminal/display/jam_StdioServer.h`, `.cpp` — `open` takes `AsyncUpdater*`.
- jam `jam_terminal/display/jam_StdioProtocol.h:30` — `@param callback` added (doxygen warning at `:23`).
- jam `jam_audio_devices/audio_io/jam_VirtualDevice.h:99-101` — doxygen of `getContext`.
- jam `jam_subprocess/subprocess/native/jam_ChildProcess_posix.cpp:6` — one include (a DCF edit outside the named file set).
- eve `Source/TerminalMemoryTTY.h`, `.cpp` — `open (jam::AsyncUpdater*, …)`; `dispatch_async_f` becomes an `AsyncUpdater::Work`; `~TerminalMemoryTTY() { close(); }` and `close() { tty->close(); cancelPendingUpdate(); }`, the sibling pattern of `jam_TerminalTTY_posix.cpp:173-176`, `jam_TerminalDisplay_posix.cpp:9-12` and `jam_VirtualDevice.cpp:8-11`; `tty` stays at its HEAD position (first member).
- eve `Source/EVETerminalProcessor.cpp` — the `applyNames` change (a DCF edit outside the named file set).
- eve `benchmark/live-report.md`, `live-samples.md`, `replay-report.md`, `replay-samples.md` — scout outputs.
- whatdbg `Source/Launcher.cpp:4-14` — macOS includes `<spawn.h>`, `<signal.h>`, `<sys/event.h>`, `<sys/wait.h>` and `extern char** environ;` (the sibling `jam_ChildProcess_posix.cpp:8`); `<cstring>` removed.
- whatdbg `Source/Launcher.cpp:158-221` — macOS `run`: the reply FIFO opens with `O_CLOEXEC` (`:166`); `posix_spawnp` with `POSIX_SPAWN_START_SUSPENDED`, the attributes released by `juce::ErasedScopeGuard attributesDestroy` (`:174-182`); a kqueue `EVFILT_PROC` watch with `NOTE_EXIT | NOTE_EXITSTATUS` is registered before `sendPid` (`:187-198`); after `didAttach`, one blocking `kevent` gives the exit status, and a signal gives `signalExitBase` 128 plus the signal number (the jam `ChildProcess` mapping, `jam_ChildProcess_posix.cpp:42`, `:107-111`); otherwise `SIGKILL`; a spawn failure sends the error. `execvp` is gone.
- whatdbg `Source/Launcher.h` — the class, `run`, `sendPid`, `launchTarget` and `readFifo` doxygen state the suspended spawn and the kqueue wait; no `execvp` remains.
- whatdbg `tests/smoke/baseline/macos/22_run_in_terminal.txt` — one `event breakpoint changed` line deleted. The old flow attached to the whatdbg image before the exec, which gave a third location event. The new sequence equals the direct launch (`01_launch_bp_continue.txt:8-9`).
- whatdbg `benchmark/smoke-report-baseline.md` — recorded again for the 22-scenario workload: 7 epochs, epoch 4.7065e10 ns, user 1.993e9 ns, system 1.120e9 ns (17-11-2026 02:43).
- whatdbg `benchmark/smoke-report.md` — scout output.
- whatdbg `carol/SPRINT-LOG.md` — the Sprint 35 lines that stated the `OS_OBJECT_USE_OBJC` define as a fix are corrected (Step 9.1); this entry.
- ling `PLAN-async-updater-events.md` — the locked plan.

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied
- [ ] Windows code (`jam_AsyncUpdater.cpp` Windows blocks, `jam_AsyncOutputStream_windows.cpp`, `jam_TerminalTTY_windows.*`, `jam_TerminalDisplay_windows.*`) is not compiled on this macOS host. ARCHITECT: *"windows will conform when we get there"*.

### Problems Solved
- The link fault that the define hid: `AsyncUpdater::Context` was `dispatch_queue_t` in public `open` signatures, so a `.mm` definition and a `.cpp` call mangled differently. No native type is in a public signature now, and both defines are gone.
- Every native event object (READ, TIMER and SIGNAL sources, pool waits, timers and work) has one owner, a nested RAII class of `jam::AsyncUpdater`. The consumers create no native object.
- Audit: 22 findings, each resolved by a read-checked fix or rejected with the read that refutes it. Findings 11-13 were plan-text corrections. Finding 3 is the `readCancellation` RAII member.
- Doxygen pass findings: F1 rejected (`jam_TerminalTTY_windows.cpp:751`, the jassert is the precondition form); F2 rejected (the zero count matches HEAD, `jam_TerminalTTY_posix.h:33`); F3 accepted (the TerminalMemoryTTY destruction order).
- whatdbg CPU. The old 21-scenario baseline measured a different workload. Scenario 22, added in d17013b, made the HEAD user time 6.884e9 ns. The cause was the macOS launcher: it sent its own pid and called `execvp` only after `didAttach`, so lldb attached to the 90.7 MB whatdbg image (which embeds liblldb). It now starts the target suspended and sends the target pid, as Windows does (`Launcher.cpp:127-132`). debugserver itself spawns suspended, then attaches with `PT_ATTACHEXC` (llvm `MachProcess.mm:3723`, `:3672`).
- The launcher wait. `PT_ATTACHEXC` makes the debugger the parent of the target (XNU `mach_process.c:285-292`), so `waitpid` in the launcher returns ECHILD (`kern_exit.c:2926`, `:3080-3082`). debugserver reaps the target (llvm `DNB.cpp:521`, `:165`, `:271`). The reap returns the target to the launcher and posts the exit event (`kern_exit.c:2754-2761`). The original parent can register the kqueue watch (`kern_event.c:1125-1128`), and the event carries the exit status (`:1234-1238`).

### Verification
- whatdbg smoke (`benchmark/smoke.sh`): pass=22 fail=0.
- whatdbg scout (`benchmark/smoke.md`, 7 epochs, Release), against the old baseline: epoch 4.6992e10 ns (−25.1%), user 2.002e9 ns (−7.7%), system 1.115e9 ns (−28.7%). HEAD on the same workload: user 6.884e9 ns.
- whatdbg scenario 22 alone (3 epochs): user 5.068e9 ns before, 1.019e8 ns after; epoch 3.602e9 ns before, 2.737e9 ns after.
- After each run, `ps` showed no launcher or fixture process.
- EVE replay (Step 8): 1.623e9 ns against the Step 1 baseline 2.295e9 ns; round 4, after the TerminalMemoryTTY close fix: 1.684e9 ns, exit 0. EVE live: epoch +0.32% (p=0.34), user +0.18% (p=0.49). EVE Profile build: OK.
- ling `benchmark/build.md`: exit 0.

### State for Continuation
- Windows build and runs are ARCHITECT's: whatdbg `benchmark/smoke-windows.md`, EVE `launch-windows*.md`.
- KANJUT `kuassa_AsyncUpdater` is not synced. It syncs from jam through cast (jam `carol/SPRINT-LOG.md:414`).
- END, TIT, CAKE and CAROLINE are not rebuilt against the new `AsyncUpdater` API.
- The doxygen XML is not regenerated. ARCHITECT runs `<leader>bd`, then checks that the `jam_StdioProtocol.h:23` warning is gone.
- The launcher no longer execs. `Session_mac.cpp:228-233` still sets `target.process.stop-on-exec` to `false`.
- The comment edits in `Launcher.h` and `Launcher.cpp:164-165`, `:184-186` came after the last build.

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 35: One stdio transport in jam — whatdbg on jam::StdioServer over TerminalFIFO rings ✅

**Date:** 2026-11-16
**Duration:** one session, continued across several context compactions
**Scope:** `ling/PLAN-stdio-transport.md` Steps 1-10 and 12, for jam and whatdbg. This entry replaces the first Sprint 35 entry (the jam_core `StdioServer` thread and `jam::FIFO` lane), which ARCHITECT rejected. ARCHITECT's rulings: *"you need ring buffer, terminal ring buffer IS TerminalFIFO"*; *"TerminalFIFO is the canon pattern carry only bytes"*; *"those are all exclusive to terminal, it's provision module, not at core"*; *"FULLY EVENT DRIVEN. no manual callbacks, no manual state machine management, use framework API to its fullest extent. DESIGN BY CONTRACT. NO HANDROLL"*; *"no copy. no alloc. no mutations. no temporary containers. no shadow state"*; *"fix whatddbg first with StdioServer. profile, no regression, exact same result at runtime. ling after that"*; *"NEVER INTRODUCE NEW SEMANTIC, ALWAYS CODEBASE ESTABLISHED PATTERN"*; *"windows will conform when we get there"*; *"only jam and whatdbg. we're not done with ling. next sprint"*.

### Agents Participated
- COUNSELOR: the plan, every brief, the validation of each return by read, the runInTerminal and stderr diagnoses, the audit filter against HEAD and EVE precedent, the scout manifests, ARCHITECTURE.md / SPEC.md / CLAUDE.md, the doxygen corrections, this log.
- Pathfinder (haiku): the pattern and HEAD-precedent reads, the scout sample run and the end-of-stdin test, the doxygen pass on jam and whatdbg.
- Engineer (haiku): the jam and whatdbg code steps, the audit fixes, the end-of-stdin change and its revert.
- Auditor (opus): one audit of jam, whatdbg and ling — 37 findings.

### Files Modified (110 total: jam 80, whatdbg 30, by `git status`)
- jam `jam_core/concurrency/jam_FIFO.h`, `jam_core/json/jam_StdioServer.h`, `.cpp` — deleted (the rejected first design); their rows in `user-modules-info.md` and includes in `jam_core.h` / `jam_core.cpp` removed.
- jam `jam_core/jam_core.mm:1` — `#define OS_OBJECT_USE_OBJC 0` (audit 18). Correction: the define is a contract violation, not a fix. It hid a native type, `jam::AsyncUpdater::Context` (`dispatch_queue_t`), in public `open` signatures that plain `.cpp` units call. The next sprint removes it: `jam::AsyncUpdater` keeps its native context in a private `Native`, owns the events (`Source`, `Signal`, `Work`), and each `open` takes `const jam::AsyncUpdater*`.
- jam `jam_core/document/jam_Document.h:944`, `:1338-1341`, `.cpp` — `arena` owns the bytes and `source` is a `std::string_view`; `getSource` returns a view. A document can borrow its source.
- jam `jam_core/json/jam_JsonDocument.h:37-46`, `.cpp:20-29` — `read (std::string_view)` parses the caller's bytes in place, with no copy (ARCHITECT approved).
- jam `getSource` callers take `std::string_view`: `jam_markdown/document/jam_MarkdownDocument.h`, `.cpp`, `jam_MarkdownBlockParser.cpp`, `jam_document/document/jam_ReflowDocument.cpp` (getCodepoint, getRowWidth, getRows, getCutSegments), the 36 `jam_mermaid_diagram/diagram/jam_Mermaid*.cpp|h` files, `jam_web/css/jam_Css.*`, `jam_web/html/jam_Html.*`, `jam_syntax_tree/document/jam_LanguageValidator.*`, `jam_fuzzy/finder/jam_BonusMatrix.*`, `jam_FuzzyPattern.cpp`, `jam_FuzzyPatternMatch.cpp`. `jam_core/text/jam_Strings.h:647-668` — `getView (const std::string&)` and `getView (const juce::MemoryBlock&)`.
- jam `jam_terminal/processor/jam_TerminalFIFO.h:86-97`, `:125-138`, `:167-195`, `.cpp` — `writeFromTerminalTTY (Handle)` and `writePendingData (Handle)`; one ring body `writeFromDevice` for TTY and Handle; a full ring makes no device read and gives 0 (audit 1).
- jam `jam_terminal/processor/native/jam_TerminalFIFO_mac.cpp:33-67` — `::read` and `::write` bodies with the TerminalUnixTTY return codes. `native/jam_TerminalFIFO_windows.cpp` — `PeekNamedPipe`, then `ReadFile` with an `OVERLAPPED` record and `GetOverlappedResult`; `WriteFile` with `OVERLAPPED`, waited (the TerminalWindowsTTY::read pattern; audit 4).
- jam `jam_terminal/display/jam_StdioServer.h`, `.cpp` (new) — a `juce::OutputStream` over the `input` and `output` TerminalFIFO rings and one `AsyncOutputStream` on stdin; `open`, `close`, `applyPendingData`, `write`, `flush`; `write` returns `false` when a flush leaves the full ring with no free space (`.cpp:79`).
- jam `jam_terminal/display/jam_StdioProtocol.h`, `.cpp` (new) — `applyMessages` gives each complete Content-Length body as a view in the ring and stops at a rejected or incomplete frame; `writeMessage` writes one frame.
- jam `jam_terminal/jam_terminal.h`, `.cpp`, `.mm` — the StdioServer and StdioProtocol includes; the `OS_OBJECT_USE_OBJC` define (a contract violation; the correction above applies).
- jam `jam_core/text/jam_Format.*`, `jam_core/json/jam_JsonWriter.*`, `jam_terminal/buffers/jam_TerminalEncoder.*`, `jam_core/text/jam_AttributedChar.h` — kept from the first Sprint 35 work: `appendBytes`, the exponent split, the `openers`/`closers` lookups, `writeCodepoint` deleted for `Format::appendUtf8`.
- `Source/Processor.h`, `.cpp` — `jam::StdioServer stdio` (the last member) and `jam::Mailbox<jam::JsonDocument> requests` for `prepare` / `releaseResources`; `applyPendingRequests` (`.cpp:132-149`) reads frames in place with `JsonDocument::read`, runs the cancellations, and leaves a deferred frame in the ring; `process` reads the ring first, then the mailbox (`:68-71`); `addMessage (std::string_view)`; `registerCommands`; `releaseResources` closes `stdio` first; the stdin callback wakes the engine (`:22-26`).
- `Source/Whatdbg.h`, `.cpp` — `dap::Reader`, `dap::Writer` and the `numMessages` route deleted. `Source/WHATDBGModel.*`, `cast/identifiers.md`, `Source/generated/*` — `numMessages` removed.
- `Source/dap/FIFO.h`, `Reader.*`, `Writer.*` — deleted.
- `Source/debug/Session.h`, `Session.cpp`, `Session_mac.cpp` — each pipe has readiness through `jam::AsyncOutputStream` and bytes in a `jam::TerminalFIFO` that the engine context reads; macOS `initialize` sets `target.process.stop-on-exec` to `false` (`Session_mac.cpp:228-233`); `attachLaunchTarget` no longer calls `process.Continue ()` (`:338-353`); `launch` redirects the target stderr to the FIFO `whatdbg-stderr` (`:260-277`), and `drainOutput` reads it (`:460-473`); Windows `drainOutput` and `launchCaptured` unrolled.
- `Source/Launcher.h`, `.cpp:89-97` — macOS `getLine` reads again after an `EINTR` from a debugger attach, and clears the error.
- `tests/smoke/baseline/macos/22_run_in_terminal.txt` (new) — the macOS baseline of scenario 22.
- `benchmark/smoke-report.md`, `benchmark/smoke-sample.md` (new), `benchmark/smoke-sample-report.md` (new).
- `ARCHITECTURE.md`, `SPEC.md`, `CLAUDE.md` — the readiness context, the two rings, the mailbox, the order of `process`, the deferred frame in the ring, the macOS stderr FIFO, the Windows `wake`, the timings.
- `.clangd`, `.project` — regenerated on macOS.

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied
- [ ] Windows code (`jam_TerminalFIFO_windows.cpp`, the StdioServer Windows path, `Session.cpp`) is not compiled on this macOS host. ARCHITECT: *"windows will conform when we get there"*.
- [ ] A frame larger than the 1 MiB input ring (`Processor.h:189`) never completes. HEAD had no bound (HEAD `Reader.cpp:77-86`). The bound follows from the TerminalFIFO ruling.

### Problems Solved
- One stdio transport: stdin → readiness → StdioServer input ring → engine context → output ring → stdout. Bytes stay in the ring until the parse reads them in place.
- The adapter exits: the HEAD `dap::Reader` closed stdin while `getline` blocked on it, so the old binary never exited. The new binary exits in 8 ms.
- Scenario 22 (runInTerminal) passes on macOS. It had three causes: the early `Continue`, the exec stop that no table entry handled, and the `EINTR` from the attach in the Launcher read.
- The macOS target stderr arrived as category stdout through the single pty. The FIFO now carries it as stderr.
- Audit findings 1, 3-7, 9, 11, 13-15, 18, 21, 23-25, 33, 35 and 36 (constructor length) fixed and checked by read. Correction: 18 was closed by the `OS_OBJECT_USE_OBJC` define, which is a contract violation (see the `jam_core.mm:1` line above).
- Audit findings rejected against HEAD or EVE precedent: 8 (HEAD `Processor.cpp:268-271`), 10 (HEAD `Whatdbg.cpp:17-21`, `:99-102`), 16 (jam `TerminalTTY_posix.cpp:259-267`), 17 (HEAD `Session.cpp:290`), 22 (HEAD `Processor.cpp:125-128`), 29 (HEAD `:52-80`), 30 (HEAD `Session.cpp:99`, `:390`), 31, 32 (`jam_AsyncUpdater.cpp`), 34 (refuted by `Processor.cpp:68-71`), 36 call spacing (HEAD `:49-84`), 37 (the files were over the limits at HEAD). 19 and 20 wait for the Windows pass. 2 is the ring bound above. 26-28 resolved by the docs and doxygen pass.
- PLAN Step 6.2 said the end of stdin sets `isRunning` to 0. That dropped the exit events of `terminate` in scenarios 04, 08, 14 and 16 (18/22). HEAD runs on after stdin closes (HEAD `Whatdbg.cpp:29-32`), so the HEAD behaviour is restored, by the ruling *"exact same result at runtime"*.
- `StdioServer::write` looped with no progress when stdout took nothing (doxygen pass finding).

### Verification
- Direct smoke (`benchmark/smoke.sh`), after the last change: pass=22 fail=0.
- Scout (`benchmark/smoke.md`, 7 epochs, Release, 22 scenarios): epoch 4.7929e10 ns against 6.2698e10 ns (−23.6%); user 6.884e9 ns against 2.169e9 ns (+217%); system 1.840e9 ns against 1.563e9 ns (+17.7%).
- Sample (`benchmark/smoke-sample.md`, 1 ms): the most samples are in two liblldb frames (6722 and 6714); `processor::handle async update` has 171.
- The jam doxygen blocks of StdioServer, StdioProtocol, the TerminalFIFO overloads and `JsonDocument::read`, and the whatdbg blocks of Session, Processor, Launcher and Whatdbg, were read against the code.

### State for Continuation
- user CPU +217% (`benchmark/smoke-report.md:15`). Scenario 22 now attaches to the whatdbg launcher binary (`Processor.cpp:480`), a path that the old binary never ran. This cause is not measured.
- Windows build and the Windows DAP run are ARCHITECT's. After a session ends, `tasklist | findstr whatdbg` shows whether the adapter exits.
- The doxygen XML is not regenerated: jam with `<leader>bd`; whatdbg `docs/xml/index.xml` still lists `addRequest`, `pendingRequests`, `dap::Reader` and `dap::Writer`.
- ling `Source/Relay.h`, `.cpp` are on the new API but are not logged: ARCHITECT: *"we're not done with ling. next sprint"*. Then `PLAN-design-conformance.md` resumes at Step 17, then `PLAN-clangd-parity.md` at Step 11.

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 34: Audio Architecture Discipline — Model, Engine Context, Message Loop ✅

**Date:** 2026-11-14
**Duration:** one session, continued across a context compaction
**Scope:** `PLAN-audio-architecture-discipline-{0..7}.md`. ARCHITECT: *"if i give you permission to build can you complete the whole plan?"*; *"just implement whatever you can implement on windows ... implement dont defer. only run audit wholesale, when the whole plan completed. PLAN locked, ARCHITECT ratifies the number and the policy at the lock. DESIGN BY CONTRACT /go no gate"*; *"always build using cast"*; *"backup current binary. then build release install. proceed"*; *"fold into your task reset whatdbg, cast, ggwp, scout version to 0.0.1, and rebuild all ... then proceed your task to completion"*.

### Agents Participated
- COUNSELOR: plan execution, every brief, validation of each return by read, the terminate and message-loop diagnoses, ARCHITECTURE.md / SPEC.md / CLAUDE.md / tracker, the audit filter, this log.
- Pathfinder (haiku): smoke runs (baseline, Parts 2-5, Part 6, ten final runs, post-audit), scout runs, terminate and attach probes, doxygen XML regeneration, the post-audit doxygen pass.
- Engineer (sonnet): Parts 2-6 code, the `JUCEApplicationBase` correction, the audit fixes, every cast build, the cast restore.
- Auditor (opus): one wholesale audit — 43 findings.

### Files Modified
- `Source/WHATDBGModel.h`, `.cpp` (new) — `jam::Model` with SESSION and EXCEPTION rows, 17 parameters; `debug::State` class deleted (`State.h` keeps the enums, `Events`, `getExceptionName`).
- `Source/Processor.h`, `.cpp` (new) — `private jam::AsyncUpdater` engine context: `requests` (`dap::FIFO<jam::JsonDocument>`, 64) and `messages` (`dap::FIFO<std::string>`, 4096) lanes, `commands` and `cancellations` tables, `prepare`/`releaseResources` tells, `process`, the eighteen command bodies moved from `Whatdbg`.
- `Source/Whatdbg.h`, `.cpp` — rewritten: `juce::JUCEApplicationBase` + `juce::ValueTree::Listener`; two `events` entries (`numMessages` drains the lane to `dap::Writer`, `isRunning` sets the return value and quits).
- `Source/Main.cpp` — `--help` through `jam::TerminalPage`; `runAdapter` runs `JUCE_CREATE_APPLICATION_DEFINE (Whatdbg)` + `JUCEApplicationBase::main` with the sidecar path.
- `Source/dap/Reader.*` (`onMessage`, no lane), `Writer.*` (new), `FIFO.h` (new), `Types.*` (`Types.cpp` new; `getRequest`, `nextSeq` on the Model, event builders).
- `Source/debug/VariableManager.*`, `StopManager.*` (new atoms); `Session.h`, `Session.cpp`, `Session_mac.cpp` (`wake`, `cancel`, `resetCancel`, macOS broadcaster, `appendStackFrames (…, addFrame)`), `Callbacks.*` (report through `Session::events`), `BreakpointManager.*`.
- `Source/HELP.md` (new), `cast/files.md` (new), `cast/identifiers.md`, `cast/spell.md`, `project-info.md` (modules, help rows, version 0.0.1).
- `tests/smoke/` — 21 scenarios, transcript baselines, `scenario_inspect.lua`, `fixture_module.cpp`, `fixture_load.cpp`, `build.sh`; `benchmark/` scout manifests and reports.
- jam: `jam_tui/` (new module, `jam::TerminalPage`), `user-modules-info.md`, `cast/spell.md`. scout, ggwp, cast: help through `jam::TerminalPage`; Help.* and style.css deleted; version 0.0.1.
- `ARCHITECTURE.md` (v0.7.0), `SPEC.md`, `CLAUDE.md`, `PLAN-…-0-tracker.md` (Status), `docs/xml` (regenerated; stale `classdebug_1_1_state.xml` removed).

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied
- [ ] Windows code (`Session.cpp`, `Callbacks.cpp`, `Loader.cpp`) is not compiled on this macOS host.
- [ ] Lean 300: `Processor.cpp` is about 670 lines — the eighteen command bodies, placed there by PLAN Part 6.

### Problems Solved
- The message loop returned at once on macOS: `runDispatchLoop` is `[NSApp run]` (`juce_MessageManager_mac.mm:323-351`), and only `JUCEApplicationBase::main` creates `NSApp` (`juce_ApplicationBase.cpp:235-258`). `Whatdbg` became a `JUCEApplicationBase` (sibling `ling/Source/Main.cpp:276-486`).
- After a terminate whose kill the engine reported as a signal stop, the terminate deadline was never checked, and whatdbg stayed alive: `process` polls while a deadline is pending (statements 2 and 5).
- Scenario 02 was not deterministic: the test harness killed the attached target while whatdbg killed it (two owners). The harness now waits for `terminated` before its cleanup kill.
- The frame id was computed in two places: `VariableManager` is the one assigner through `addFrame`.

### Deliberate differences beyond D1-D7
- D8: a terminate of a target that the engine first reports as stopped by the signal now sends `stopped` (exception), then `exited` and `terminated`; before, the transcript ended at `stopped` and whatdbg exited by its 5 s deadline or by the client. Baselines 04, 08, 09, 14, 16 updated. Basis: the zombie fix intent, "the main loop's normal pollEvents path observes the debuggee's exit" (ARCHITECTURE v0.6.0:378-382).
- D9: attach + terminate ends `exited`, `terminated` (5 of 5 probe runs without the second killer). Baseline 02 updated.

### Verification
- macOS smoke: ten runs, all 21 sequence checks pass in each; two more after the audit fixes. Scenario `17_evaluate_fail` fails its two evaluate checks in every run, as before the rewrite (`evaluate notAVariable` gives `success=true`).
- scout with the baseline column (after the scout and cast repairs, whatdbg Release built by the new cast, 7 epochs): `epoch` 4.5390e10 ns, change −27.6%, probability 0.75; `user` 2.3587e9, change +8.7%, probability 0.0005; `system` 1.6543e9, change +5.8%, probability 0.0007 (`benchmark/smoke-report.md`, 14-11-2026 16:14).
- scout, Release, 7 epochs, against the Part 1 baseline (11 epochs), read side by side before the repairs: `epoch` 4.5387e10 ns (±0.12%, min 4.5296e10, max 4.6253e10) vs 6.2698e10 (±0.327%, min 6.1858e10, max 6.5898e10); `user` 2.376e9 (±0.49%, min 2.333e9, max 2.414e9) vs 2.169e9 (±2.07%, min 1.745e9, max 2.270e9); `system` 1.643e9 (±0.84%, min 1.621e9, max 1.665e9) vs 1.563e9 (±1.58%, min 1.335e9, max 1.619e9).
- T1 (18 commands) and T4 (drains) of Part 1 read against `Processor.cpp` and `StopManager.cpp`: match, except D2 (drains run on the next tick) and D8.

### Audit
- Resolved: #3 (reader starts only after a good `prepare`), #5 (`onReleaseResources` clears the deadline), #8 (`parse (std::string&&)`), #9 (`addMessage` moves), #10 (`TerminalPage` moves the fetchers), #13 (one frame-id assigner), #21 (`inline` removed), #22 (`"Locals"` literal per Part 3:81), #26 (tracker Status), #27 (redundant includes), #28 (unnamed parameter), #29 (HELP versions 0.0.1), #30-38 (doxygen pass, 0 warnings), #40-43 (documents).
- Rejected, each by its read: #1 (the Part 1 baselines already end with `stopped exception`), #2/#12/#15/#17 (ARCHITECT's 62b81cb forms, moved verbatim), #4/#6/#7/#14/#16/#18 (PLAN-ratified, or closed by #5), #11 (twice, the clause says "more than twice"), #19 (the page destructor tripped `jam_TextEditor.cpp:1397`), #20 (templates stay in the header), #23 (`execv` does not search PATH, `Main.cpp:156`), #24 (fact for ARCHITECT), #25 (build needs the include rows), #39 (log text, no clause).

### State for Continuation
- Windows: build, the smoke suite against the Windows baseline, the EVE session, and the scout Windows run (Parts 1-7 Windows steps) are ARCHITECT's.
- Part 7 Step 4 (field debugging of a JAM project) is ARCHITECT's.
- `user` and `system` CPU time rose 9.5% and 5.1% against the baseline; `epoch` fell 27.6%.

### cast and scout repaired (ARCHITECT: *"don't leave cast and scout at broken state, why crash? fix it"*)
- cast crashed before main: its generated namespace-scope comment tables copied `Id::comment` during static initialisation, and since cast commit 106685c that identifier is defined in jam's TU (`jam/generated/jam_Core.cpp:50`); cross-TU dynamic initialisation order is unspecified. ARCHITECT chose *"First-use shape in jam"*. jam `cast/code.cast:150-157` adds the `shared-hash-map` shape (a struct over `jam::HashMap` whose constructor holds the entries); cast's spell emits `map::ClangComment` … `map::SqlComment` and `map::CommentSyntax` through it into `HashMaps.h`; each read site holds `static const jam::SharedInstance<map::CommentSyntax> commentSyntax { std::in_place };` (the form of `jam_Format.cpp:850`). New cast: `--help` exit 0, rebuilds itself with no change, builds whatdbg.
- scout could not read back its own report: an empty samples file left the table type empty (`Benchmark.h:193`), and `jam::Format::isNumber` rejected the exponent form that `juce::String (double)` writes (`jam_Format.cpp:828-846`). Fixed: the type defaults to `Id::epoch`; `Format::isNumber` accepts one exponent after today's mantissa rule (`isMantissa`); `runEpochs` validates the baseline with `jam::ConfigDocument::isValid` and fails with a diagnostic; `ProfileReport::getProfile` looks baseline zones up by `jam::Format::toType` (the typing of `ConfigDocument::getValueTree`). `benchmark/smoke-report-baseline.md` and `smoke-report.md` headings set to `## epoch`; `smoke.md` compares against the baseline again.

### Debts Paid
- None

### Debts Deferred
- None

## Sprint 33: DAP JSON Through jam::JsonDocument and jam::JsonWriter ✅

**Date:** 2026-11-14
**Duration:** part of one session. `jam/carol/SPRINT-LOG.md` Sprint 187 is the primary record.
**Scope:** `jam/PLAN-json.md` ruling 3, ARCHITECT: *"All of them"* — "whatdbg's full DAP path: DynamicObject builds replaced by JsonWriter appends, reads by JsonDocument, JSON::toString removed."

### Files Modified
- `Source/dap/Reader.h`, `.cpp` — DAP messages read through `jam::JsonDocument`.
- `Source/dap/Types.h`, `Source/Whatdbg.h`, `.cpp`, `Source/debug/BreakpointManager.h`, `.cpp`, `Source/debug/Session.h`, `.cpp`, `Source/debug/Session_mac.cpp` — responses and events built through `jam::JsonWriter`; `juce::DynamicObject` builds and `JSON::toString` removed.
- `cast/identifiers.md`, `cast/source.cast` (new), `cast/spell.md`, `Source/generated/*` (new) — the identifier lane and its cast output.
- `CMakeLists.txt` — the `juce-plugin-window-size-hook.patch` row.

### Alignment Check
- [x] BLESSED principles followed
- [x] NAMES.md adhered
- [x] MANIFESTO.md principles applied
- [ ] `Session.cpp` (Windows) is not compiled on this macOS host.

### Problems Solved
- whatdbg built DAP JSON through `juce::DynamicObject` and `juce::JSON`. It now uses jam's one JSON module.

### State for Continuation
- Windows build and a DAP run are not done this session.

### Debts Paid
- None

### Debts Deferred
- None

