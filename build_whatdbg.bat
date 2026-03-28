@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake --build "C:\Users\jreng\Documents\Poems\dev\whatdbg\Builds\Ninja" --target whatdbg 1> "C:\Users\jreng\Documents\Poems\dev\whatdbg\build_out.log" 2>&1
echo BUILD_EXIT=%ERRORLEVEL% >> "C:\Users\jreng\Documents\Poems\dev\whatdbg\build_out.log"
