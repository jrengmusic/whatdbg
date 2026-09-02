#!/usr/bin/env bash
set -euo pipefail

# Pinned LLVM version
LLVM_TAG="llvmorg-21.1.8"

# Repo-root anchoring
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Paths
WORK_DIR="$REPO_ROOT/Builds/liblldb"
SRC_DIR="$WORK_DIR/llvm-project"
BUILD_DIR="$WORK_DIR/cmake"
DIST_DIR="$REPO_ROOT/Resources/macos"

# Fetch llvm-project source at pinned tag (resumable tarball, not git clone)
if [[ -d "$SRC_DIR" ]]; then
    echo "=== WARNING: $SRC_DIR already exists. ==="
    echo "=== To change the tag, remove $SRC_DIR manually and re-run. ==="
    echo "=== Proceeding with the existing source tree. ==="
else
    TARBALL="$WORK_DIR/llvm-project-${LLVM_TAG}.tar.gz"
    TARBALL_URL="https://github.com/llvm/llvm-project/archive/refs/tags/${LLVM_TAG}.tar.gz"
    EXTRACTED_DIR="$WORK_DIR/llvm-project-${LLVM_TAG}"

    mkdir -p "$WORK_DIR"

    echo "=== Downloading $TARBALL_URL ==="
    # -L: follow redirects (GitHub redirects to codeload CDN).
    # --fail: exit non-zero on HTTP errors so set -e catches.
    # -C -: resume from partial download on re-invocation.
    # --retry 5 --retry-delay 5 --retry-all-errors: internal retries with resume.
    curl -L --fail -C - --retry 5 --retry-delay 5 --retry-all-errors \
        -o "$TARBALL" "$TARBALL_URL"

    echo "=== Extracting tarball ==="
    tar -xzf "$TARBALL" -C "$WORK_DIR"

    # GitHub tarballs extract to <repo>-<tag>/ — rename to our canonical $SRC_DIR.
    if [[ -d "$EXTRACTED_DIR" ]]; then
        mv "$EXTRACTED_DIR" "$SRC_DIR"
    else
        echo "ERROR: expected $EXTRACTED_DIR after extraction, not found" >&2
        exit 1
    fi

    # Remove tarball after successful extraction to reclaim disk (~200 MB compressed).
    rm -f "$TARBALL"
fi

# Clean cmake build dir (force reconfigure)
if [[ -d "$BUILD_DIR" ]]; then
    echo "=== Cleaning previous cmake build directory ==="
    rm -rf "$BUILD_DIR"
fi

# Configure build (CMake + Ninja) — builds a universal (arm64+x86_64) dylib,
# split into per-arch thin dylibs below
cmake -G Ninja \
    -S "$SRC_DIR/llvm" \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DLLVM_ENABLE_PROJECTS="clang;lldb" \
    -DLLVM_ENABLE_ZSTD=OFF \
    -DLLVM_TARGETS_TO_BUILD="AArch64;X86" \
    -DLLDB_ENABLE_PYTHON=OFF \
    -DLLDB_ENABLE_LUA=OFF \
    -DLLDB_ENABLE_LIBEDIT=OFF \
    -DLLDB_ENABLE_CURSES=OFF \
    -DLLDB_ENABLE_LIBXML2=OFF \
    -DLLDB_INCLUDE_TESTS=OFF \
    -DLLDB_USE_SYSTEM_DEBUGSERVER=ON

# Build only the liblldb target
cmake --build "$BUILD_DIR" --target liblldb

# Stage outputs under $DIST_DIR
mkdir -p "$DIST_DIR/arm64" "$DIST_DIR/x86_64" "$DIST_DIR/include/lldb/API" "$DIST_DIR/licenses"

# Remove stale universal dylib from previous layout
rm -f "$DIST_DIR/liblldb.dylib"

# Resolve versioned dylib — fail loud on zero or multiple matches
DYLIB_MATCHES=()
while IFS= read -r -d '' match; do
    DYLIB_MATCHES+=("$match")
done < <(find "$BUILD_DIR/lib" -maxdepth 1 -name "liblldb.*.dylib" -print0)

if [[ ${#DYLIB_MATCHES[@]} -eq 0 ]]; then
    echo "ERROR: no liblldb.*.dylib found under $BUILD_DIR/lib" >&2
    exit 1
fi

if [[ ${#DYLIB_MATCHES[@]} -gt 1 ]]; then
    echo "ERROR: multiple liblldb.*.dylib found under $BUILD_DIR/lib — ambiguous:" >&2
    printf '  %s\n' "${DYLIB_MATCHES[@]}" >&2
    exit 1
fi

# Stage universal with corrected install_name, then split into per-arch thin dylibs
cp "${DYLIB_MATCHES[0]}" "$DIST_DIR/liblldb-universal.dylib"
install_name_tool -id "@rpath/liblldb.dylib" "$DIST_DIR/liblldb-universal.dylib"

lipo "$DIST_DIR/liblldb-universal.dylib" -thin arm64  -output "$DIST_DIR/arm64/liblldb.dylib"
lipo "$DIST_DIR/liblldb-universal.dylib" -thin x86_64 -output "$DIST_DIR/x86_64/liblldb.dylib"

# Strip local symbols — keep externals for dlopen resolution
strip -x "$DIST_DIR/arm64/liblldb.dylib"
strip -x "$DIST_DIR/x86_64/liblldb.dylib"

rm -f "$DIST_DIR/liblldb-universal.dylib"

# Copy SB API headers
cp -R "$SRC_DIR/lldb/include/lldb/API/." "$DIST_DIR/include/lldb/API/"
cp "$SRC_DIR/lldb/include/lldb/"lldb-*.h "$DIST_DIR/include/lldb/"
cp "$BUILD_DIR/tools/lldb/include/lldb/API/"*.h "$DIST_DIR/include/lldb/API/"

# Copy license — check both known filenames; fail loud if neither exists
if [[ -f "$SRC_DIR/llvm/LICENSE.TXT" ]]; then
    cp "$SRC_DIR/llvm/LICENSE.TXT" "$DIST_DIR/licenses/LLVM-LICENSE.TXT"
elif [[ -f "$SRC_DIR/llvm/LICENSE" ]]; then
    cp "$SRC_DIR/llvm/LICENSE" "$DIST_DIR/licenses/LLVM-LICENSE.TXT"
else
    echo "ERROR: LLVM license file not found at $SRC_DIR/llvm/LICENSE.TXT or $SRC_DIR/llvm/LICENSE" >&2
    exit 1
fi

# Report sizes
ARM64_SIZE_BYTES="$(stat -f%z "$DIST_DIR/arm64/liblldb.dylib")"
ARM64_SIZE_MB="$(echo "scale=2; $ARM64_SIZE_BYTES / 1048576" | bc)"
X86_SIZE_BYTES="$(stat -f%z "$DIST_DIR/x86_64/liblldb.dylib")"
X86_SIZE_MB="$(echo "scale=2; $X86_SIZE_BYTES / 1048576" | bc)"

echo ""
echo "==================================================================="
echo "=== BUILD REPORT ==================================================="
echo "==================================================================="
echo "LLVM_TAG:     $LLVM_TAG"
echo "arm64:        $DIST_DIR/arm64/liblldb.dylib  (${ARM64_SIZE_MB} MB)"
echo "x86_64:       $DIST_DIR/x86_64/liblldb.dylib (${X86_SIZE_MB} MB)"
echo ""
echo "--- arm64 ---"
file "$DIST_DIR/arm64/liblldb.dylib"
echo "--- x86_64 ---"
file "$DIST_DIR/x86_64/liblldb.dylib"
echo "==================================================================="
