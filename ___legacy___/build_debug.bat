@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" > nul

set MSVC_BIN=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64

cd /d "%~dp0"
rmdir /s /q build 2>nul

cmake -G Ninja ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_CXX_COMPILER="%MSVC_BIN%\cl.exe" ^
    -DCMAKE_LINKER="%MSVC_BIN%\link.exe" ^
    -B build .

ninja -C build
