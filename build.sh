#!/usr/bin/env bash
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

BUILD_DIR="Builds/Release"

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

echo "Configuring cast (Release)..."
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "Building cast..."
ninja -C "$BUILD_DIR"
