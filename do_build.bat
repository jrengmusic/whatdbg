@echo off
setlocal

set VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community
set MSVC_VER=14.44.35207
set WIN_SDK_VER=10.0.26100.0

call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

set NINJA_PATH=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja
set PATH=%NINJA_PATH%;%PATH%

if exist "Builds\Ninja" rmdir /s /q "Builds\Ninja"

echo [1/2] Configuring...
cmake -S . -B Builds\Ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 (
    echo CMake configure FAILED
    exit /b 1
)

echo [2/2] Building...
cmake --build Builds\Ninja --config Debug -- -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo Build FAILED
    exit /b 1
)

echo.
echo Build succeeded.
if exist "Builds\Ninja\whatdbg.exe" (
    echo whatdbg.exe: OK
) else (
    echo whatdbg.exe: NOT FOUND
    exit /b 1
)
endlocal
