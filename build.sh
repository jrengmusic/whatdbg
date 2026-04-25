#!/usr/bin/env bash
set -e

clear

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CONFIG="Release"
CLEAN="clean"

if [[ "$1" == "debug" ]]; then
    CONFIG="Debug"
    CLEAN=""
fi

INSTALL_DIR="$HOME/.local/bin"
mkdir -p "$INSTALL_DIR"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        # Locate VS via vswhere
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

        if [[ -n "$CLEAN" ]]; then
            echo "Cleaning Builds/Ninja..."
            rm -rf Builds/Ninja
        fi

        NEEDS_CONFIGURE=1
        if [[ -d Builds/Ninja ]]; then
            EXISTING_CONFIG=""
            if [[ -f Builds/Ninja/.build_config ]]; then
                EXISTING_CONFIG="$(cat Builds/Ninja/.build_config)"
            fi
            if [[ "$EXISTING_CONFIG" == "$CONFIG" ]]; then
                NEEDS_CONFIGURE=0
            else
                echo "Config changed [$EXISTING_CONFIG] -> [$CONFIG], reconfiguring..."
                rm -rf Builds/Ninja
            fi
        fi

        if [[ "$NEEDS_CONFIGURE" -eq 1 ]]; then
            echo "Configuring [$CONFIG]..."
            cmake -S . -B Builds/Ninja -G Ninja \
                -DCMAKE_BUILD_TYPE="$CONFIG" \
                -DCMAKE_C_COMPILER="$CC" \
                -DCMAKE_CXX_COMPILER="$CXX"
            echo "$CONFIG" > Builds/Ninja/.build_config
        fi

        echo "Building [$CONFIG]..."
        cmake --build Builds/Ninja -- -j$(nproc)

        ARTIFACT="Builds/Ninja/whatdbg_App_artefacts/$CONFIG/whatdbg.exe"

        echo "Installing..."
        cp "$ARTIFACT" "$INSTALL_DIR/whatdbg.exe"

        ARCH="$(uname -m)"
        if [[ "$ARCH" == "x86_64" ]]; then ARCH="x64"; fi
        mkdir -p dist
        zip -j "dist/whatdbg-win-${ARCH}.zip" "$ARTIFACT"
        echo "Dist: dist/whatdbg-win-${ARCH}.zip"
        ;;
    Darwin)
        HOST_ARCH="$(uname -m)"

        if [[ "$HOST_ARCH" == "arm64" ]]; then
            ARCH_LIST=("arm64" "x86_64")
        else
            ARCH_LIST=("x86_64")
        fi

        for ARCH in "${ARCH_LIST[@]}"; do
            echo "--- Building [$CONFIG] for $ARCH ---"

            rm -rf "Builds/Ninja"
            cmake -S . -B Builds/Ninja -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_OSX_ARCHITECTURES="$ARCH"
            echo "$CONFIG" > "Builds/Ninja/.build_config"

            cmake --build Builds/Ninja

            ARTIFACT="Builds/Ninja/whatdbg_App_artefacts/$CONFIG/whatdbg"

            if [[ "$ARCH" == "$HOST_ARCH" ]]; then
                echo "Installing..."
                cp "$ARTIFACT" "$INSTALL_DIR/whatdbg"
            fi

            mkdir -p dist
            PKG="Builds/Ninja/whatdbg_App_artefacts/$CONFIG/whatdbg.pkg"
            if [[ -f "$PKG" ]]; then
                cp "$PKG" "dist/whatdbg-macos-${ARCH}.pkg"
                echo "Dist: dist/whatdbg-macos-${ARCH}.pkg"
            else
                zip -j "dist/whatdbg-macos-${ARCH}.zip" "$ARTIFACT"
                echo "Dist: dist/whatdbg-macos-${ARCH}.zip"
            fi
        done
        ;;
    *)
        echo "Unsupported OS: $(uname -s)"
        exit 1
        ;;
esac

echo "Done."
