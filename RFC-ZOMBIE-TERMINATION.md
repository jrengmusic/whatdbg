# RFC: Debuggee Becomes an Unreapable Zombie on macOS terminate/disconnect

**Status:** Implemented — the fix described in §5 shipped this sprint.
**Origin:** ARCHITECT/MACHINIST session debugging nvim's `<leader>dt` (`core/build.lua` in the
`~/.config` monorepo) reported a debuggee GUI dangling in the Dock after termination. Root cause
traced into whatdbg itself. Filed here per RFC Fidelity Protocol. A separate, unrelated RFC
(`RFC.md`, TUI/CLI terminal attachment) already occupies the canonical RFC.md path in this repo —
this is a new file to avoid clobbering that draft.

---

## 1. Problem

After a DAP `terminate`/`disconnect` request ends a **launched** (not attached) macOS debug
session, the debuggee process is confirmed killed at the kernel level (`SIGKILL` delivered,
process leaves the run queue) but **never reaches a reaped state** — it becomes a permanent
zombie (`ps` shows `STAT=Z`, `<defunct>`), reparented to `launchd` (`PPID=1`), and `launchd` does
not reap it. Externally visible symptoms: the debuggee's Dock icon / App Switcher entry (backed
by `NSWorkspace.runningApplications`/`NSRunningApplication`, which reports `isTerminated=false`
for the zombie) never disappears, indefinitely, until the user logs out or reboots.

## 2. Root Cause (cited)

### 2.1 The kill path — `Source/debug/Session_mac.cpp:371-398`

```cpp
void Session::shutdown (EndMode mode) noexcept
{
    switch (mode)
    {
        case EndMode::terminate:
        {
            const auto pid { State::getContext ()->targetProcessId };

            if (pid != 0)
            {
                ::kill (static_cast<pid_t> (pid), SIGKILL);   // line 381
            }
            break;
        }
        case EndMode::detach:
            if (process.IsValid ())
            {
                process.Detach ();
            }
            break;
        case EndMode::passive:
        default:
            break;
    }

    lldb::SBDebugger::Destroy (debugger);   // line 396 — no wait for exit before this
    lldb::SBDebugger::Terminate ();
}
```

Two defects, both on the `EndMode::terminate` path:

1. **Line 381** kills the debuggee with a raw POSIX `::kill(pid, SIGKILL)` — bypassing LLDB's own
   `SBProcess` API entirely (`SBProcess::Kill()`/`SBProcess::Destroy()` are never called; `process`
   the `SBProcess` member is only used on the `detach` branch, line 386-389).
2. **Line 396** calls `lldb::SBDebugger::Destroy(debugger)` **immediately** after issuing the
   signal — no poll of `SBProcess::GetState()` for `eStateExited`, no registered
   `SBListener`/`eBroadcastBitStateChanged` wait, no `waitpid()`-equivalent of any kind. The
   debugger (and, transitively, its `debugserver` connection — see §2.3) is torn down before the
   kernel has necessarily finished collecting the debuggee's exit status.

### 2.2 The DAP handler that triggers it — `Source/WhatdbgHandlers.cpp:139-150`

```cpp
void Whatdbg::handleDisconnect (const juce::var& request)
{
    const int seq { static_cast<int> (request["seq"]) };
    const juce::String command { request["command"].toString () };
    const juce::var& args { request["arguments"] };

    const bool isTerminate { command == "terminate" };
    shouldTerminateOnExit = isTerminate or static_cast<bool> (args["terminateDebuggee"]);

    sendResponse (dap::makeResponse (seq, command, true));
    isRunning = false;
}
```

Both DAP `terminate` and `disconnect` route here. The response is sent, and the main loop is
signaled to exit, **before** `shutdown()` ever runs — `shutdown(EndMode::terminate)` is called
afterward, once the main loop unwinds:

### 2.3 Where `shutdown` actually fires — `Source/Whatdbg.cpp:130-138`

```cpp
    reader.stop ();
    debug::EndMode endMode { debug::EndMode::detach };

    if (state.executionState == debug::ExecutionState::exited)
        endMode = debug::EndMode::passive;
    else if (shouldTerminateOnExit)
        endMode = debug::EndMode::terminate;

    session.shutdown (endMode);
```

Process tree at the point `shutdown()` runs (confirmed empirically, see §3):
`whatdbg` → `liblldb` (embedded, spawns/owns) → `debugserver` (macOS platform connection,
`Session_mac.cpp:268-291`, `target.Launch(launchInfo, error)`) → `debuggee`. `SBDebugger::Destroy`
tears down `liblldb`'s state including its `debugserver` connection — `debugserver`, the
debuggee's actual ptrace/Mach-task tracer, goes away in the same window as the raw `SIGKILL`, with
nothing left alive that ever called `wait()`/polled state on the debuggee.

## 3. Empirical Confirmation (headless, this session)

Two independent, convergent tests — full methodology available on request:

1. **Bare `dap.terminate()`** from nvim (zero client-side interference — whatdbg's own
   `handleDisconnect` → `shutdown(EndMode::terminate)` path runs exactly as shipped): debuggee
   reached `STAT=Z`, `PPID=1`, unreaped after 6+ seconds of polling.
2. **Minimal reproduction with zero whatdbg code involved**: a hand-written C program launched
   under plain `lldb -b -o "process launch"`, then killed via `kill -9 <debuggee_pid>` followed by
   `kill -9 <debugserver_pid>` (mirroring what whatdbg's own kill effectively does — sever the
   debuggee and tear down the tracer near-simultaneously) — same permanent-zombie result. This
   confirms the defect is not specific to any one caller's kill sequence; it's the general
   consequence of severing/tearing down the tracer without reaping the tracee first.

## 4. External Research (cited)

- **macOS launchd does not reap zombies once it becomes the parent** — a documented limitation,
  not a transient timing issue: <https://discussions.apple.com/thread/250681170> ("If launchd
  becomes the parent of a zombie process, launchd will NOT reap it — your only recourse is to
  reboot.")
- **macOS has no Linux-style automatic tracer-detach.** Linux's `ptrace(2)` guarantees "if the
  tracer dies, all tracees are automatically detached and restarted"
  (<https://man7.org/linux/man-pages/man2/ptrace.2.html>). No equivalent is documented for XNU —
  the tracer (`debugserver`) dying does not hand the tracee back to a clean, waitable state on its
  own.
- **`debugserver`'s own lifecycle already encodes the correct pattern** — cited by the research
  pass against `llvm/llvm-project`'s `lldb/tools/debugserver/source/debugserver.cpp`: on its
  communication thread exiting, it explicitly branches between
  `DNBProcessDetach(pid)`/`DNBProcessKill(pid)` — i.e. `debugserver` itself treats "kill" and
  "leave running" as distinct, deliberate operations, not a fire-and-forget signal.
- **A ptrace-attached process's signal delivery can be gated on the tracer's exception-port
  bookkeeping** — <https://www.spaceflint.com/?p=150> notes that abrupt tracer death can leave a
  tracee's threads "blocked waiting for exception replies," and that clean detachment requires
  "the saved exception port information" to be explicitly restored before cleanup — not simply
  abandoned.

## 5. Candidate Mechanisms (researched, cited — no recommendation implied by order)

### Option A — Poll `SBProcess::GetState()` after the kill, before `Destroy()`

- Send the kill (ideally via `process.Kill()`, see Option B, or keep the raw `::kill()` at
  `Session_mac.cpp:381` if Option B proves insufficient), then loop
  `process.GetState() == lldb::eStateExited` with a bounded timeout (e.g. via
  `juce::Thread::sleep` polling, mirroring the idle-poll pattern already used at
  `Whatdbg.cpp:126`) before line 396's `SBDebugger::Destroy(debugger)` runs.
- Scope: localized to `Session::shutdown`'s `EndMode::terminate` case, `Session_mac.cpp:375-384`.
- Risk: a bounded timeout still needs a decision for the "didn't exit in time" case (§6).

### Option B — Use `SBProcess::Kill()` instead of raw `::kill()`

- `process` (the `SBProcess` member) is already live and valid at this point in `terminate` mode
  (it's used identically in the `detach` branch two lines below, `Session_mac.cpp:386-389`) — no
  new state needed to reach it.
- `SBProcess::Kill()` operates through LLDB's own process state machine rather than bypassing it
  with a bare signal; whether it internally waits for the exit event is not confirmed by this
  RFC's research pass — worth checking `lldb::SBProcess::Kill`'s implementation in the LLVM tree
  used by this build (`/opt/homebrew/Cellar/llvm/...`, version confirmed at RFC.md:33 as `22.1.4`)
  before relying on it alone.
- Likely still needs pairing with Option A's poll — `SBProcess::Kill()` sends the kill and may
  return before the exit is collected, same class of problem as `::kill()` alone, just through the
  "correct" API surface.

### Option C — Register an `SBListener` for the process state-changed event before killing

- Subscribe to `eBroadcastBitStateChanged` on `process` before issuing the kill (either path),
  then block (bounded) on the listener for an `eStateExited` event instead of a raw poll loop —
  the event-driven equivalent of Option A, using LLDB's own notification mechanism instead of
  hand-rolled polling.
- Scope: touches `Session::shutdown` plus wherever `SBListener`/broadcaster wiring already exists
  for this `SBDebugger`/`SBTarget` (not located by this RFC's research pass — needs a follow-up
  Pathfinder/Librarian check of existing listener setup in `Session_mac.cpp`/`Session.h` before
  scoping this option further).

## 6. Open Questions (unresolved — ARCHITECT to decide, not inferred here)

- Which mechanism, if any — A, B, C, or A+B combined — should be implemented.
- What `shutdown()` should do if the bounded wait/poll times out without observing
  `eStateExited`: proceed to `Destroy()` anyway and accept the zombie (status quo for the timeout
  case only), retry the kill, or surface a diagnostic?
- Whether the same defect exists on the `EndMode::detach` path (`Session_mac.cpp:385-390`,
  `process.Detach()`) — this RFC's empirical testing covered `terminate` only; `detach` was not
  independently verified to be clean or affected.
- Whether `Session.cpp` (Windows, dbgeng) has an analogous gap — out of scope for this RFC (macOS
  ptrace/zombie semantics do not apply to dbgeng's out-of-process model), but worth a companion
  check if Windows termination is ever reported to leave a process running or a session in a bad
  state.
- Whether a bounded-wait fix belongs in `Session::shutdown` synchronously (blocking the DAP
  response/main-loop teardown for up to the timeout) or should be moved off the path that
  `Whatdbg.cpp:138` calls synchronously today.

## 7. Shipped Fix and Deviation from §5

None of Options A, B, or C shipped as originally scoped. The implemented design
kills early and observes the exit through the existing main-loop poll instead of
adding a dedicated wait/timeout:

- **`Session::terminateDebuggee` (`Source/debug/Session_mac.cpp`), called from
  `Whatdbg::onDisconnect`:** sends `SIGKILL` via `process.Signal` (closer to
  Option B's intent than Option B's literal `SBProcess::Kill()`, which was never
  called), then calls `Session::resume()` — this hands the exit observation back
  to `pollEvents`'s existing `onProcessStateStopped` dispatch, the same path
  every other stop event already uses, instead of the polling or listener loop
  Options A and C proposed adding to `shutdown` itself.
- **`Session::shutdown`'s `EndMode::terminate` case:** performs the identical
  `Signal (SIGKILL)` + `resume()` pair as a fallback for the case where
  `shutdown` runs without a prior `onDisconnect` kill (e.g. `run()`'s own exit
  path). `SBDebugger::Destroy`/`Terminate` now run only when `debugger.IsValid()`
  is still true, making a second `shutdown()` call a no-op — `run()` calls it
  explicitly, and `~Session()` calls it again unconditionally.
- **No bounded wait/timeout was added to `shutdown`** (§6's open question on
  timeout behavior). The kill is issued from `onDisconnect`, well before `run()`
  reaches `shutdown()` — by the time `shutdown` runs, `run()`'s own loop has
  already been polling for the exit event via the normal `executionState ==
  running` gate, so a separate wait inside `shutdown` was unnecessary.
- **`detach` was not touched** — §6 flagged it as unverified; it remains
  `process.Detach()` with no kill involved, so the zombie mechanism (kill +
  torn-down tracer with no reap) does not apply to it.

Verified by `tests/smoke/scenario_terminate.lua`'s `scenarioTerminateNoZombie`,
one of the ten scenarios `run_smoke.lua` runs; all ten pass.

## 8. Non-Goals (superseded)

The original scope statement — "No implementation performed, no PLAN.md written,
no code touched in `Source/`" — described the state of this RFC before the fix
above shipped. It no longer applies.
