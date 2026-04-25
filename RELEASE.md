# whatdbg v0.0.4

Cross-platform DAP debug adapter for C/C++ with neovim -- Windows (dbgeng) and macOS (liblldb), one tool, identical DX.

## What's New

First cross-platform release. macOS support via liblldb sidecar, matching full Windows feature parity.

## Features

- **Launch & Attach** -- debug any process by launch or PID attach
- **Breakpoints** -- deferred resolution, pending breakpoints, per-module symbol reload
- **Stepping** -- source-level next, step in, step out
- **Pause** -- break into a running target
- **Variable Inspection** -- full locals panel, struct/class expansion, pretty-printing for juce::String, std::string, std::unique_ptr, std::vector
- **Expression Evaluation** -- C++ expressions in DAP REPL (member access, pointer dereference, casts, sizeof)
- **Multi-Thread** -- thread enumeration with names, per-thread stack trace and locals
- **Debug Output Capture** -- OutputDebugString/DBG() forwarded to nvim-dap
- **Exception Surfacing** -- crash info with exception code, address, and name
- **Terminate / Disconnect** -- kill or detach cleanly

## Platforms

| Platform | Architecture | Format |
|----------|-------------|--------|
| Windows 10+ | x64 | .zip (executable) |
| macOS 12+ | arm64 | .pkg (signed, notarized) |
| macOS 12+ | x86_64 | .pkg (signed, notarized) |

## macOS Installation

Double-click the `.pkg` -- installs to `/opt/whatdbg/` with symlink to `~/.local/bin/whatdbg`. Add `~/.local/bin` to your PATH if not already present.

## Windows Installation

Extract `whatdbg.exe` from the zip to a directory on your PATH (e.g. `~/.local/bin/`).

## Requirements

- Neovim with [nvim-dap](https://github.com/mfussenegger/nvim-dap)
- Windows: target built with MSVC + PDB symbols
- macOS: target built with Xcode clang + DWARF symbols

## Configuration

See [README.md](https://github.com/jrengmusic/whatdbg#nvim-dap-configuration) for nvim-dap setup.
