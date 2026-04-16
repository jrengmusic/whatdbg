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

echo "Building [$CONFIG]..."
cmd.exe //c build.bat $CLEAN $CONFIG

echo "Installing..."
ARTIFACT="Builds/Ninja/whatdbg_App_artefacts/$CONFIG/whatdbg.exe"
INSTALL_DIR="$HOME/.local/bin"
mkdir -p "$INSTALL_DIR"
cp "$ARTIFACT" "$INSTALL_DIR/whatdbg.exe"

echo "Done."
