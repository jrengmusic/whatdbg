@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo vcvars64 failed
    exit /b 1
)
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -B build_vc -S "%~dp0"
if errorlevel 1 (
    echo cmake configure failed
    exit /b 1
)
ninja -C build_vc
if errorlevel 1 (
    echo ninja build failed
    exit /b 1
)
echo BUILD SUCCEEDED
