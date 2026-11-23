# RFC — macOS adapter crashes in CreateTarget on a large Debug binary
Date: 2026-11-23
Status: Ready for COUNSELOR handoff
Author: COUNSELOR (END session)
Targets: `Source/debug/Session_mac.cpp`

## Problem Statement

The `whatdbg` MCP `launch` tool cannot start a session for the Debug END binary on macOS.

- Target: `/Users/jreng/Documents/Poems/dev/end/Builds/Debug/END_artefacts/Debug/END.app/Contents/MacOS/END` (115 MB, from a Pathfinder `ls`).
- Each `launch` call returned only the tool error `session ended`. No session handle came back and no event came back.
- The END session needs a `whatdbg` trace of a Debug END-hosted EVE on macOS. ARCHITECT: *"if you cant trace with whatdbg, you are no useful"*.
- ARCHITECT's scope rule for this work: *"if this whatdbg defects we must fix it, i will open parallel session to fix it. ONLY when it genuienely need to be fixed"*.

It is a genuine defect. The adapter process crashes in its own address space, inside the `liblldb.dylib` that `whatdbg` ships, on each try (Research Summary, items 3 to 6).

## Research Summary

1. **Meaning of `session ended`.**
   - The MCP server starts its own executable as the DAP adapter child (`Source/ToolServer.cpp:149`).
   - It uses `jam::StdioClient` for the child (`Source/Session.cpp:39-44`).
   - When the child exits, `client.onExit` sets `isOpen` to 0 (`Source/Session.cpp:34-37`).
   - `Session::setClosed` then fails every pending request with `sessionEndedMessage { "session ended" }` (`Source/Session.cpp:157-178`).
   - So the error means that the adapter child exited before it replied to `launch`. It does not mean that the debuggee exited.
2. **Single-instance ruled out.** END returns `true` from `ENDApplication::moreThanOneInstanceAllowed()` (`end/Source/Main.cpp:228`).
3. **Crash reports.** `~/Library/Logs/DiagnosticReports/` holds three adapter reports from 2026-11-23, one for each launch try:
   - `whatdbg-2026-11-23-220406.ips`
   - `whatdbg-2026-11-23-220634.ips`
   - `whatdbg-2026-11-23-220714.ips`
4. **Crash, from `whatdbg-2026-11-23-220714.ips`:**
   - `"parentProc" : "whatdbg"`, `"coalitionName" : "com.JRENG.END"` (`:26-28`).
   - `"exception" : {... "type":"EXC_BAD_ACCESS","signal":"SIGBUS","subtype":"KERN_PROTECTION_FAILURE at 0x000000016c197fe0"}` (`:44`).
   - `"termination" : {... "indicator":"Bus error: 10" ...}` (`:45`).
   - `vmRegionInfo`: the fault address is in a `Stack Guard` region, under a `Stack` region of `[  544K]` (`:43`). This is a stack overflow on a thread with a 544 KB stack.
   - `"faultingThread" : 11` (`:48`). That thread is `"name":"llvm-worker-0"`, `"triggered":true` (`:49`).
   - All frames of `llvm-worker-0` are in image index 1, `liblldb.dylib` (`:49`, `:60-67`).
   - One frame at `imageOffset` `3997676` repeats: `"recursionInfoArray":[{... "depth":43707, "keyFrame":{"imageOffset":3997676,"imageIndex":1}}]` (`:49`). The recursion is under `std::__1::__call_once` (`:49`).
   - The threads `llvm-worker-2`, `-3`, `-5`, `-6` and `-9` show the same key frame, at depths 26018, 38269, 33288, 27067 and 24738 (`:49`).
   - Thread 1 waits in `lldb::SBDebugger::CreateTarget(char const*)` (`:49`).
5. **The other two reports.**
   - `220406.ips:49` shows `SBDebugger::CreateTarget`, and recursion depths of 43707, 19558, 28176, 28534, 23398 and 27821.
   - `220634.ips:49` shows recursion depths of 43703 and 21433, and no `SBDebugger` frame.
6. **Call site.** `Session::launch` calls `debugger.CreateTarget (program.toRawUTF8 ())` first (`Source/debug/Session_mac.cpp:249`). `Session::attach` calls `debugger.CreateTarget ("")` before `AttachToProcessWithID` (`Source/debug/Session_mac.cpp:395-404`).
7. **The vendored liblldb.**
   - `Resources/macos/arm64/liblldb.dylib`, 60753280 bytes, dated Nov 12.
   - `dwarfdump --uuid`: `3CA11F73-185E-37CE-896C-C8C263F7CE5C`, which matches the crash image UUID (`220714.ips:65`).
   - `strings`: `lldb version 21.1.8`.
   - Its internal symbols are stripped. `atos -l 0x10D29C000` gave image offsets only for all 38 crash frames. It gave a name only for an exported address (`lldb::SBDebugger::GetTargetAtIndex(unsigned int)`). The recursing function has no name in this build.
8. **Symbol-loading settings in this liblldb (`strings`).**
   - `preload-symbols`: *"Enable loading of symbol tables before they are needed."*
   - `load-on-demand`: *"Enable on demand symbol loading in LLDB. LLDB will load debug info on demand for each module based on various conditions (e.g. matched breakpoint, resolved stack frame addresses and matched global variables/function symbols in symbol table) to improve performance."*
   - Also present: `parallel-module-load`, `ignore-file-indexes`, `auto-download`, `enable-background-lookup`.
   - No string names a thread stack-size setting.
9. **Settings that whatdbg sets today.**
   - `Session::initialize` sets one setting before any target, `target.process.stop-on-exec` = `false`, through `lldb::SBDebugger::SetInternalVariable` (`Source/debug/Session_mac.cpp:228-233`).
   - The only other setting calls are `HandleCommand` at `:622` (`appendSymbolPath`) and `:631` (`appendSourcePath`).
   - `SBDebugger::Create (false)` reads no `~/.lldbinit` (`Source/debug/Session_mac.cpp:225`). So a user setting cannot change this behaviour.
10. **History.** No sprint log in `whatdbg` or END records a macOS `whatdbg` launch or attach of END. The END log records Windows traces only: `end/carol/SPRINT-LOG.md:678-681` (`WindowsHooks`, `vstHookWndProc`), and `:181`, `:195` (Sprint 106, "Windows END Hosts EVE").

## Principles and Rationale

- **Use the framework API fully (CODING.md CRITICAL RULES).** liblldb has a setting for when symbols load (Research Summary, item 8). Set it through the same SB call that whatdbg already uses (item 9). Do not write a new mechanism.
- **Inventing a new pattern where one exists (MANIFESTO Anti-Patterns, E).** The `stopOnExec` lines at `Session_mac.cpp:228-233` are the established family. The fix joins that family.
- **Explicit (E).** The setting name and value are named `static constexpr` values, as the sibling is. The `SBError` result is asserted at the owner, as the sibling is.
- **Deterministic (D).** The failure repeats on each try (items 3 to 5). The cause is the symbol load in `CreateTarget`, which is in whatdbg's control through a setting. It is not a race.
- **Considered and rejected:**
  - A bigger worker-thread stack. liblldb exposes no stack-size setting (item 8), and the worker threads are liblldb's own threads (item 4). whatdbg does not own their creation.
  - A system `lldb` run to test the setting first. CAROL.md Debugging forbids raw lldb.
  - A user `~/.lldbinit`. `SBDebugger::Create (false)` does not read it (item 9).

## Scaffold

The established sibling, `Source/debug/Session_mac.cpp:228-233`:

```cpp
    static constexpr const char* stopOnExecVariable { "target.process.stop-on-exec" };
    static constexpr const char* stopOnExecValue { "false" };

    const lldb::SBError stopOnExecResult { lldb::SBDebugger::SetInternalVariable (stopOnExecVariable, stopOnExecValue, debugger.GetInstanceName ()) };
    jassert (stopOnExecResult.Success ());
    juce::ignoreUnused (stopOnExecResult);
```

The fix goes directly after the sibling, in `Session::initialize`, before any `CreateTarget`. Only the setting name and the identifiers change:

```cpp
    static constexpr const char* preloadSymbolsVariable { "target.preload-symbols" };
    static constexpr const char* preloadSymbolsValue { "false" };

    const lldb::SBError preloadSymbolsResult { lldb::SBDebugger::SetInternalVariable (preloadSymbolsVariable, preloadSymbolsValue, debugger.GetInstanceName ()) };
    jassert (preloadSymbolsResult.Success ());
    juce::ignoreUnused (preloadSymbolsResult);
```

Verification run, after ARCHITECT rebuilds and installs `whatdbg`:

1. Call MCP `launch` with `program` set to the Debug END path from the Problem Statement.
2. Expect a `{session}` result, not `session ended`.
3. Call `configurationDone`, then `events` with a bounded `timeoutMs`.
4. Expect an `initialBreak` stop (`Session_mac.cpp:128-131`), not a new `whatdbg-*.ips` report.
5. Call `attach` on a running Debug END pid, because `attach` also calls `CreateTarget ("")` (`:395`).

## BLESSED Compliance Checklist
- [x] Bounds — the setting belongs to the debugger instance that `Session` owns (`debugger`, `:225`).
- [x] Lean — five lines in one function. No new function, no new file.
- [x] Explicit — named constants. The `SBError` is asserted.
- [x] SSOT — one setting, at the one place where the debugger is set up.
- [x] Stateless — no new member and no flag.
- [x] Encapsulation — the setting goes through liblldb's own API.
- [x] Deterministic — the setting removes the repeating failure path, if the verification run confirms it (Open Question 1).

## Open Questions

1. **Is the recursion in the preload step?** The frames are stripped (Research Summary, item 7). Two settings are available: `target.preload-symbols` = `false` (the scaffold), or `symbols.load-on-demand` = `true` (item 8). The verification run in the Scaffold settles it. Start with `preload-symbols` alone. If a crash report still appears, add `symbols.load-on-demand` in the same family. COUNSELOR in the whatdbg session settles it from the run.
2. **Does the Windows backend need the same change?** This RFC covers macOS only: all the evidence is from `Session_mac.cpp` and the arm64 liblldb. The Windows backend uses dbgeng (`Session.cpp`), not liblldb. ARCHITECT settles the scope.

## Handoff Notes

- The END session waits for this fix. It needs a `whatdbg` pause-and-stack read of Debug END during `time seq 1 1000000` on macOS. The output stays blank until `seq` ends, then the prompt and the history show together.
- The installed adapter is `/Users/jreng/.local/bin/whatdbg`, 89 MB, dated 23 Nov 00:27 (`which -a whatdbg`). The MCP config runs `"command": "whatdbg", "args": ["--mcp"]` (`~/.claude.json:7256-7260`).
- Read every crash report with the Read or Glob tool. The `ls` on this machine is aliased and rejects `ls -lt <dir>`.
- Builds run only through `cast` (CAROL.md). ARCHITECT builds.
