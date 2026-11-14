#!/usr/bin/env bash
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        VSWHERE="$(cygpath -u "$(printenv 'ProgramFiles(x86)')/Microsoft Visual Studio/Installer/vswhere.exe")"
        VS_PATH="$("$VSWHERE" -latest -property installationPath | tr -d '\r')"
        VCVARSALL="$VS_PATH\\VC\\Auxiliary\\Build\\vcvarsall.bat"

        echo "Setting up MSVC x64 environment..."
        while IFS='=' read -r key value; do
            key="${key//$'\r'/}"
            value="${value//$'\r'/}"
            if [[ -n "$key" ]]; then
                export "$key=$value"
            fi
        done < <(cmd.exe //c "call \"$VCVARSALL\" x64 >nul 2>&1 && set")

        export PATH="$(cygpath -u "$VS_PATH")/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja:$PATH"
        ;;
esac

mkdir -p build

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        mkdir -p build/obj
        for name in fixture fixture_wait fixture_crash fixture_load; do
            cl /std:c++17 /Zi /Od /EHsc "$name.cpp" /Fe:"build\\$name.exe" /Fo:build\\obj\\ /Fd:build\\
        done
        cl /std:c++17 /Zi /Od /EHsc /LD fixture_module.cpp /Fe:build\\fixture_module.dll /Fo:build\\obj\\ /Fd:build\\
        ;;
    *)
        for name in fixture fixture_wait fixture_crash fixture_load; do
            c++ -std=c++17 -g -O0 "$name.cpp" -o "build/$name"
        done
        c++ -std=c++17 -g -O0 -dynamiclib fixture_module.cpp -o build/libfixture_module.dylib
        ;;
esac
