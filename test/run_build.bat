@echo off
cd /d C:\Users\jreng\Documents\Poems\dev\whatdbg\test
set LOGFILE=C:\Users\jreng\Documents\Poems\dev\whatdbg\test\build_log.txt
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -property installationPath`) do set VS_PATH=%%i
echo VS_PATH=%VS_PATH% > %LOGFILE%
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >> %LOGFILE% 2>&1
echo --- CL compile --- >> %LOGFILE%
cl /Zi /EHsc /Od C:\Users\jreng\Documents\Poems\dev\whatdbg\test\hello.cpp /Fo:C:\Users\jreng\Documents\Poems\dev\whatdbg\test\hello.obj /Fe:C:\Users\jreng\Documents\Poems\dev\whatdbg\test\hello.exe /Fd:C:\Users\jreng\Documents\Poems\dev\whatdbg\test\hello.pdb >> %LOGFILE% 2>&1
echo BUILD_EXIT=%ERRORLEVEL% >> %LOGFILE%
