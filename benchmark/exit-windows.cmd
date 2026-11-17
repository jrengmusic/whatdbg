@echo off
setlocal

set "root=%~dp0.."
set "eve=%root%\..\eve"

nvim --headless -l "%root%\benchmark\exit-windows.lua" "%eve%\Builds\Release\EVE_artefacts\Release\Standalone\EVE.exe" "%eve%"
set exitCode=%errorlevel%

type nul > "%~dp1exit-windows-samples.md"
exit /b %exitCode%
