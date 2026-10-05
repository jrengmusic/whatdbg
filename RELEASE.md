# whatdbg v0.1.0

DAP adapter for Windows DbgEng / macOS liblldb

## What's New

- Release lane: a signed macOS pkg and Windows x64 and arm64 installers, uploaded to the GitHub release.
- First cross-platform release. macOS support via liblldb sidecar, matching full Windows feature parity.
- Launch & Attach -- debug any process by launch or PID attach
- Breakpoints -- deferred resolution, pending breakpoints, per-module symbol reload
- Stepping -- source-level next, step in, step out
- Pause -- break into a running target
- Variable Inspection -- full locals panel, struct/class expansion, pretty-printing for juce::String, std::string, std::unique_ptr, std::vector
- Expression Evaluation -- C++ expressions in DAP REPL (member access, pointer dereference, casts, sizeof)
- Multi-Thread -- thread enumeration with names, per-thread stack trace and locals
- Debug Output Capture -- OutputDebugString/DBG() forwarded to nvim-dap
- Exception Surfacing -- crash info with exception code, address, and name
- Terminate / Disconnect -- kill or detach cleanly

## Platforms

| Platform | Architecture       | Format                            |
| -------- | ------------------ | --------------------------------- |
| macOS    | arm64              | .pkg (signed, notarized)          |
| macOS    | x86_64             | .pkg (signed, notarized)          |
| Windows  | x64                | .exe installer                    |
| Windows  | arm64              | .exe installer                    |

## Installation

macOS: open the .pkg for your Mac's architecture. It installs `whatdbg` on your PATH.

Windows: run the installer. It installs `whatdbg.exe` and adds its folder to your user PATH.
