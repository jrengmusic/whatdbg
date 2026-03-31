@echo off
cd /d C:\Users\jreng\Documents\Poems\dev\whatdbg\test
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -property installationPath`) do set VS_PATH=%%i
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64
echo --- CL compile ---
cl /Zi /EHsc /Od hello.cpp /Fe:hello.exe /Fd:hello.pdb
echo BUILD_EXIT=%ERRORLEVEL%
