#!/usr/bin/env bash

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/tests/smoke/build"

nvim --headless -l "$root/tests/mcp/run_mcp.lua" "$(command -v whatdbg)" "$build/fixture" "$build/fixture_wait" "$root/tests/smoke/fixture.cpp"
exitCode=$?

: > "$(dirname "$1")/mcp-samples.md"
exit $exitCode
