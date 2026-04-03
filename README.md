# whatdbg

**Windows Host Abstraction Translator for dbgeng**

A DAP debug adapter for Windows that actually works with neovim.

---

## Why whatdbg?

There is no reliable DAP debug adapter for Windows C/C++ that works with nvim + mason. codelldb is LLDB-based and struggles with MSVC PDBs. cppvsdbg is VS Code-only. cppdbg is closed-source and flaky outside VS Code.

whatdbg uses **dbgeng** -- the same engine behind WinDbg -- so it reads MSVC PDBs natively, handles COM, and debugs anything Windows can run: standalone apps, DLL plugins loaded in host processes, services.

---

## Features

**Execution Control**
- Launch or attach to any Windows process
- Source-level stepping: next (F10), step in (F11), step out
- Pause a running target via DebugBreakProcess
- Terminate kills the process, disconnect detaches cleanly

**Breakpoints**
- Set breakpoints before or after module load -- deferred resolution handles both
- Per-module symbol reload eliminates load storms during DAW/host startup
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
- Real thread enumeration with OS thread IDs and names
- Click any thread to inspect its stack trace and locals
- Correct thread context for variables at any frame

**OutputDebugString Capture**
- `DBG()` and `OutputDebugString()` from the target appear in nvim-dap
- Engine noise filtered -- only debuggee output forwarded

**Architecture**
- Two-thread model: main thread owns COM + DAP, stdin thread is a FIFO buffer
- Sidecar dbgeng DLLs embedded in the binary via JUCE BinaryData -- no external dependencies
- Debug-only file logging (`#if JUCE_DEBUG`) -- zero overhead in Release
- BLESSED-compliant: zero early returns, ComPtr RAII, named constants, dispatch table

---

## Requirements

**Runtime:**
- Windows 10+ (x64)
- Neovim with [nvim-dap](https://github.com/mfussenegger/nvim-dap)
- Target built with MSVC and PDB symbols

**Build from source (optional):**
- Visual Studio 2022 (MSVC toolchain)
- CMake 3.25+
- Ninja
- JUCE 8 (auto-fetched via FetchContent if not found locally)

---

## Get Started

### Build from source

```bash
build.bat Release
```

The binary lands at `Builds/Ninja/whatdbg_App_artefacts/Release/whatdbg.exe`.

Install to PATH:
```bash
./install.sh        # Release build + copy to ~/.local/bin
./install.sh debug  # Debug build (with file logging)
```

### nvim-dap configuration

```lua
local dap = require("dap")

dap.adapters.whatdbg = {
  type = "executable",
  command = "whatdbg",
}

dap.configurations.cpp = {
  {
    name = "Launch",
    type = "whatdbg",
    request = "launch",
    program = "${workspaceFolder}/build/MyApp.exe",
    sourcePath = "${workspaceFolder}",
    symbolPath = "${workspaceFolder}",
  },
  {
    name = "Attach",
    type = "whatdbg",
    request = "attach",
    processId = "${command:pickProcess}",
    sourcePath = "${workspaceFolder}",
    symbolPath = "${workspaceFolder}",
  },
}
```

### DAW plugin debugging (JUCE + DAW)

```lua
{
  name = "Debug Plugin in DAW",
  type = "whatdbg",
  request = "launch",
  program = "C:/Program Files/DAW/yourDAWofChoice.exe",
  sourcePath = "${workspaceFolder}",
  symbolPath = "${workspaceFolder}",
}
```

Launch DAW, load your plugin, set breakpoints in plugin code, hit them.

---

## For Architects and Engineers

- **[SPEC.md](SPEC.md)** -- Complete specification: features, user flows, edge cases, error handling
- **[ARCHITECTURE.md](ARCHITECTURE.md)** -- System design, threading model, COM lifecycle
- **[PLAN.md](PLAN.md)** -- Development history and design decisions

---

Rock 'n Roll!

**JRENG!**

---
conceived with [CAROL](https://github.com/jrengmusic/carol)
