# PLAN: whatdbg macOS Port via liblldb (Sidecar)

**RFC:** none — RFC-WHATDBG-MAC-00 inlined into Appendix A, original deleted
**Date:** 2026-04-15
**BLESSED Compliance:** verified against MANIFESTO.md
**Language Constraints:** C++17 / JUCE console app (reference implementation per LANGUAGE.md — no overrides)

---

## Overview

Port whatdbg to macOS by swapping the dbgeng backend for liblldb. The DAP surface, threading model, and `Session` public contract remain identical across platforms. `liblldb.dylib` is vendored and embedded in the `.app` bundle as the direct analogue of the Windows dbgeng sidecar — zero host dependencies, deterministic version.

---

## Session-Locked Decisions (ARCHITECT)

1. **Sidecar model — not Xcode framework.** Apple's `LLDB.framework` at `/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework` is 354 MB per-arch, depends on Python3, 12+ Swift compiler dylibs, and private framework `CoreAnalytics`. Non-redistributable. Ships without `Headers/`. Out of scope.
2. **Self-built `liblldb.dylib` bundled inside `.app/Contents/Frameworks/`.** Loader-resolved via `@rpath`. Macos equivalent of Windows dbgeng sidecar.
3. **Binary budget: ~200 MB universal (arm64 + x86_64).** Estimate ~150–180 MB for `liblldb.dylib` with Python/Swift/libedit/curses/libxml2 disabled, MinSizeRel, only AArch64 + X86 codegen targets, full Clang expression parser retained.
4. **Full expression-eval parity.** `SBFrame::EvaluateExpression` kept — mirrors Windows `Execute("?? …")`. This is the dominant contributor to binary size; dropping it would break `Session` contract parity.
5. **License: Apache 2.0 with LLVM Exception.** Permits static + dynamic linking in proprietary apps. Obligations: ship `LICENSE.TXT` inside the `.app`, preserve copyright notices, no LLVM/Clang/LLDB trademarks in marketing. No copyleft. No GPL transitive deps when Python/Lua/libedit/libxml2 are disabled.
6. **API surface mirrored.** Every method on `debug::Session` keeps its signature. Platform fork lives strictly below the `Session` public interface.

---

## RFC Corrections (locked)

Evidence-based corrections to RFC-WHATDBG-MAC-00 discovered during pre-plan audit:

| RFC claim | Reality | Correction |
|---|---|---|
| "Session.h is the contract. Do not modify it." | `Session.h:3-7` includes `windows.h`, `wrl/client.h`, `dbgeng.h`, `Loader.h`, `Callbacks.h`; public API uses `ULONG`, `HRESULT`, `ULONG64`, `ComPtr<IDebug*>` in member fields. Won't compile on macOS as-is. | Header must be detoxed — portable-typed public API, Windows COM members behind `#if JUCE_WINDOWS`. |
| "State, BreakpointManager, Whatdbg unchanged" | `State.h:3` `#include <windows.h>`; `ULONG` in `targetProcessId`, `breakpointEngineId`. `BreakpointManager.h` uses `ULONG` throughout public API. `Whatdbg.h:271` uses `ULONG` in `frameIdMap`. | All three need `ULONG` → `std::uint32_t` substitution. |
| "Link `LLDB.framework` from Xcode" | Framework ships without `Headers/`; binary is non-redistributable (Swift/Python/private frameworks). | Replace with vendored `liblldb.dylib` in `.app`. |
| "Loader.cpp / Callbacks.cpp deleted on macOS" | Correct — these are DbgEng-specific. | Platform-select in CMake. |

---

## Language / Framework Constraints

C++17 / JUCE. MANIFESTO enforced as-written. No overrides from LANGUAGE.md apply. Relevant coding-standard points for this port:

- No early returns — positive nested checks (JRENG-CODING-STANDARD.md §Control Flow).
- `not` / `and` / `or` alternative tokens — never `!` / `&&` / `||`.
- `.at()` on containers — never `[]`.
- Brace initialization — `{ }`, never `=`.
- No raw `new`/`delete`; SB API objects are value types with ref-counted internals — RAII by default.
- No anonymous namespaces; use `static` file-local symbols.
- Doxygen on all public APIs (SPEC success criteria).

---

## Validation Gate

Each step validated before the next begins. Validation = `@Auditor` confirms step output complies with:

- MANIFESTO.md (BLESSED)
- NAMES.md (naming philosophy — Rule -1: no improvised names without ARCHITECT approval)
- carol/JRENG-CODING-STANDARD.md (C++ standards)
- carol/LANGUAGE.md (no overrides for C++/JUCE)
- Locked PLAN decisions above — no scope drift

---

## Architectural Decisions (LOCKED 2026-04-15)

### D-1: Header detox strategy — **A (locked)**

Substitute `ULONG` → `std::uint32_t` in `State.h`, `BreakpointManager.h`, `Whatdbg.h`, and `Session.h` public signatures. Guard remaining COM includes (`windows.h`, `dbgeng.h`, `wrl/client.h`) and `ComPtr<IDebug*>` private members in `Session.h` with `#if JUCE_WINDOWS`. Add parallel `#if JUCE_MAC` block for macOS-only private members (SB object owners). Mechanical find-replace; preserves current file layout; zero behaviour change on Windows.

### D-2: LLVM version pin — **B (locked)**

Pin to the latest `21.x` LLVM release tag — one major version behind the 22.1.3 stable released 2026-04-07. More field-tested, still modern. Exact tag (e.g. `llvmorg-21.?.?`) confirmed at Phase 0 Step 0.1 by reading `https://github.com/llvm/llvm-project/tags`.

### D-3: Dylib delivery — **C (locked)**

Hybrid. `scripts/build-liblldb-mac.sh` pins the D-2 source tag, clones `llvm-project` into a **gitignored** working directory, builds `liblldb.dylib` + headers, places output under a gitignored path (e.g. `build/liblldb/`). Developers run the script once (30–90 min first build); CI runs it on clean macOS runners with artefact caching. Repo stays lean. Version bumps are a one-line change in the script.

### D-4: `lldb-dap` reference location — **A (locked)**

Use the `llvm-project` clone produced by D-3-C directly. The 6 reference files (Appendix A.6) live under `<gitignored-workdir>/llvm-project/lldb/tools/lldb-dap/`. Path documented in the build script and in PLAN. No separate vendoring. Composes cleanly with D-3 — clone is already required for the build.

---

## Implementation-Time Decisions (deferrable)

### D-5: `pollEvents` timeout granularity

`SBListener::WaitForEvent` takes seconds (uint32_t). Windows uses 50 ms.

- **D-5-A** — `listener.WaitForEvent(0, event)` non-blocking each iteration; main loop retains own pacing. Closest to Windows semantics.
- **D-5-B** — Pass `1` second; accept coarser granularity.

Deferred to Phase 3.2. `@engineer` proposes at implementation, `@auditor` validates.

### D-6: `task_for_pid` entitlement policy

Attaching to a running process on macOS requires the debuggee to carry `com.apple.security.get-task-allow`.

- **D-6-A** — Document as known limitation in SPEC.md macOS section. No code change.
- **D-6-B** — Add graceful attach-failure diagnostic in `Session_mac.cpp`.

Deferred to Phase 3 attach work. `@engineer` proposes at implementation, `@auditor` validates.

---

## Steps

**Phasing rationale:** Phase 0 derisks the entire plan cheaply — if `liblldb.dylib` can't be built at target size or the 20-line smoke test fails, the plan needs rework before any production code touches. Phase 1 (header detox) is pure Windows-side refactor that must pass Windows CI before macOS code is written. Phase 2 wires the vendor artefact into CMake. Phases 3–5 are the actual port, sequenced by `Session` concern. Phase 6 is integration.

---

### Phase 0 — Vendor & Derisk

#### Step 0.1: Write `scripts/build-liblldb-mac.sh`
**Scope:** new file `scripts/build-liblldb-mac.sh`.
**Action:** `@engineer` writes a bash script that clones `llvm-project` at the D-2 pinned tag into a build directory, configures via CMake with `-DLLDB_ENABLE_PYTHON=OFF -DLLDB_ENABLE_LUA=OFF -DLLDB_ENABLE_LIBEDIT=OFF -DLLDB_ENABLE_CURSES=OFF -DLLDB_ENABLE_LIBXML2=OFF -DLLDB_INCLUDE_TESTS=OFF -DLLVM_ENABLE_PROJECTS="clang;lldb" -DLLVM_TARGETS_TO_BUILD="AArch64;X86" -DCMAKE_BUILD_TYPE=MinSizeRel`, builds `liblldb`, reports final `.dylib` size per arch and combined universal size, copies output + headers to `Resources/macos/liblldb/`.
**Validation:** `@auditor` runs the script end-to-end, captures reported size, confirms `< 200 MB universal`. Reports actual size number back to ARCHITECT.

#### Step 0.2: Vendor `lldb-dap` reference (per D-4 ruling)
**Scope:** per D-4 ruling — either `vendor/lldb-dap-reference/` or external sibling path.
**Action:** `@engineer` vendors or documents the 6 files named in Appendix A.6 at the D-2-matching SHA. Preserves Apache-2.0 LICENSE header.
**Validation:** `@auditor` confirms files present (or path documented), LICENSE intact, SHA recorded.

#### Step 0.3: 20-line smoke test
**Scope:** new standalone file `tests/mac/smoke_liblldb.cpp` + minimal CMakeLists entry.
**Action:** `@engineer` writes a standalone program that: `SBDebugger::Initialize()`, creates target for `/bin/echo hello`, launches, waits for stop, reads stop reason, prints, cleans up. Links against vendored `liblldb.dylib` via `@rpath`. Documents signing / entitlement requirements encountered.
**Validation:** `@auditor` runs the binary, confirms clean launch + stop + exit. Any entitlement blockers captured for D-6.

---

### Phase 1 — Header Detox (Windows build must stay green)

#### Step 1.1: `ULONG` substitution in cross-platform headers
**Scope:** `Source/debug/State.h`, `Source/debug/BreakpointManager.h`, `Source/Whatdbg.h`.
**Action (pending D-1 ruling):** `@engineer` replaces every `ULONG` with `std::uint32_t` in these three files + their `.cpp` counterparts where field types propagate. Removes `#include <windows.h>` from `State.h`. Updates all signatures and field types consistently. Does not touch `Session.h` yet.
**Validation:** `@auditor` confirms (a) zero `ULONG` remains in State/BreakpointManager/Whatdbg surface, (b) Windows build compiles and passes on first try, (c) no semantic behaviour change (storage width unchanged), (d) NAMES.md compliance — no improvised names introduced.

#### Step 1.2: `Session.h` detox (per D-1 ruling)
**Scope:** `Source/debug/Session.h`, plus any callers affected by type substitution.
**Action:** `@engineer` applies D-1-A / B / C strategy per ARCHITECT ruling.
  - If D-1-A: substitute `ULONG` → `std::uint32_t`, `ULONG64` → `std::uint64_t`, `HRESULT` → typedef'd portable result type; wrap `#include <windows.h>`, `<wrl/client.h>`, `<dbgeng.h>`, `Loader.h`, `Callbacks.h` and `ComPtr<IDebug*>` members in `#if JUCE_WINDOWS`; reserve parallel `#if JUCE_MAC` block (empty at this step).
  - If D-1-B: introduce PIMPL `Impl` struct, move all COM members into Windows-only `Session.cpp`.
  - If D-1-C: split into public `Session.h` + platform-private internals headers.
**Validation:** `@auditor` confirms Windows build still green, public API method signatures unchanged (except portable-type substitution), BLESSED compliance (no new shadow state, no new improvised names — NAMES.md Rule -1).

#### Step 1.3: Windows regression gate
**Scope:** full Windows build + smoke.
**Action:** ARCHITECT or automation runs Windows build on MSVC; smoke-tests launch + set BP + step + inspect + terminate.
**Validation:** `@auditor` confirms zero regressions against SPEC §Success Criteria.

**GATE:** Phase 2 cannot start until Phase 1 Windows build is green.

---

### Phase 2 — CMake + Sidecar Bundle

#### Step 2.1: Platform-select source files
**Scope:** `CMakeLists.txt`.
**Action:** `@engineer` adds `if (APPLE) … elseif (WIN32) … endif()` block selecting platform-specific source files. Current Windows sources gated to `WIN32`. Mac branch adds forward-declared file placeholders (`Session_mac.cpp`, `SessionInspection_mac.cpp`, `SessionPrettyPrint_mac.cpp` — empty skeletons with header include and namespace only) to unblock the build before Phase 3 fills them.
**Validation:** `@auditor` confirms CMake configures on both platforms without errors; Windows build still green; macOS build reaches the link step and fails only on unresolved `debug::Session` method bodies.

#### Step 2.2: Embed vendored `liblldb.dylib` into `.app`
**Scope:** `CMakeLists.txt` macOS branch.
**Action:** `@engineer` adds (a) `target_include_directories` pointing at `Resources/macos/liblldb/include`, (b) `target_link_libraries` against the vendored `liblldb.dylib`, (c) copy step placing the dylib at `$<TARGET_BUNDLE_CONTENT_DIR>/Frameworks/liblldb.dylib`, (d) `install_name_tool` rewrite so the main executable's `LC_LOAD_DYLIB` points to `@rpath/liblldb.dylib`, (e) `-rpath @executable_path/../Frameworks` on the main target, (f) copy `LLVM LICENSE.TXT` to `$<TARGET_BUNDLE_CONTENT_DIR>/Resources/licenses/`.
**Validation:** `@auditor` runs `otool -L` on the built binary and confirms the dylib resolves via `@rpath`, runs the app, confirms no missing-dylib errors. Confirms LICENSE is present in the bundle.

#### Step 2.3: `Main.cpp` platform guard
**Scope:** `Source/Main.cpp`.
**Action:** `@engineer` wraps `extractSidecarBinaries()` in `#if JUCE_WINDOWS`. On macOS, the sidecar dylib is already inside the bundle — no extraction needed. Pass an empty `juce::File {}` or a sentinel to `Whatdbg::initialize` on macOS (signature unchanged per locked decision #6). Guards `_setmode` and any other Windows-only calls.
**Validation:** `@auditor` confirms both platforms build and link, Windows extraction unchanged, macOS entry path clean.

**GATE:** Phase 3 starts only after Phase 2 macOS build links successfully (even with empty Session_mac bodies).

---

### Phase 3 — `Session_mac.cpp` (Lifecycle + Events)

Each sub-step implements one concern against Appendix A.1 (method mapping). `@engineer` reads the referenced `lldb-dap` source file before writing, per D-4 vendoring. `@auditor` validates after each sub-step.

#### Step 3.1: `initialize` / `shutdown`
**Scope:** `Source/debug/Session_mac.cpp`.
**Action:** `SBDebugger::Initialize()` + `SBDebugger::Create(false)` + `SetAsync(true)` + `SBListener` setup (process events + target events per Appendix A.4). `shutdown()` → `SBProcess::Kill()` / `Detach()` + `SBDebugger::Destroy()`.
**Validation:** `@auditor` confirms RAII on `SBDebugger`/`SBListener`/`SBTarget`/`SBProcess` (BLESSED B — no raw owning pointers), no early returns, `not`/`and`/`or` tokens, NAMES.md compliance.

#### Step 3.2: `pollEvents`
**Scope:** `Source/debug/Session_mac.cpp`.
**Action:** Implement per Appendix A.5 — `listener.WaitForEvent(...)`, dispatch `SBProcess::EventIsProcessEvent` / `SBTarget::EventIsTargetEvent`, write flags into `debug::State` (same fields used by Windows callbacks). Timeout handling per D-5 ruling. Resolve `state` naming collision noted in A.5 — new identifier requires NAMES.md Rule -1 ARCHITECT approval.
**Validation:** `@auditor` confirms (a) flag-writing pattern matches existing Windows behaviour (SSOT through `debug::State`, no shadow state), (b) no early returns, positive nested checks only, (c) `switch` on `StopReason` ≤ 3 branches or refactored to lookup per MANIFESTO L (3-branch limit).

#### Step 3.3: Execution control — `resume` / `stepOver` / `stepInto` / `stepOut` / `interrupt`
**Action:** Per Appendix A.1 — `SBProcess::Continue()`, `SBThread::StepOver/Into/OutOfFrame`, `SBProcess::SendAsyncInterrupt()`. Each function ≤ 30 lines per MANIFESTO L.
**Validation:** `@auditor` confirms signatures unchanged, no new state added to `Session`.

#### Step 3.4: Breakpoints — `addBreakpoint` / `removeBreakpoint` / `getOffsetByLine` / `getLineByOffset`
**Action:** Per Appendix A.1. `SBTarget::BreakpointCreateByLocation`, `BreakpointDelete`, `SBCompileUnit::FindLineEntry` + `SBLineEntry::GetStartAddress().GetLoadAddress()`, `SBAddress::GetLineEntry()`. Returned `break_id_t` is `int32_t`; cast to `std::uint32_t` at the boundary — internal `State` storage width unchanged after Phase 1.1.
**Validation:** `@auditor` confirms cast-at-boundary only (no ULONG leakage, Explicit), BLESSED-compliant.

#### Step 3.5: Symbol / source paths
**Action:** `loadModuleSymbols` / `forceReloadAllSymbols` → no-ops (liblldb auto-loads). `appendSymbolPath` → `SBTarget::AppendImageSearchPath`. `appendSourcePath` → source-map config. Behavior difference acceptable — macOS dSYM resolution is largely automatic via Spotlight / `dsymutil`.
**Validation:** `@auditor` confirms no-op paths documented with a single WHY comment (non-obvious behaviour per CLAUDE.md — "a comment is warranted when WHY is non-obvious").

#### Step 3.6: Thread ops
**Action:** `getThreads`, `getEventThreadSystemId`, `setCurrentThreadBySystemId`, `resetSymbolGroupCache` — per Appendix A.1. `lldb::tid_t` is `uint64_t`; cast to `std::uint32_t` at boundary (matches State's existing storage after Phase 1.1).
**Validation:** `@auditor` confirms boundary casts only, no width-narrowing silent data loss on threads with TID > 32-bit max (assert or log).

**GATE:** Phase 4 starts only after Phase 3 passes full `@auditor` validation.

---

### Phase 4 — `SessionInspection_mac.cpp`

#### Step 4.1: `getStackTrace`
**Action:** `SBThread::GetNumFrames()` walk, `SBFrame::GetLineEntry()` per frame, produce the same `juce::var` DAP-formatted output as Windows (schema defined in `Session.h` doxygen).
**Validation:** `@auditor` confirms output schema bit-identical to Windows for equivalent inputs (D — Deterministic).

#### Step 4.2: `getLocals`
**Action:** `SBFrame::GetVariables(args=true, locals=true, statics=true, inScopeOnly=true)` → iterate `SBValueList`, build DAP variable objects with same schema as Windows (`symbolIndex` maps to index within cached `SBValueList`).
**Validation:** `@auditor` confirms output schema parity, SSOT — `SBValueList` cached per-frame on `Session` (mirror of Windows `cachedSymbolGroup`), cache invalidation via `resetSymbolGroupCache` matches Windows semantics.

#### Step 4.3: `getVariableChildren`
**Action:** `SBValue::GetNumChildren()` + `GetChildAtIndex(i)`, same DAP schema.
**Validation:** `@auditor` confirms no new state, same schema.

#### Step 4.4: `evaluateExpression`
**Action:** `SBFrame::EvaluateExpression(expr)` → `SBValue::GetValue()` / `GetSummary()`. Format through existing `debug::detail::formatSymbolValue` where applicable, or macOS-specific formatter in Phase 5.
**Validation:** `@auditor` confirms signature parity, expression-eval covers SPEC §Feature 7 supported-expression set.

**GATE:** Phase 5 starts after Phase 4 passes.

---

### Phase 5 — `SessionPrettyPrint_mac.cpp`

#### Step 5.1: Replace Windows post-processing formatters
**Action:** `SBValue::GetSummary()` + `GetValue()` → same DAP `value` field format as Windows per SPEC §Feature 6 formatting contract. STL types (`juce::String`, `std::string`, `std::unique_ptr<T>`, `std::vector<T>`) use LLDB's built-in data formatters — verify output matches Windows hand-rolled `prettyPrint` output for these 4 types.
**Validation:** `@auditor` confirms output bit-identity for these 4 types against Windows reference output (D — Deterministic); any divergence documented and ARCHITECT-approved.

#### Step 5.2: Filtered-symbol parity
**Action:** `<begin>$L0`, `<end>$L0`, `leakDetectorNNN`, `__vfptr` filters applied same as Windows (SPEC §Feature 6 Filtered Symbols).
**Validation:** `@auditor` confirms filter list applied identically.

**GATE:** Phase 6 starts after Phase 5 passes.

---

### Phase 6 — Integration

#### Step 6.1: macOS golden-path smoke
**Action:** ARCHITECT or test harness runs SPEC §Success Criteria features 1–10 against a real target on macOS. At minimum: launch a JUCE console app, set breakpoint, hit, inspect locals, step, continue, terminate.
**Validation:** `@auditor` confirms all SPEC success criteria pass on macOS; output compared against Windows reference where deterministic.

#### Step 6.2: Windows regression gate
**Action:** Re-run full Windows smoke (from Phase 1.3).
**Validation:** `@auditor` confirms zero Windows regressions introduced by the port.

#### Step 6.3: Documentation
**Action:** Update `SPEC.md` → add macOS to Technology Stack § Platform row; add macOS §Success Criteria (if any differ). Update `ARCHITECTURE.md` → add Mac branch to Module Map + File Structure. Update `CLAUDE.md` / `LANGUAGE.md` if platform-specific notes needed.
**Validation:** `@auditor` confirms docs reflect reality, no stale Windows-only claims left.

---

## BLESSED Alignment

| Pillar | How satisfied in this plan |
|---|---|
| **B** — Bound | `SBDebugger`, `SBTarget`, `SBProcess`, `SBListener` are SB API value types with ref-counted internals — RAII automatic. Sidecar dylib lifecycle owned by dynamic loader (`@rpath`) — no manual Loader class on macOS. |
| **L** — Lean | Each `Session_mac*.cpp` file mirrors its Windows counterpart's scope. ~30-method public API preserved. Expect 300-line smell detector respected by file split mirroring Windows. YAGNI on abstract base classes — no `ISession` virtual dispatch. |
| **E** — Explicit | Every `SBError` returned by SB API checked. `jassert` at API boundaries. No early returns — positive nested checks. `not`/`and`/`or` tokens. All casts (tid_t→uint32_t, break_id_t→uint32_t) explicit at boundary. |
| **S** — SSOT | `debug::State` remains the single source of truth. Event dispatch writes same flags Windows writes. No shadow state. Per-frame cached `SBValueList` mirrors Windows `cachedSymbolGroup`. |
| **S** — Stateless | `Session_mac` holds only lifecycle-required objects (`SBDebugger`, `SBTarget`, `SBListener`, cached per-frame `SBValueList`). Same state surface as Windows. No machinery state. |
| **E** — Encapsulation | `Whatdbg` sees `Session` as one opaque interface on both platforms. Platform leak stops at `Session`'s public boundary. Tell-don't-ask preserved. |
| **D** — Deterministic | Same DAP input → same DAP output on both platforms is a Phase 5.1 and Phase 6 validation requirement, not an assumption. Divergence = bug. |

---

## Risks / Open Questions

All listed above in **Open Decisions Requiring ARCHITECT Ruling**. No unlisted risks.

**Phase 0 is the derisker:** if Step 0.1 reports > 200 MB universal, or Step 0.3 fails on linkage/entitlement, the entire plan returns to ARCHITECT for re-scoping before Phase 1 begins.

---

*Plan gated at Decision Gate. Awaiting ARCHITECT ruling on D-1 through D-6 before any execution.*

**JRENG!**

---

# Appendix A — API Mapping Reference

Reference material inlined from former RFC-WHATDBG-MAC-00 (now deleted). Ground-truth patterns for implementing `Session_mac.cpp`, `SessionInspection_mac.cpp`, `SessionPrettyPrint_mac.cpp`. Source: `llvm-project/lldb/tools/lldb-dap` per D-4 vendoring.

---

## A.1 — Session Method Mapping (DbgEng → liblldb)

| `debug::Session` method | DbgEng (Windows) | liblldb (macOS) |
|---|---|---|
| `initialize()` | `CoInitializeEx` + `DebugCreate` + QI chain | `SBDebugger::Initialize()` + `SBDebugger::Create()` + `SBListener` setup |
| `launch()` | `IDebugClient5::CreateProcess2` | `SBTarget::LaunchSimple` or `SBTarget::Launch` with `SBLaunchInfo` |
| `attach()` | `IDebugClient5::AttachProcess` | `SBTarget::AttachToProcessWithID` |
| `resume()` | `IDebugControl4::SetExecutionStatus(GO)` | `SBProcess::Continue()` |
| `pollEvents()` | `IDebugControl4::WaitForEvent(0, 50ms)` | `SBListener::WaitForEvent(timeoutSec, event)` — see D-5 |
| `shutdown()` | `IDebugClient5::EndSession` + `CoUninitialize` | `SBProcess::Kill()` or `SBProcess::Detach()` + `SBDebugger::Destroy()` |
| `stepOver()` | `SetExecutionStatus(STEP_OVER)` | `SBThread::StepOver(eOnlyDuringStepping)` |
| `stepInto()` | `SetExecutionStatus(STEP_INTO)` | `SBThread::StepInto()` |
| `stepOut()` | `Execute("gu")` | `SBThread::StepOutOfFrame(frame)` |
| `interrupt()` | `DebugBreakProcess(OpenProcess(pid))` | `SBProcess::SendAsyncInterrupt()` |
| `addBreakpoint()` | `IDebugControl4::AddBreakpoint2` | `SBTarget::BreakpointCreateByLocation(file, line)` |
| `removeBreakpoint()` | `IDebugControl4::RemoveBreakpoint2` | `SBTarget::BreakpointDelete(bp_id)` |
| `getOffsetByLine()` | `IDebugSymbols3::GetOffsetByLine` | `SBCompileUnit::FindLineEntry` + `SBLineEntry::GetStartAddress().GetLoadAddress()` |
| `getLineByOffset()` | `IDebugSymbols3::GetLineByOffset` | `SBAddress::GetLineEntry()` → `SBLineEntry` |
| `loadModuleSymbols()` | `Execute(".reload /f <name>")` | No-op — liblldb loads symbols automatically on module load |
| `forceReloadAllSymbols()` | `Execute(".reload /f")` | No-op |
| `appendSymbolPath()` | `IDebugSymbols3::AppendSymbolPath` | `SBTarget::AppendImageSearchPath` or `SBDebugger::SetSelectedPlatformWorkingDirectory` |
| `appendSourcePath()` | `IDebugSymbols3::AppendSourcePath` | `SBDebugger::SetSourceMap` or compile-unit path resolution |
| `getThreads()` | `GetThreadIdsByIndex` + `GetThreadDescription` | `SBProcess::GetNumThreads()` + `SBProcess::GetThreadAtIndex(i).GetThreadID()` + `GetName()` |
| `getEventThreadSystemId()` | `IDebugSystemObjects::GetEventThread` | `SBProcess::GetSelectedThread().GetThreadID()` |
| `setCurrentThreadBySystemId()` | `GetThreadIdBySystemId` + `SetCurrentThreadId` | `SBProcess::SetSelectedThreadByID(systemId)` |
| `resetSymbolGroupCache()` | Reset `cachedSymbolGroup` ComPtr | Reset cached `SBFrame` / `SBValueList` |

---

## A.2 — Inspection Mapping

| Operation | DbgEng | liblldb |
|---|---|---|
| Get locals for frame | `GetScopeSymbolGroup2(DEBUG_SCOPE_GROUP_ALL)` | `SBFrame::GetVariables(args, locals, statics, inScopeOnly)` |
| Enumerate symbols | `IDebugSymbolGroup2::GetSymbolParameters` loop | `SBValueList` iteration |
| Has children | `DEBUG_SYMBOL_PARAMETER::Flags & DEBUG_SYMBOL_IS_EXPANDABLE` | `SBValue::MightHaveChildren()` |
| Get children | `IDebugSymbolGroup2::ExpandSymbol(index)` | `SBValue::GetNumChildren()` + `SBValue::GetChildAtIndex(i)` |
| Symbol name | `GetSymbolName(index, buf, size, &nameSize)` | `SBValue::GetName()` |
| Symbol type | `GetSymbolTypeName(index, buf, size, &typeSize)` | `SBValue::GetTypeName()` |
| Symbol value | `GetSymbolValueText(index, buf, size, &valSize)` | `SBValue::GetValue()` |
| Stack walk | `IDebugControl4::GetStackTrace` + `GetLineByOffset` per frame | `SBThread::GetNumFrames()` + `SBFrame::GetLineEntry()` per frame |
| Expression eval | `Execute("?? <expr>")` + capture output | `SBFrame::EvaluateExpression(expr)` → `SBValue::GetValue()` |

The `symbolIndex`/`frameIndex` cache in `Whatdbg` maps to `SBValue` references held per stop event. `resetSymbolGroupCache()` clears the cached `SBValueList`.

---

## A.3 — Pretty-Print Mapping

| DbgEng pattern | liblldb equivalent |
|---|---|
| `stripDecimalPrefix` on raw output | Not needed — `SBValue::GetValue()` is already clean |
| `readTargetString` for `std::string` | `SBValue::GetSummary()` handles most STL types natively |
| `parseHexAddress` for pointer display | `SBValue::GetValue()` returns formatted pointer string |
| `findChildByName` for struct field traversal | `SBValue::GetChildMemberWithName(name)` |
| `formatSymbolValue` dispatch on type name | `SBValue::GetSummary()` covers most cases; fallback to `GetValue()` |

`SBValue::GetSummary()` leverages LLDB's built-in data formatters (STL, Objective-C containers, etc.) — significantly more capable than the hand-rolled DbgEng formatters in `SessionPrettyPrint.cpp`.

---

## A.4 — SBListener Setup (reference for Phase 3.1)

Reference only. Variable naming and style must conform to JRENG-CODING-STANDARD.md + NAMES.md at implementation time — any new identifiers are NAMES.md Rule -1 gated.

```cpp
// Reference pattern — adapt to JRENG style + portable types per PLAN Phase 1.
bool Session::initialize (const juce::File& /* sidecarDir unused on macOS */) noexcept
{
    lldb::SBDebugger::Initialize();
    debugger = lldb::SBDebugger::Create (/*source_init_files=*/false);
    debugger.SetAsync (true);

    listener = debugger.GetListener();
    listener.StartListeningForEventClass (
        debugger,
        lldb::SBProcess::GetBroadcasterClassName(),
        lldb::SBProcess::eBroadcastBitStateChanged
        | lldb::SBProcess::eBroadcastBitSTDOUT
        | lldb::SBProcess::eBroadcastBitSTDERR);

    listener.StartListeningForEventClass (
        debugger,
        lldb::SBTarget::GetBroadcasterClassName(),
        lldb::SBTarget::eBroadcastBitModulesLoaded
        | lldb::SBTarget::eBroadcastBitBreakpointChanged);

    return debugger.IsValid();
}
```

---

## A.5 — Event Dispatch Pattern (reference for Phase 3.2)

Reference only — derived from `lldb-dap/EventHelper.cpp` `HandleProcessEvent`. **Known defect in the original RFC snippet:** two variables named `state` in the same scope (the `lldb::StateType` from `GetStateFromEvent` and the `debug::State&` from `Context::get()`). Rename one at implementation time — new identifier requires NAMES.md Rule -1 ARCHITECT approval. Flow of writes into `debug::State` flags is the authoritative part.

```cpp
// Reference pattern. NAMES collision on `state` must be resolved before writing.
// Return type uses PLAN D-1 portable result type, not HRESULT.
SessionResult Session::pollEvents (std::uint32_t timeoutMs) noexcept
{
    lldb::SBEvent event;
    const bool hasEvent { listener.WaitForEvent (
        /* seconds, see D-5 */, event) };

    if (hasEvent)
    {
        if (lldb::SBProcess::EventIsProcessEvent (event))
        {
            const auto processState { lldb::SBProcess::GetStateFromEvent (event) };
            auto& debugState { jreng::Context<debug::State>::get() };

            switch (processState)
            {
                case lldb::eStateStopped:
                case lldb::eStateCrashed:
                case lldb::eStateSuspended:
                {
                    if (not lldb::SBProcess::GetRestartedFromEvent (event))
                    {
                        auto thread { process.GetSelectedThread() };
                        switch (thread.GetStopReason())
                        {
                            case lldb::eStopReasonBreakpoint:
                                debugState.hasBreakpointHit = true;
                                debugState.breakpointEngineId =
                                    static_cast<std::uint32_t> (
                                        thread.GetStopReasonDataAtIndex (0));
                                debugState.executionState =
                                    debug::ExecutionState::stopped;
                                break;
                            case lldb::eStopReasonTrace:
                            case lldb::eStopReasonPlanComplete:
                                debugState.hasStepCompleted = true;
                                debugState.executionState =
                                    debug::ExecutionState::stopped;
                                break;
                            case lldb::eStopReasonInterrupt:
                                debugState.executionState =
                                    debug::ExecutionState::stopped;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                }
                case lldb::eStateExited:
                    debugState.processExitCode  = process.GetExitStatus();
                    debugState.hasProcessExited = true;
                    debugState.executionState   = debug::ExecutionState::exited;
                    break;
                case lldb::eStateRunning:
                case lldb::eStateStepping:
                    debugState.executionState = debug::ExecutionState::running;
                    break;
                default:
                    break;
            }
        }
        else if (lldb::SBTarget::EventIsTargetEvent (event))
        {
            const std::uint32_t mask { event.GetType() };

            if ((mask bitand lldb::SBTarget::eBroadcastBitModulesLoaded) != 0)
            {
                auto& debugState { jreng::Context<debug::State>::get() };
                const std::uint32_t numModules {
                    lldb::SBTarget::GetNumModulesFromEvent (event) };

                if (numModules > 0)
                {
                    auto module {
                        lldb::SBTarget::GetModuleAtIndexFromEvent (0, event) };
                    debugState.lastLoadedImageName =
                        juce::String (module.GetFileSpec().GetFilename());
                    debugState.hasNewModuleLoaded = true;
                }
            }
        }
    }

    // Returned status per Session.h contract (portable result type, D-1).
}
```

**BLESSED notes for Phase 3.2 implementation:**
- The nested `switch` + `if (not …)` may exceed the 3-branch smell threshold. Consider extracting stop-reason dispatch to a lookup table per MANIFESTO L (3-branch rule) — decision at implementation time by `@engineer` with `@auditor` validation.
- `Whatdbg::processDeferredEvents()` reads these flags identically on both platforms — **zero changes required** above `Session`.

---

## A.6 — Prior Art (Reference Source Files)

Source: `llvm-project/lldb/tools/lldb-dap/` (Apache 2.0 with LLVM Exception). Located per D-4 ruling.

| File | Purpose |
|---|---|
| `EventHelper.cpp` | `SBListener::WaitForEvent` dispatch loop; process state → DAP event translation; stop-reason mapping |
| `Variables.h` / `Variables.cpp` | `SBFrame::GetVariables()` → `SBValueList` → `SBValue` child traversal |
| `Handler/LaunchRequestHandler.cpp` | `SBTarget::Launch` with `SBLaunchInfo` |
| `Handler/AttachRequestHandler.cpp` | `SBTarget::AttachToProcessWithID` |
| `Handler/StackTraceRequestHandler.cpp` | `SBThread::GetNumFrames()` + `SBFrame::GetLineEntry()` walk |
| `Handler/EvaluateRequestHandler.cpp` | `SBFrame::EvaluateExpression()` |

---

## A.7 — task_for_pid Attach Entitlement (reference for D-6)

Attaching to a running process on macOS requires the debuggee to carry `com.apple.security.get-task-allow`. This is a build-configuration concern on the *debuggee* side, not a whatdbg code concern. Debug builds produced by Xcode or CMake + Apple Clang carry this entitlement automatically.

Plugin debugging (Hardened-Runtime DAW hosts): attach requires host-side `com.apple.security.cs.allow-unsigned-executable-memory` or explicit debuggability. Whatdbg cannot influence the host binary. D-6 ruling determines whether this is documented-only or surfaced at runtime.

---

*End of Appendix A — inlined from former RFC-WHATDBG-MAC-00.*
