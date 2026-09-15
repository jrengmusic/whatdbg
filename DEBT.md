# DEBT.md

**Purpose:** Inter-sprint ledger of debts — bugs, nitpicks, friction observed during usage. Drained by sprints via `/pay` (COUNSELOR planning) and `/log` (hygiene drain). **JRENG = paid in full, cash. No triage.**

**Format:** Each entry uses **O / D / E** articulation — Observation, Divergence, Expectation. IDs are UTC timestamps (`DEBT-YYYYMMDDTHHMMSS`). Newest entries at top. Add via `carol debt add`.

**Lifecycle:** Created lazily on first `carol debt add`. Entries appended via interactive prompt. Entries removed by `carol debt clear <id>` (called by `/log` hygiene step after SPRINT-LOG receipt is written). Survives `carol reset` — debts persist across protocol resets.

---

## DEBT-20260902T184427

**Observation:** Windows bootstrap — chain vcvarsall into the toolchain. whatdbg is the only CAST-managed project that generates a wrapper build script. cast's own project-info.md:176-198 has three toolchain groups — default, debug, no-sign — each cast / cmake / ninja, and no windows row and no wrapper script. whatdbg adds a windows row that runs a generated build-windows.sh.
**Divergence:** The reason is a real constraint. Toolchain rows are argv, not shell: Processor::getToolchainArguments puts command at argv[0] and splits flag on whitespace, then hands that to jam::Subprocess driving juce::ChildProcess. There is no shell, and no environment carries from one row to the next. So a row that runs vcvarsall.bat cannot pass the MSVC environment to the following cmake row. One row must run one script that does both — that is what build-windows.sh is.
**Expectation:** The intended resolution: make the toolchain itself able to chain a Windows shell script carrying vcvarsall so no project needs a generated wrapper. That is a change in cast — the generator — not only in whatdbg, and it should land for both. Scope when paid: cast's toolchain execution path; then remove whatdbg's windows toolchain rows from project-info.md, the windows-build fence from cast/cmake.cast, the @build-windows alias and its output row from cast/spell.md, the generated build-windows.sh, and the four references in CLAUDE.md, README.md and ARCHITECTURE.md. Project: whatdbg. Also affects: cast.

---
