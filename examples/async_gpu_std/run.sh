#!/bin/bash
# run.sh — run async_gpu_std example with apex.gdml
#
# Usage:
#   ./run.sh [--sync]
#   GPU_PHOTON_FLUSH_THRESHOLD=1000000 ./run.sh
#   GPU_MAX_QUEUE_SIZE=2 ./run.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

GDML="${SCRIPT_DIR}/apex.gdml"
MACRO="${SCRIPT_DIR}/run.mac"
MODE="${1:---async}"

for f in "$GDML" "$MACRO"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found"
        exit 1
    fi
done

echo "=== async_gpu_std example (std-only worker thread) ==="
echo "GDML:        $GDML"
echo "Macro:       $MACRO"
echo "Mode:        $MODE"
echo "Threshold:   ${GPU_PHOTON_FLUSH_THRESHOLD:-10000000 (default)}"
echo "Max queue:   ${GPU_MAX_QUEUE_SIZE:-3 (default)}"
echo ""

OPTICKS_MAX_BOUNCE=1000 \
async_gpu_std \
    -g "$GDML" \
    -m "$MACRO" \
    "$MODE"

echo ""
echo "=== Done ==="

for f in gpu_hits*.npy g4_hits.npy; do
    [ -f "$f" ] && echo "Output: $f ($(stat -c%s "$f") bytes)"
done
