@echo off
setlocal

set "root=%~dp0.."
set "build=%root%\tests\smoke\build"

nvim --headless -l "%root%\tests\smoke\run_smoke.lua" whatdbg "%build%\fixture.exe" "%build%\fixture_wait.exe" "%root%\tests\smoke\fixture.cpp" "%build%\fixture_crash.exe"
set exitCode=%errorlevel%

type nul > "%~dp1smoke-samples.md"
exit /b %exitCode%
