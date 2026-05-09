#!/bin/bash
#
# test_wavelength_shifting.sh
# ============================
# End-to-end test: GPU vs G4 wavelength shifting physics
#
# Emits UV photons (350nm) from inside a WLS sphere and compares
# GPU (opticks) and G4 hit wavelength distributions, WLS conversion
# rate, and arrival-time distributions using KS tests.
#
# Geometry: tests/geom/wls_test.gdml (introduced in #269)
#   - WLS sphere r=20mm (absorbs UV, re-emits visible)
#   - Detector shell r=28-30mm (100% efficiency)
#   - Air outside the sphere
#
# Usage:
#   ./tests/test_wavelength_shifting.sh [seed]
#
# Exit code 0 = PASS, 1 = FAIL
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SEED=${1:-42}
GEOM_FILE="$REPO_DIR/tests/geom/wls_test.gdml"
CONFIG="wls_test"

# StandAloneGeant4Validation is provided by PR #271. SKIP rather than
# FAIL when it isn't installed so this test is benign on branches where
# #271 hasn't landed yet.
if ! command -v StandAloneGeant4Validation >/dev/null 2>&1; then
    echo "SKIPPED: StandAloneGeant4Validation not installed (depends on PR #271)"
    exit 0
fi

source /opt/eic-opticks/eic-opticks-env.sh 2>/dev/null || true
export OPTICKS_MAX_BOUNCE=100
export OPTICKS_EVENT_MODE=HitPhoton
export OPTICKS_MAX_SLOT=100000

# Override USER/GEOM so opticks writes to a deterministic, test-only path
# regardless of the developer's environment. Matches the convention used
# by tests/test_GPURaytrace.sh and tests/test_GPUPhotonSource_8x8SiPM.sh.
export USER=fakeuser
export GEOM=fakegeom

GPU_HIT_FILE="/tmp/${USER}/opticks/GEOM/${GEOM}/GPUPhotonSourceMinimal/ALL0_no_opticks_event_name/A000/hit.npy"
G4_HIT_FILE="g4_hits.npy"

# Clear any stale outputs from a prior run so we never compare against
# leftover data if the binaries fail silently.
rm -f "$GPU_HIT_FILE" "$G4_HIT_FILE"

echo "=============================================="
echo " WLS Test: GPU vs G4 Wavelength Shifting"
echo "=============================================="
echo "  Geometry: $GEOM_FILE"
echo "  Config:   $CONFIG"
echo "  Seed:     $SEED"
echo ""

# --- GPU run ---
echo "[GPU] Running GPUPhotonSourceMinimal..."
GPU_OUT=$(GPUPhotonSourceMinimal \
    -g "$GEOM_FILE" -c "$CONFIG" -m "$REPO_DIR/tests/run.mac" -s "$SEED" 2>&1)
GPU_HITS=$(echo "$GPU_OUT" | grep "Opticks: NumHits" | head -1 | awk '{print $NF}')
echo "[GPU] Hits: $GPU_HITS"

# --- G4 run ---
echo "[G4]  Running StandAloneGeant4Validation..."
G4_OUT=$(StandAloneGeant4Validation \
    -g "$GEOM_FILE" -c "$CONFIG" -s "$SEED" 2>&1)
G4_HITS=$(echo "$G4_OUT" | grep "Total accumulated hits" | awk '{print $NF}')
echo "[G4]  Hits: $G4_HITS"

# --- Compare ---
echo ""
echo "[COMPARE] Analyzing wavelength and time distributions..."
echo ""

python3 "$REPO_DIR/optiphy/ana/wls_test.py" "$GPU_HIT_FILE" "$G4_HIT_FILE"
