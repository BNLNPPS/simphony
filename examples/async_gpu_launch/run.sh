#!/bin/bash
# run.sh — Run async GPU launch example with apex.gdml
#
# Usage:
#   ./run.sh [--sync]         # default is async mode
#   GPU_PHOTON_FLUSH_THRESHOLD=1000000 ./run.sh   # custom threshold
#
# The photon flush threshold controls how many photons accumulate before
# a GPU batch is submitted.  Default is 10M.  Lower values give more
# overlap at the cost of smaller GPU batches.

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

echo "=== Async GPU Launch Example ==="
echo "GDML:      $GDML"
echo "Macro:     $MACRO"
echo "Mode:      $MODE"
echo "Threshold: ${GPU_PHOTON_FLUSH_THRESHOLD:-10000000 (default)}"
echo ""

OPTICKS_MAX_BOUNCE=1000 \
async_gpu_launch \
    -g "$GDML" \
    -m "$MACRO" \
    "$MODE"

echo ""
echo "=== Done ==="

# Show output files
for f in gpu_hits*.npy g4_hits.npy; do
    [ -f "$f" ] && echo "Output: $f ($(stat -c%s "$f") bytes)"
done
