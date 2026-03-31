@echo off
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -property installationPath`) do set VS_PATH=%%i
echo VS_PATH=%VS_PATH%
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 > "%~dp0build_log.txt" 2>&1
cl /Zi /EHsc /Od hello.cpp >> "%~dp0build_log.txt" 2>&1
echo BUILD_EXIT=%ERRORLEVEL% >> "%~dp0build_log.txt"
