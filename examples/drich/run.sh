#!/bin/bash
# run.sh — run the standalone (no-DD4hep) dRICH GDML example.
#
# Runs the Geant4 + Opticks driver side-by-side and prints per-event
# "gpu_hits=N cpu_hits=M ..." so the GPU/G4 hit ratio can be checked.
# Build first via CMake (find_package(eic-opticks)); see README.md.
#
# Usage:
#   ./run.sh                                            # drich_ag02.gdml, 1 event
#   GDML_FILE=drich_ag02.gdml MULT=100 SEED=12345 NEVENTS=5 ./run.sh
#
# Env (see drich_gdml_main.cc): GDML_FILE MULT SEED NEVENTS PHI_DEG ETA
#   KILL_OPTICAL (1=GPU-only, skips CPU optical transport) CUDA_VISIBLE_DEVICES
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

GDML_FILE="${GDML_FILE:-drich_ag02.gdml}"
if [ ! -f "$GDML_FILE" ]; then
    echo "ERROR: $SCRIPT_DIR/$GDML_FILE not found"
    exit 1
fi

# Locate the built binary (installed/on PATH, or a sibling build dir).
BIN="${BIN:-drich_gdml_main}"
if ! command -v "$BIN" >/dev/null 2>&1 && [ ! -x "$BIN" ]; then
    echo "ERROR: '$BIN' not found — build it first (see README.md) or set BIN=/path/to/drich_gdml_main"
    exit 1
fi

echo "=== standalone dRICH GDML example ==="
echo "GDML:    $GDML_FILE"
echo "events:  ${NEVENTS:-1}   MULT: ${MULT:-1}   SEED: ${SEED:-1}   PHI_DEG: ${PHI_DEG:-30}   ETA: ${ETA:-2.0}"
echo ""

GDML_FILE="$GDML_FILE" \
MULT="${MULT:-1}" SEED="${SEED:-1}" NEVENTS="${NEVENTS:-1}" \
PHI_DEG="${PHI_DEG:-30}" ETA="${ETA:-2.0}" \
"$BIN"

echo ""
echo "=== Done ==="
