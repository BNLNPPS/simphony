#!/bin/bash
# run.sh — run async_gpu_std example with apex.gdml
#
# Usage:
#   ./run.sh [--sync]
#   GPU_PHOTON_FLUSH_THRESHOLD=1000000 ./run.sh
#   GPU_MAX_QUEUE_SIZE=2 ./run.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

GDML="${REPO_ROOT}/apex.gdml"
MACRO="${REPO_ROOT}/tests/run.mac"
MODE="${1:---async}"

if [ ! -f "$GDML" ]; then
    echo "ERROR: $GDML not found"
    echo "Run from the eic-opticks root or ensure apex.gdml exists."
    exit 1
fi

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
