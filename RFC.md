# RFC: TUI/CLI Debuggee Terminal Attachment

**Status:** Draft — pre-flight research, not yet planned or approved.
**Origin:** ARCHITECT question during MACHINIST session (nvim build-only keymap task) — surfaced a gap in whatdbg's terminal handling for CLI/TUI debuggees. Captured here per RFC Fidelity Protocol; no scope decided yet.

---

## 1. Problem

whatdbg currently gives a launched debuggee's stdio a real terminal on Windows, but not on macOS. For CLI/TUI programs (END, TIT, CAKE — raw-mode/ncurses-style rendering, resize events, ANSI, `isatty()` checks), the macOS path breaks interactivity: output is captured into a text buffer and surfaced as a DAP `output` event, not delivered to a live TTY.

## 2. Current Behavior (cited)

### 2.1 Windows — `Source/debug/Session.cpp`
- `Session.cpp:137` — comment: *"WHATDBG: CreateProcess2 commandLine: ..."* logged immediately before the `CreateProcess2` call.
- `Session.cpp:151` — `options.CreateFlags = DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE;`
- Effect: debuggee spawns into its own new console window. Real TTY. dbgeng debugs out-of-process via Windows debugging API — no stdio contention. **TUI apps work under the debugger today on Windows.**

### 2.2 macOS — `Source/debug/Session_mac.cpp`
- `Session_mac.cpp:279-283` — `launchInfo.SetLaunchFlags(lldb::eLaunchFlagDebug | lldb::eLaunchFlagStopAtEntry);` then `process = target.Launch(launchInfo, error);`.
- No TTY/console flag set.
- `Session_mac.cpp:68-90` — `drainProcessStdio()` reads via `process.GetSTDOUT()` / `process.GetSTDERR()` into `state->debuggeeOutputText`, a plain string buffer, tagging `state->debuggeeOutputCategory` as `"stdout"` or `"stderr"`.
- `Source/Whatdbg.cpp:296-310` — `drainDebuggeeOutput` forwards that buffer as a DAP `output` event using the captured category — text only, no TTY semantics.
- Effect: `isatty()` on the debuggee reports false. Raw-mode/ncurses rendering will not behave interactively — same as running under a pipe. **TUI apps do not work correctly under the debugger today on macOS.** Plain line-based CLI output is unaffected (captured either way).

### 2.3 whatdbg has no `runInTerminal` reverse-request support
- `grep -rn "runInTerminal\|RunInTerminal" Source/` — zero matches.
- whatdbg always spawns the debuggee itself (`CreateProcess2` on Windows, `target.Launch` on macOS). It never delegates spawning to the DAP client.

## 3. Candidate Mechanisms (researched, cited — no recommendation implied by order)

### Option A — LLDB `eLaunchFlagLaunchInTTY`
- `lldb-enumerations.h:113-114` (LLVM 22.1.4, `/opt/homebrew/Cellar/llvm/22.1.4/include/lldb/lldb-enumerations.h`): `eLaunchFlagLaunchInTTY = (1u << 5), ///< Launch the process in a new TTY if supported by the host`.
- Not currently set anywhere in `Session_mac.cpp`.
- Effect if added: LLDB itself opens the new TTY. On macOS this is a **separate Terminal.app window, external to nvim** — not an editor split.
- Scope: one flag added to the existing `SetLaunchFlags` call at `Session_mac.cpp:279-280`.

### Option B — Implement DAP `runInTerminal` reverse request
- nvim-dap already implements the client side of this in full: `~/.local/share/nvim/lazy/nvim-dap/lua/dap/session.lua:234-294` (`run_in_terminal`).
  - `session.lua:239-254` — if `body.kind == 'external'` (or adapter forces it), launches a genuine external terminal process and reports back `processId`.
  - `session.lua:255-282` — otherwise, opens an nvim split via `terminals.acquire(settings.terminal_win_cmd, ...)` and runs the program inside it with `vim.fn.jobstart`/`termopen` — a real PTY inside an nvim buffer, i.e. TUI/ncurses raw-mode renders correctly.
- Effect if whatdbg implements the adapter side: whatdbg stops spawning the process itself, sends a `runInTerminal` reverse request with the program/args/cwd, and the DAP client (nvim-dap) spawns it and returns the PID. whatdbg then attaches to that PID — the same PID-attach pattern whatdbg already uses for `'Attach to DAW (VST3)'` on macOS (`dap/configurations.lua:525-527` in nvim config: `request = ... 'attach'`, `pid = getDawPid`).
- Where it runs from nvim: **inside an nvim split**, if `settings.terminal_win_cmd` default is used (integrated) — or an external terminal app, if `kind = 'external'` is requested instead. Client-side choice, no whatdbg-side branching needed beyond sending the request.
- Scope: larger than Option A — new reverse-request send/response in whatdbg's DAP layer, plus switching the macOS (and possibly Windows) launch path from direct spawn to request-then-attach.

## 4. Open Questions (unresolved — ARCHITECT to decide, not inferred here)

- Which mechanism, if either, should be implemented — Option A (simpler, external OS window), Option B (integrated nvim split, larger implementation), both (platform-conditional), or neither.
- Whether Windows should also gain a `runInTerminal`-based path (currently `CREATE_NEW_CONSOLE` already gives a working, separate console — Option B may be unnecessary there).
- Whether this applies to all launch configs or only a new dedicated "Launch CLI/TUI" DAP config, distinct from `'Launch Standalone'` (`dap/configurations.lua:478-519` in nvim config, currently used for JUCE GUI/standalone apps with no console field set either).
- Whether `eLaunchFlagCloseTTYOnExit` (`lldb-enumerations.h:129`) should also be set alongside `eLaunchFlagLaunchInTTY`, if Option A is chosen.

## 5. Non-Goals (this RFC)

- No implementation performed. No PLAN.md written. No code touched in `Source/`.
- No recommendation between Option A and B — both are correct-direction candidates; the tradeoff (external window vs. integrated split, implementation size) is ARCHITECT's call.
