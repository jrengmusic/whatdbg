# whatdbg

**WYSIWYG Hybrid Abstraction Translator for DAP Debug Adapter**

A cross-platform DAP debug adapter for C/C++ with neovim -- Windows and macOS, one tool, identical DX.

---

## Why whatdbg?

**Windows:** There is no reliable DAP debug adapter for Windows C/C++ that works with nvim + mason. codelldb is LLDB-based and struggles with MSVC PDBs. cppvsdbg is VS Code-only. cppdbg is closed-source and flaky outside VS Code. whatdbg uses **dbgeng** -- the same engine behind WinDbg -- so it reads MSVC PDBs natively, handles COM, and debugs anything Windows can run: standalone apps, DLL plugins loaded in host processes, services.

**macOS:** codelldb exists and works, but if you develop on both platforms you now carry two adapters with different configs, different quirks, and different debugging experiences. whatdbg uses **liblldb** -- the same engine behind Xcode's debugger -- and exposes the same DAP surface as the Windows backend. One config, both platforms.

---

## Features

Features apply to both Windows and macOS unless noted.

**Execution Control**
- Launch or attach to any process
- Source-level stepping: next (F10), step in (F11), step out
- Pause a running target (Windows: DebugBreakProcess / macOS: SBProcess::SendAsyncInterrupt via liblldb)
- Terminate kills the process, disconnect detaches cleanly

**Breakpoints**
- Set breakpoints before or after module load -- deferred resolution handles both
- Per-module symbol reload eliminates load storms during DAW/host startup (Windows)
- Breakpoints survive plugin reload (remove + re-add instance)

**Variable Inspection**
- Full locals panel with struct/class expansion
- Type pretty-printing: `juce::String` shows `"actual content"`, `std::vector` shows `size=N`, `std::unique_ptr` shows address or `null`
- Compiler internals filtered: no `leakDetector`, no `__vfptr`, no range-for temporaries

**Expression Evaluation**
- C++ expression evaluator in the DAP REPL
- Member access (`this->processor`), pointer dereference, arithmetic, casts, sizeof
- `juce::String` expressions auto-resolve to string content

**Multi-Thread**
- Thread enumeration with OS thread IDs. Windows resolves real thread names via `GetThreadDescription`; macOS reports `SBThread::GetName()`, which has no equivalent OS-level naming API behind it
- Click any thread to inspect its stack trace and locals
- Correct thread context for variables at any frame

**Debug Output Capture**
- Windows: `DBG()` and `OutputDebugString()` from the target appear in nvim-dap
- macOS: the target's own stdout/stderr streams are drained and forwarded to nvim-dap
- Engine noise filtered -- only debuggee output forwarded

**Architecture**
- Two-thread model on both platforms: main thread owns the debug engine + DAP, stdin thread is a FIFO buffer
- Sidecar debug engine embedded in the binary via JUCE BinaryData, extracted at runtime -- no external dependencies
- Debug-only file logging (`#if JUCE_DEBUG`) -- zero overhead in Release
- BLESSED-compliant: no bail-out guards, RAII resource management, named constants, dispatch table
- macOS termination kills and reaps the debuggee's exit through the normal event
  loop -- no unreapable zombie left behind after `terminate`

---

## Requirements

### Windows

**Runtime:**
- Windows 10+ (x64, ARM64)
- Neovim with [nvim-dap](https://github.com/mfussenegger/nvim-dap)
- Target built with MSVC and PDB symbols

**Build:**
- MSVC toolchain (Visual Studio 2022)
- CMake 3.25+
- Ninja
- JUCE 8
- `cast` build generator (`build-windows.sh`, generated, wraps vswhere + vcvarsall + cmake + ninja)

### macOS

**Runtime:**
- macOS 12+ Monterey (arm64, x86_64)
- Neovim with [nvim-dap](https://github.com/mfussenegger/nvim-dap)
- Target built with Xcode clang and DWARF symbols

**Build:**
- Xcode clang toolchain
- CMake 3.25+
- Ninja
- JUCE 8
- `cast` build generator, CMake + Ninja underneath
- liblldb.dylib built from LLVM source (see `build-liblldb.sh`)

---

## Get Started

`cast` builds whatdbg. It reads `project-info.md`, writes `CMakeLists.txt`, `Source/generated/ProjectInfo.h` and `build-windows.sh`, then runs the toolchain row you select. One command generates and builds.

Do not edit `CMakeLists.txt`. `cast` overwrites it.

### macOS

```bash
cast              # Release, signed and notarized
cast --debug      # Debug (file logging on)
cast --no-sign    # Release, unsigned
```

The build targets the machine's own architecture. CMake reads `uname -m`, then picks the matching sidecar `liblldb.dylib` from `Resources/macos/`.

The build installs the binary to `~/.local/bin/whatdbg`.

`liblldb.dylib` must exist first. Build it once:

```bash
./build-liblldb.sh
```

### Windows

```bash
cast --windows
```

The `windows` row runs the generated `build-windows.sh`. That script finds Visual Studio with `vswhere`, imports the MSVC environment from `vcvarsall.bat`, then runs cmake and ninja. It does all of this in one shell.

---

## nvim-dap configuration

The adapter type is `whatdbg` on both platforms. Platform detection is automatic -- the binary shipped for each OS uses the appropriate backend.

```lua
local dap = require("dap")

dap.adapters.whatdbg = {
  type = "executable",
  command = "whatdbg",
}

dap.configurations.cpp = {
  -- Windows: launch
  {
    name = "Launch (Windows)",
    type = "whatdbg",
    request = "launch",
    program = "${workspaceFolder}/build/MyApp.exe",
    cwd = "${workspaceFolder}",
  },
  -- Windows: attach
  {
    name = "Attach (Windows)",
    type = "whatdbg",
    request = "attach",
    pid = "${command:pickProcess}",
    cwd = "${workspaceFolder}",
  },
  -- macOS: launch
  {
    name = "Launch (macOS)",
    type = "whatdbg",
    request = "launch",
    program = "${workspaceFolder}/build/MyApp",
    cwd = "${workspaceFolder}",
  },
  -- macOS: attach
  {
    name = "Attach (macOS)",
    type = "whatdbg",
    request = "attach",
    pid = "${command:pickProcess}",
    cwd = "${workspaceFolder}",
  },
}
```

whatdbg reads `cwd` (source/symbol search path) and `pid` (attach target) from the
DAP arguments — `sourcePath`, `symbolPath`, and `processId` are not read by
`Source/WhatdbgHandlers.cpp` and have no effect if supplied.

### DAW plugin debugging (Windows -- JUCE + DAW)

```lua
{
  name = "Debug Plugin in DAW",
  type = "whatdbg",
  request = "launch",
  program = "C:/Program Files/DAW/yourDAWofChoice.exe",
  cwd = "${workspaceFolder}",
}
```

Launch DAW, load your plugin, set breakpoints in plugin code, hit them.

---

## Testing

`tests/smoke/` runs whatdbg end-to-end through an nvim-headless Lua DAP client
(`run_smoke.lua`, driving `scenario_breakpoint.lua`, `scenario_process.lua`, and
`scenario_terminate.lua` against three fixture binaries). Ten scenarios cover
launch+breakpoint, step, attach, pause, variables, evaluate, output, crash,
terminate-without-zombie, and disconnect-detach.

---

## For Architects and Engineers

- **[SPEC.md](SPEC.md)** -- Complete specification: features, user flows, edge cases, error handling
- **[ARCHITECTURE.md](ARCHITECTURE.md)** -- System design, threading model, engine lifecycle

---

Rock 'n Roll!

**JRENG!**

---
conceived with [CAROL](https://github.com/jrengmusic/carol)
