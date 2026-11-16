# whatdbg — CLAUDE.md

**JRENG Cognitive Amplifier Synthesis — Project CLAUDE**

---

## Project Identity

**Name:** whatdbg  
**Full Name:** WYSIWYG Hybrid Abstraction Translator for DAP Debug Adapter  
**Type:** DAP (Debug Adapter Protocol) debug adapter — console application, cross-platform  
**Version:** 0.0.1  
**Repository:** https://github.com/jrengmusic/whatdbg  
**Root Directory:** `/Users/jreng/Documents/Poems/dev/whatdbg`

**Technology Stack:**
- **Language:** C++17. JUCE modules: juce_core, juce_data_structures, juce_events, juce_graphics, juce_gui_basics, juce_audio_processors. JAM modules: jam_core, jam_document, jam_markdown, jam_freetype, jam_graphics, jam_data_structures, jam_animation, jam_web, jam_style, jam_gui, jam_mermaid_diagram, jam_markdown_graphics, jam_terminal, jam_tui (`project-info.md`)
- **Build System:** `cast` generates `CMakeLists.txt` + `Source/generated/ProjectInfo.h` from `project-info.md`. CMake + Ninja underneath. `CMakeLists.txt` is generated — never edit it.
- **Platforms:** Windows 10+ (x64, ARM64) / macOS 12+ Monterey (arm64, x86_64)
- **Debug Engines:** Windows dbgeng COM API / macOS liblldb SB API (both sidecar-embedded as BinaryData)
- **Protocol:** Debug Adapter Protocol over stdin/stdout

---

## Current State

**Last Sprint:** Audio architecture discipline rewrite (PLAN-audio-architecture-discipline-0..7):
`Whatdbg` is a `juce::JUCEApplicationBase` on the JUCE message loop; `WHATDBGModel`
(`jam::Model`) holds the session state; `Processor` (`jam::AsyncUpdater`) owns the engine
atoms (`Session`, `BreakpointManager`, `VariableManager`, `StopManager`) and the
`requests` mailbox and `jam::StdioServer`; `debug::State` class and the hand-written loop deleted;
`--help` paints `Source/HELP.md` through `jam::TerminalPage` (`jam_tui`); `tests/smoke/`
extended to 21 scenarios with transcript baselines  
**Date Completed:** 2026-11-14  
**Status:** macOS verified via `tests/smoke/`. Windows: the conformance sprint (`PLAN-windows-conformance-audit.md`) gives `tests/smoke` 22/22 in two runs against `baseline/windows/` (2026-11-15). macOS: `tests/smoke` 22/22 against `baseline/macos/`, `22_run_in_terminal` included (2026-11-16).

**Active ODE:** None  
**Active Debt:** None.

**Critical Path:**
- DAP command dispatch table in `Processor` (`jam::Function::Map`, O(1) lookup)
- Threads, one way: stdin → readiness context → `jam::StdioServer` input ring → engine context → `addMessage` → output ring → stdout. Model parameters flow from the engine context to the message thread. The stdin callback wakes the engine context. The `Session` stream callbacks only notify
- Cross-platform Session abstraction with Windows COM (dbgeng) and macOS liblldb (SB API) backends
- Sidecar binary extraction + embedding (BinaryData) with per-arch macOS support
- Event-driven pipe readiness through `jam::AsyncOutputStream`; the engine context reads each pipe into a `jam::TerminalFIFO` (`writeFromTerminalTTY (Handle)`): the Windows debuggee stdout and stderr, the macOS debuggee stderr (a FIFO), and the `runInTerminal` comm channel on both platforms
- `runInTerminal`: whatdbg in launcher mode (`Launcher`, `--comm-file`, `--launch-target`) runs the program in the client's terminal, and the adapter attaches to it

---

## Layer Order & Key Source Directories

```
Whatdbg (juce::JUCEApplicationBase, message thread; Model listener, isRunning quit)
  ├─ WHATDBGModel (jam::Model; SESSION and EXCEPTION rows)
  ├─ Processor (jam::AsyncUpdater engine context; commands table, command bodies, process)
  │    ├─ debug::Session (cross-platform debug engine wrapper)
  │    │    ├─ [Windows] Session.cpp / SessionPrettyPrint.cpp / Callbacks.cpp / Loader.cpp
  │    │    └─ [macOS]   Session_mac.cpp / SessionPrettyPrint_mac.cpp
  │    ├─ debug::BreakpointManager (breakpoint registry)
  │    ├─ debug::VariableManager (frameId and variablesReference registry)
  │    ├─ debug::StopManager (stops table, stop drains)
  │    ├─ jam::Mailbox requests
  │    └─ jam::StdioServer stdio (input and output rings, stdin readiness; the last member)
dap::Types (DAP message builders over jam::JsonWriter)
Launcher (launcher mode of whatdbg for runInTerminal; Main.cpp dispatches it on --comm-file and --launch-target)
debug::State.h (enums ExecutionState, StopReason; alias Events)
```

**Key Source Directories:**

| Directory | Purpose |
|-----------|---------|
| `Source/` | Entry point (`Main.cpp`), main (`Whatdbg.h/.cpp`), Model (`WHATDBGModel.h/.cpp`), engine context (`Processor.h/.cpp`), `runInTerminal` launcher (`Launcher.h/.cpp`), help page (`HELP.md`) |
| `Source/debug/` | Debug engine layer: `Session.h`, `Session.cpp`, `Session_mac.cpp`, `SessionPrettyPrint.cpp`, `SessionPrettyPrint_mac.cpp`, `PrettyPrint.h`, `State.h`, `BreakpointManager`, `VariableManager`, `StopManager`, `Callbacks` and `Loader` (Windows) |
| `Source/dap/` | DAP protocol layer: `Types` (message builders). The stdio frames are `jam::StdioServer` over two `jam::TerminalFIFO` rings, both in jam_terminal |
| `Source/generated/` | Generated by cast: `ProjectInfo.h`, `Generated.h/.cpp`, `Identifiers.h`, `Files.h` |
| `benchmark/` | scout manifests and launchers (`smoke.md`/`smoke.sh`, `smoke-windows.md`/`smoke.cmd`) and the timing reports |
| `Resources/windows/{x64,arm64}/` | Sidecar dbgeng DLLs (dbgeng.dll, dbghelp.dll, dbgcore.dll, symsrv.dll) |
| `Resources/macos/{arm64,x86_64}/` | Sidecar liblldb.dylib per architecture (built by `build-liblldb.sh`, versioned via Git LFS) |
| `Resources/macos/include/lldb/API/` | liblldb SB API headers (pinned, vendored) |
| `tests/smoke/` | DAP smoke test suite: `run_smoke.lua` orchestrator, `scenario_breakpoint.lua`, `scenario_process.lua`, `scenario_terminate.lua`, `scenario_inspect.lua`, `scenario_terminal.lua`, `scenario_runner.lua`, `dap_client.lua`, `report.lua`, `spell.md` (cast builds the fixtures into `build/`), plus fixtures `fixture.cpp`, `fixture_wait.cpp`, `fixture_crash.cpp`, `fixture_module.cpp`, `fixture_load.cpp`. 22 scenarios (`01`–`22`), each compared with its transcript in `baseline/<platform>/`. `run_smoke.lua` takes `<whatdbg> <fixture> <fixture_wait> <fixture.cpp> <fixture_crash> [--record]` as positional args under `nvim --headless -l`. |
| `cast/` | `spell.md` (output rows, toolchain wiring), `cmake.cast` (CMakeLists template), `identifiers.md` (`Id::` vocabulary), `files.md` (BinaryData file names) |
| `build-liblldb.sh` | Pinned LLVM build — writes `Builds/liblldb` and `Resources/macos/` |
| `build.sh` | Escape hatch, a copy of `cast/build.sh`: on Windows it imports the vcvarsall x64 environment, then runs cmake and ninja (Release) |
| `cast/installer.cast` | Installer templates: `distribution.xml` (macOS pkg), `installer.nsi` (Windows NSIS), `RELEASE.md` |
| `cast/installer/resources/` | Installer artwork: `background.png`, `background-dark.png`, `header.bmp`, `welcome.bmp` |

---

## Key Documentation

| Document | Purpose | Status | Last Updated |
|----------|---------|--------|--------------|
| **ARCHITECTURE.md** | System design, threading model, module map, data flow, design decisions, glossary | Current (v0.8.0) | 2026-11-16 |
| **SPEC.md** | Feature specification, 11 DAP features (launch, attach, breakpoints, stepping, pause, variables, evaluation, output, multi-thread, terminate, exception info) | Complete (v1.0) | 2026-11-14 |
| **project-info.md** | Build manifest — modules, architectures, toolchain rows, signing, BinaryData. `cast` reads it. | Active | 2026-11-14 |
| **cast/spell.md** | Output rows and toolchain wiring for `cast` | Active | 2026-11-14 |
| **CMakeLists.txt** | GENERATED by `cast` from `cast/cmake.cast`. Never edit by hand. | Generated | 2026-11-14 |
| **carol/SPRINT-LOG.md** | Long-term context memory, completed sprints, technical decisions, problem resolutions | Active | Sprint 25 |
| **RFC.md** | RFC: CLI/TUI debuggee terminal attachment (macOS TTY gap) | Implemented — Option B (`runInTerminal`) | 2026-11-15 |
| **PLAN-audio-architecture-discipline-{0..7}.md** | The audio architecture discipline rewrite: tracker and seven parts | Ephemeral; completed plans deleted post-sprint | 2026-11-14 |
| **PLAN-windows-conformance-audit.md** | Windows conformance: audit resolution, `jam::AsyncOutputStream`, `jam::ScopedComInitialiser`, `runInTerminal` | Ephemeral; tracker Part 8 | 2026-11-15 |
| **NAMES.md** | (inherited from ~/.carol/) | Referenced for identifier contracts | — |
| **MANIFESTO.md** | (inherited from ~/.carol/) | BLESSED compliance reference | — |
| **CODING.md** | (inherited from ~/.carol/) | Code standards reference | — |

---

## Doxygen Locations

Doxygen lives in headers; the header is the canonical doc. The `doxygen-writer` skill governs it.

**Headers (doxygen home):**
- `Source/Whatdbg.h` — main: application, Model listener, isRunning quit
- `Source/WHATDBGModel.h` — the Model and its rows
- `Source/Processor.h` — engine context, rings, mailbox, command bodies
- `Source/debug/State.h` — enums `ExecutionState`, `StopReason`; alias `Events`; `getExceptionName`
- `Source/Launcher.h` — runInTerminal launcher mode
- `Source/debug/Session.h` — cross-platform engine wrapper interface
- `Source/debug/BreakpointManager.h` — breakpoint registry, `BreakpointInfo`, `BreakpointLocation`
- `Source/debug/VariableManager.h` — frameId and variablesReference registry
- `Source/debug/StopManager.h` — stops table and stop drains
- `Source/debug/Callbacks.h`, `Source/debug/Loader.h` — [Windows] COM callbacks, sidecar loader
- `Source/debug/PrettyPrint.h` — shared helpers (`shouldSkipSymbol`, `parseHexAddress`)
- `Source/dap/Types.h` — DAP message builders

The classes and members of the 2026-11-14 rewrite carry no doxygen yet. Header prose is a dedicated pass, run after the audit. The XML index is `docs/xml`.

**Framework Doxygen (referenced, not actively maintained by whatdbg):**
- JUCE — via `#include <JuceHeader.h>`
- JAM — `jam::Model`, `jam::AsyncUpdater`, `jam::Instance`, `jam::Union`, `jam::JsonDocument`, `jam::JsonWriter`, `jam::TerminalPage`, `jam::StdioServer`, `jam::TerminalFIFO`, `jam::Mailbox`, `jam::Array`, `jam::debug::Log`

**Excluded (not applicable or actively obsolete):**
- KANJUT, CIUM, END framework doxygen — separate projects
- `Source/jam/Context.h` — deleted; `jam::Instance` in jam_core replaces it
- `Source/Log.h` — deleted; `jam::debug::Log` replaces it

---

## Build Notes

**Generator:** `cast`. It reads `project-info.md` and `cast/spell.md`, writes `CMakeLists.txt`, `Source/generated/ProjectInfo.h`, `gh.cmake`, `RELEASE.md`, `cast/installer/mac/distribution.xml` and `cast/installer/win/installer.nsi`, then runs the toolchain row you select.

**Commands:** `cast cast/spell.md` (Release, signed, installer, GitHub upload through `cmake -P gh.cmake`), `cast cast/spell.md --debug`, `cast cast/spell.md --no-sign`. On Windows, run them from the MSVC shell of the host architecture.

**Release:** each host uploads its installer to the GitHub release `v<versionString>` of `jrengmusic/whatdbg`. The first host creates the release with `RELEASE.md` as its notes; the other hosts add their asset.

**Architecture:** native. CMake reads `uname -m` and forces `CMAKE_OSX_ARCHITECTURES` to it, then selects the matching sidecar from `Resources/macos/`. There is no universal binary.

**Primary Build Target:** `juce_add_console_app(${PROJECT_NAME} …)` in the generated `CMakeLists.txt`.

**Platform-Specific Behavior:**

**Windows:**
- Toolchain: MSVC (via vcvarsall)
- Sidecar path: `~/.config/whatdbg/dbgeng/` (DLLs embedded as BinaryData, extracted at startup)
- Binaries: x64, ARM64 (per CMAKE_GENERATOR_PLATFORM)
- Linking: ole32, oleaut32 (COM APIs)
- Signing: Release only. makensis signs `whatdbg.exe`, the installer and the uninstaller with signtool and the `identityWindows` thumbprint (`cast/signing.md`). The NSIS installer puts `whatdbg.exe` in `%USERPROFILE%\.local\bin` and adds that folder to the user PATH (EnVar plugin).

**macOS:**
- Toolchain: Xcode clang
- Architecture: native. CMake reads `uname -m`, forces `CMAKE_OSX_ARCHITECTURES` to it, and selects the matching sidecar. `Resources/macos/` holds both arm64 and x86_64.
- Sidecar path: `~/Library/Application Support/whatdbg/liblldb/` (per-arch liblldb.dylib, extracted + re-exec trampoline)
- Re-exec pattern: Main.cpp extracts liblldb.dylib, sets DYLD_LIBRARY_PATH, execv's itself
- Linking: direct link against `Resources/macos/<arch>/liblldb.dylib`. At run time `Main.cpp` extracts the sidecar copy, sets `DYLD_LIBRARY_PATH`, and re-execs so dyld resolves the SB API symbols.
- Signing: Release only. The generated `CMakeLists.txt` runs strip, `xattr -cr`, `codesign` with `entitlements.plist`, `pkgbuild`, `productbuild` (with `cast/installer/mac/distribution.xml`), `productsign`, `notarytool submit --wait`, then `stapler staple`. The project owns `entitlements.plist` — it depends on no shared signing directory.
- Deployment target: macOS 12.0 (Monterey)

**Build Output:**
- `Builds/Release/` and `Builds/Debug/` — CMake out-of-source trees, one per build type (generated)
- `../Release/whatdbg v<version> macOS <arch>.pkg` — macOS installer (signed, notarized, stapled), one per mac architecture
- `../Release/whatdbg v<version> Windows <arch>.exe` — Windows installer (signed), x64 or arm64
- `~/.local/bin/whatdbg` — post-build copy on every non-Windows build

**Build Scripts:**
- `build-liblldb.sh` — LLVM pinned-version build, per-arch strip, sidecar layout. Also writes `Resources/macos/licenses/LLVM-LICENSE.TXT`.
- `build.sh` — escape hatch, a copy of `cast/build.sh`.

**Special Notes:**
- BinaryData embedding: `juce_add_binary_data` consumes `WHATDBG_BINARY_FILES`, which the generated CMake fills per architecture from `BINARY_DATA_DIR`
- `-DCAST_SIGN=ON` on the default row, `OFF` on `no-sign`. `CMakeLists.txt` defaults the option to `ON`.
- Sidecar DLLs/dylibs versioned via Git LFS (`.gitattributes` rules)
- `~/.local/bin` is the only install location, on both platforms. The generated CMake post-build block copies the artefact there on every build, atomically via a `.tmp` rename. The `.pkg` payload stages to `/private/tmp/whatdbg_pkg_root`; its postinstall script resolves the real user's home, moves the binary to `~/.local/bin/whatdbg`, then removes the staging directory. Nothing is installed outside `~/.local/bin`.

---

**End of CLAUDE.md**  
**JRENG!**
