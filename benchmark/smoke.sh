#!/usr/bin/env bash

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/tests/smoke/build"

nvim --headless -l "$root/tests/smoke/run_smoke.lua" "$(command -v whatdbg)" "$build/fixture" "$build/fixture_wait" "$root/tests/smoke/fixture.cpp" "$build/fixture_crash"
exitCode=$?

: > "$(dirname "$1")/smoke-samples.md"
exit $exitCode
