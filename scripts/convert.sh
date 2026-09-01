#!/usr/bin/env bash
#
# Full pipeline: X68000 .vgm -> (this tool) -> Mega Drive .vgm -> (xgmtool) -> .xgm
#
#   scripts/convert.sh input.vgm output.xgm [--fm-channels 0,1,2,3,5,7] \
#                      [--keep-intermediate] [xgmtool flags...]
#
# Stage 1 (this tool) produces an intermediate MD VGM; stage 2 (xgmtool)
# converts it to XGM. Stops on the first failing stage and says which.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TOOL_BIN="$ROOT_DIR/build/x68k-md-vgm-conv"
XGMTOOL_BIN="$ROOT_DIR/external/xgmtool/build/xgmtool"

usage() {
    echo "usage: $0 input.vgm output.xgm [--fm-channels list] [--keep-intermediate] [xgmtool flags...]" >&2
    exit 2
}

[ "$#" -ge 2 ] || usage
INPUT="$1"; shift
OUTPUT="$1"; shift

# Split remaining args: this tool's flags (--fm-channels) vs xgmtool's.
TOOL_ARGS=()
XGMTOOL_ARGS=()
KEEP_INTERMEDIATE=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --fm-channels)
            [ "$#" -ge 2 ] || { echo "error: --fm-channels needs a value" >&2; exit 2; }
            TOOL_ARGS+=("$1" "$2"); shift 2 ;;
        --fm-channels=*)
            TOOL_ARGS+=("$1"); shift ;;
        --keep-intermediate)
            KEEP_INTERMEDIATE=1; shift ;;
        *)
            XGMTOOL_ARGS+=("$1"); shift ;;
    esac
done

if [ ! -x "$TOOL_BIN" ]; then
    echo "error: tool binary not found at $TOOL_BIN" >&2
    echo "       build it first: make" >&2
    exit 1
fi
if [ ! -x "$XGMTOOL_BIN" ]; then
    echo "error: xgmtool not found at $XGMTOOL_BIN" >&2
    echo "       set up external tools first: make init" >&2
    exit 1
fi

INTERMEDIATE="$(mktemp -t x68k-md-XXXXXX.vgm)"
cleanup() {
    if [ "$KEEP_INTERMEDIATE" -eq 1 ]; then
        echo "intermediate MD VGM kept at: $INTERMEDIATE" >&2
    else
        rm -f "$INTERMEDIATE"
    fi
}
trap cleanup EXIT

echo "==> stage 1: transcribe X68000 VGM -> Mega Drive VGM"
if ! "$TOOL_BIN" "$INPUT" "$INTERMEDIATE" "${TOOL_ARGS[@]}"; then
    echo "error: stage 1 (transcriber) failed" >&2
    exit 1
fi

echo "==> stage 2: convert Mega Drive VGM -> XGM (xgmtool)"
if ! "$XGMTOOL_BIN" "${XGMTOOL_ARGS[@]}" "$INTERMEDIATE" "$OUTPUT"; then
    echo "error: stage 2 (xgmtool) failed" >&2
    exit 1
fi

echo "done: $OUTPUT"
