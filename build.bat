@echo off
setlocal

set ROOT=%~dp0
if %ROOT:~-1%==\ set ROOT=%ROOT:~0,-1%

:: Find vcvarsall.bat via vswhere
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Is Visual Studio installed?
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set VS_PATH=%%i

set VCVARSALL=%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat
if not exist "%VCVARSALL%" (
    echo ERROR: vcvarsall.bat not found at %VCVARSALL%
    exit /b 1
)

echo Setting up MSVC x64 environment...
call "%VCVARSALL%" x64

:: Use VS-bundled ninja (avoids MSYS2 ld.exe conflict)
set PATH=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%

set CAST=%USERPROFILE%\.local\bin\cast.exe
if not exist "%CAST%" (
    echo ERROR: cast.exe not found at %CAST%. Build cast first.
    exit /b 1
)

:: Purge stale CMake cache from earlier failed configures
if exist "%ROOT%\Builds\Release\CMakeCache.txt" rmdir /s /q "%ROOT%\Builds\Release"

echo Running cast codegen + build (no-sign)...
cd /d "%ROOT%"
"%CAST%" project-info.md --no-sign
if errorlevel 1 exit /b 1

echo ==========================================
echo whatdbg built: %ROOT%\Builds\Release\whatdbg_artefacts\Release\whatdbg.exe
echo Installed to: %USERPROFILE%\.local\bin\whatdbg.exe
echo ==========================================
endlocal
