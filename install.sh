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
        echo "Building [$CONFIG]..."
        cmd.exe //c build.bat $CLEAN $CONFIG

        echo "Installing..."
        ARTIFACT="Builds/Ninja/whatdbg_App_artefacts/$CONFIG/whatdbg.exe"
        cp "$ARTIFACT" "$INSTALL_DIR/whatdbg.exe"
        ;;
    Darwin)
        NEEDS_CONFIGURE=0

        if [[ ! -d "Builds/Ninja" ]]; then
            NEEDS_CONFIGURE=1
        else
            STORED_CONFIG=""
            if [[ -f "Builds/Ninja/.build_config" ]]; then
                STORED_CONFIG="$(cat Builds/Ninja/.build_config)"
            fi
            if [[ "$STORED_CONFIG" != "$CONFIG" ]]; then
                rm -rf "Builds/Ninja"
                NEEDS_CONFIGURE=1
            fi
        fi

        if [[ "$CLEAN" == "clean" ]]; then
            rm -rf "Builds/Ninja"
            NEEDS_CONFIGURE=1
        fi

        if [[ "$NEEDS_CONFIGURE" == "1" ]]; then
            cmake -S . -B Builds/Ninja -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG"
            echo "$CONFIG" > "Builds/Ninja/.build_config"
        fi

        echo "Building [$CONFIG]..."
        cmake --build Builds/Ninja

        ARTIFACT="Builds/Ninja/whatdbg_App_artefacts/$CONFIG/whatdbg"

        echo "Installing..."
        cp "$ARTIFACT" "$INSTALL_DIR/whatdbg"
        ;;
    *)
        echo "Unsupported OS: $(uname -s)"
        exit 1
        ;;
esac

echo "Done."
