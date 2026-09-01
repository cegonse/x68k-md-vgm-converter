#!/usr/bin/env bash
#
# Populate git-ignored external/ so a developer needs only a C toolchain,
# CMake, and git. Idempotent: re-running updates rather than re-clones.
#
#   - vgmtools (shallow clone + build)  -> external/vgmtools/build/
#   - xgmtool  (sparse SGDK + build)    -> external/xgmtool/build/xgmtool
#   - Cest v5  (header + runner binary) -> external/cest/{cest,cest-runner}
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
EXTERNAL_DIR="$ROOT_DIR/external"
CEST_DIR="$EXTERNAL_DIR/cest"

# Clone sources are kept in one place so a future commit-pin is a one-liner.
VGMTOOLS_URL="https://github.com/vgmrips/vgmtools.git"
SGDK_URL="https://github.com/Stephane-D/SGDK.git"
CEST_RELEASE="https://github.com/cegonse/cest/releases/download/v5"

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------
require() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "error: '$1' is required but was not found in PATH" >&2
        exit 1
    }
}
require git
require cmake
require curl

if [ -n "${CC:-}" ]; then CC_BIN="$CC";
elif command -v cc >/dev/null 2>&1; then CC_BIN="cc";
elif command -v gcc >/dev/null 2>&1; then CC_BIN="gcc";
elif command -v clang >/dev/null 2>&1; then CC_BIN="clang";
else
    echo "error: a C compiler (cc/gcc/clang) is required" >&2
    exit 1
fi

# zlib is a hard requirement (transparent .vgz / gzipped-.vgm decompression).
# Verify by compiling a stub that links against it.
echo "==> checking zlib"
zlib_check_dir="$(mktemp -d)"
cat > "$zlib_check_dir/zlibcheck.c" <<'EOF'
#include <zlib.h>
int main(void) { return zlibVersion()[0] == 0; }
EOF
if ! "$CC_BIN" "$zlib_check_dir/zlibcheck.c" -lz -o "$zlib_check_dir/zlibcheck" >/dev/null 2>&1; then
    rm -rf "$zlib_check_dir"
    echo "error: zlib is required but a stub failed to compile/link against it." >&2
    echo "       install zlib development files, e.g.:" >&2
    echo "         Debian/Ubuntu: apt-get install zlib1g-dev" >&2
    echo "         macOS:         brew install zlib" >&2
    exit 1
fi
rm -rf "$zlib_check_dir"

if command -v sha256sum >/dev/null 2>&1; then
    sha256_of() { sha256sum "$1" | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then
    sha256_of() { shasum -a 256 "$1" | awk '{print $1}'; }
else
    echo "error: need sha256sum or shasum to verify downloads" >&2
    exit 1
fi

mkdir -p "$EXTERNAL_DIR" "$CEST_DIR"

# ---------------------------------------------------------------------------
# Clone/update helpers
# ---------------------------------------------------------------------------
update_repo() {   # dir
    local dir="$1"
    echo "==> updating $(basename "$dir")"
    git -C "$dir" fetch --depth 1 origin >/dev/null 2>&1 || true
    git -C "$dir" reset --hard FETCH_HEAD >/dev/null 2>&1 || true
}

# ---------------------------------------------------------------------------
# vgmtools (shallow)
# ---------------------------------------------------------------------------
VGMTOOLS_DIR="$EXTERNAL_DIR/vgmtools"
if [ -d "$VGMTOOLS_DIR/.git" ]; then
    update_repo "$VGMTOOLS_DIR"
else
    echo "==> cloning vgmtools"
    git clone --depth 1 "$VGMTOOLS_URL" "$VGMTOOLS_DIR"
fi
echo "==> building vgmtools"
cmake -S "$VGMTOOLS_DIR" -B "$VGMTOOLS_DIR/build"
cmake --build "$VGMTOOLS_DIR/build"

# Normalize the tool we rely on (vgm2txt) to a stable path if CMake placed
# it in a subdirectory of the build tree.
VGM2TXT_BIN="$VGMTOOLS_DIR/build/vgm2txt"
if [ ! -x "$VGM2TXT_BIN" ]; then
    found="$(find "$VGMTOOLS_DIR/build" -type f -name vgm2txt -perm -u+x 2>/dev/null | head -n1 || true)"
    if [ -n "$found" ]; then
        cp "$found" "$VGM2TXT_BIN"
    fi
fi

# ---------------------------------------------------------------------------
# xgmtool (sparse checkout of SGDK tools/xgmtool; falls back to shallow clone)
# ---------------------------------------------------------------------------
XGMTOOL_DIR="$EXTERNAL_DIR/xgmtool"
if [ -d "$XGMTOOL_DIR/.git" ]; then
    update_repo "$XGMTOOL_DIR"
    git -C "$XGMTOOL_DIR" sparse-checkout set tools/xgmtool >/dev/null 2>&1 || true
else
    echo "==> sparse-cloning xgmtool from SGDK"
    if git clone --depth 1 --filter=blob:none --sparse "$SGDK_URL" "$XGMTOOL_DIR"; then
        git -C "$XGMTOOL_DIR" sparse-checkout set tools/xgmtool
    else
        echo "==> sparse checkout unavailable; falling back to shallow full clone"
        rm -rf "$XGMTOOL_DIR"
        git clone --depth 1 "$SGDK_URL" "$XGMTOOL_DIR"
    fi
fi
echo "==> building xgmtool"
cmake -S "$XGMTOOL_DIR/tools/xgmtool" -B "$XGMTOOL_DIR/build"
cmake --build "$XGMTOOL_DIR/build"

XGMTOOL_BIN="$XGMTOOL_DIR/build/xgmtool"
if [ ! -x "$XGMTOOL_BIN" ]; then
    found="$(find "$XGMTOOL_DIR/build" -type f -name xgmtool -perm -u+x 2>/dev/null | head -n1 || true)"
    if [ -n "$found" ]; then
        cp "$found" "$XGMTOOL_BIN"
    fi
fi

# ---------------------------------------------------------------------------
# Cest v5 (header + platform-matched runner), from the GitHub release
# ---------------------------------------------------------------------------
case "$(uname -s)" in
    Linux)  CEST_OS="linux" ;;
    Darwin) CEST_OS="macos" ;;
    *)      CEST_OS="windows" ;;
esac
case "$(uname -m)" in
    x86_64|amd64)   CEST_ARCH="x64" ;;
    aarch64|arm64)  CEST_ARCH="aarch64" ;;
    i686|i386)      CEST_ARCH="x86" ;;
    *) echo "error: unsupported host arch '$(uname -m)' for cest-runner" >&2; exit 1 ;;
esac

CEST_ASSET="cest-runner-${CEST_OS}-${CEST_ARCH}"
[ "$CEST_OS" = "windows" ] && CEST_ASSET="${CEST_ASSET}.exe"

case "$CEST_ASSET" in
    cest-runner-linux-aarch64) CEST_SHA="260ac0ecf5a6223405a71dcb70cf35916c3fb8ffc1f9e8290cc788d254e23755" ;;
    cest-runner-linux-x64)     CEST_SHA="a227da96cfe59e6a29e8ab390cffdb507abe7ff24c8ace3e7ddd6caac38d956b" ;;
    cest-runner-linux-x86)     CEST_SHA="6026d144234a756ffdbd37f9000ed937282816a21e8a9fa224e20ef1edd436af" ;;
    cest-runner-macos-aarch64) CEST_SHA="0fca1326fd7382c3186e8719825cf04c7d020e46a57588d8ca80bf8d1cce53c6" ;;
    cest-runner-macos-x64)     CEST_SHA="90b2d1304036788ed01780c9022d11006ae861470345efb087990dc250816c18" ;;
    *) echo "error: no checksum listed for host asset '$CEST_ASSET'" >&2; exit 1 ;;
esac

echo "==> downloading cest-runner ($CEST_ASSET)"
curl -fSL "$CEST_RELEASE/$CEST_ASSET" -o "$CEST_DIR/cest-runner"
actual_sha="$(sha256_of "$CEST_DIR/cest-runner")"
if [ "$actual_sha" != "$CEST_SHA" ]; then
    echo "error: cest-runner checksum mismatch" >&2
    echo "  expected: $CEST_SHA" >&2
    echo "  actual:   $actual_sha" >&2
    exit 1
fi
chmod +x "$CEST_DIR/cest-runner"

echo "==> downloading cest header"
curl -fSL "$CEST_RELEASE/cest" -o "$CEST_DIR/cest"

echo "==> smoke-testing cest-runner"
"$CEST_DIR/cest-runner" --help >/dev/null 2>&1 || {
    echo "error: cest-runner failed its --help smoke test" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Verify expected artifacts
# ---------------------------------------------------------------------------
missing=0
check() {   # path label
    if [ ! -e "$1" ]; then
        echo "error: expected artifact missing: $2 ($1)" >&2
        missing=1
    else
        echo "  ok: $2 -> $1"
    fi
}
echo "==> verifying artifacts"
check "$VGM2TXT_BIN" "vgmtools/vgm2txt"
check "$XGMTOOL_BIN" "xgmtool"
check "$CEST_DIR/cest" "cest header"
check "$CEST_DIR/cest-runner" "cest-runner"
[ "$missing" -eq 0 ] || { echo "init.sh: setup incomplete" >&2; exit 1; }

echo "init.sh: external tools ready."
