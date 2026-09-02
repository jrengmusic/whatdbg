
                        Codegen Annotated Source of Truth
————————————————————————————————————————————————————————————————————————————————

            ░░████████████░░████████████░░████████████░░████████████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████        ░░████  ░░████░░████            ░░████
            ░░████        ░░████████████░░████████████    ░░████
            ░░████        ░░████  ░░████        ░░████    ░░████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████████████░░████  ░░████░░████████████    ░░████

————————————————————————————————————————————————————————————————————————————————
                         FOR YOUR EYES ONLY, DO NOT EDIT


#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CONFIG="Release"
INSTALL_DIR="$HOME/.local/bin"
mkdir -p "$INSTALL_DIR"

VSWHERE="$(cygpath -u "$PROGRAMFILES/Microsoft Visual Studio/Installer/vswhere.exe")"
VS_PATH="$("$VSWHERE" -latest -property installationPath)"
VCVARSALL="$(cygpath -w "$VS_PATH")\VC\Auxiliary\Build\vcvarsall.bat"

echo "Setting up MSVC x64 environment..."
eval "$(cmd.exe //c "\"$VCVARSALL\" x64 >nul 2>&1 && set" 2>/dev/null | sed 's/\r$//' | sed -n 's/^\([^=]*\)=\(.*\)$/export \1="\2"/p')"

export CC=cl
export CXX=cl

VS_CMAKE="$(cygpath -u "$VS_PATH/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin")"
VS_NINJA="$(cygpath -u "$VS_PATH/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja")"
export PATH="$VS_CMAKE:$VS_NINJA:$PATH"

echo "Configuring [$CONFIG]..."
cmake -S . -B "Builds/$CONFIG" -G Ninja \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX"

echo "Building [$CONFIG]..."
cmake --build "Builds/$CONFIG" -- -j$(nproc)

ARTIFACT="Builds/$CONFIG/whatdbg_artefacts/$CONFIG/whatdbg.exe"

echo "Installing..."
cp "$ARTIFACT" "$INSTALL_DIR/whatdbg.exe"

ARCH="$(uname -m)"
if [[ "$ARCH" == "x86_64" ]]; then ARCH="x64"; fi
mkdir -p dist
zip -j "dist/whatdbg-win-${ARCH}.zip" "$ARTIFACT"
echo "Dist: dist/whatdbg-win-${ARCH}.zip"
