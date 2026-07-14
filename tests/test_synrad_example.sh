#!/usr/bin/env bash
#
# test_synrad_example.sh
# =======================
# End-to-end test: GPU vs G4 soft X-ray surface reflection transport
#
# Builds the examples/synrad external example against the installed
# simphony and transports the SAME pencil-beam photons through the
# SynradBenchmark tunnel with both modes -- the GPU reflect-or-absorb mode
# (qsim::propagate_gamma, fused tessellated envelope GDML) and the
# Geant4 reference app (synrad_g4: the benchmark's analytic CSG geometry and
# its reflect-or-absorb process continued in place, same Cu table, double
# precision, native CLHEP diffuse smearing, independent RNG) -- and
# statistically compares the wall-absorption records
# (optiphy/ana/synrad_test.py: counts, reflected fraction, z / x / y
# marginals, absorbed-energy spectrum).
#
# The beam is a pure pencil (no angular fan) tilted 7 mrad onto the drift
# walls, and the G4 mode is required to report LOST 0 --
# every photon that enters transport must end in a surface record, so the
# two modes compare the identical ensemble.
#
# GPU memory: the app caps the photon slot allocation to the photon count
# (~64 B/photon, so ~30 MB here) instead of the ~87%-of-VRAM heuristic;
# OPTICKS_MAX_SLOT below is a redundant safeguard for the same. Runs on
# whatever single GPU the runner exposes -- no device pinning.
#
# Usage:
#   ./tests/test_synrad_example.sh [nphoton] [seed]
#
# Exit code 0 = PASS, 1 = FAIL
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
NPHOTON=${1:-500000}
SEED=${2:-42}
BEAM="0,0,100,0,0.007,1,0.3,19.4"

source /opt/simphony/simphony-env.sh 2>/dev/null || true
PREFIX="${SIMPHONY_PREFIX:-/opt/simphony}"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

export OPTICKS_MAX_SLOT=$((NPHOTON + 100000))

BUILD_DIR=/tmp/simphony-synrad-test-build
RUN_DIR=/tmp/simphony-synrad-test-run
rm -rf "$BUILD_DIR" "$RUN_DIR"
mkdir -p "$RUN_DIR"

echo "=============================================="
echo " synrad Test: GPU vs G4 X-ray reflect-or-absorb"
echo "=============================================="
echo "  Photons: $NPHOTON"
echo "  Seed:    $SEED"
echo "  Beam:    -I $BEAM -f 0"
echo "  Prefix:  $PREFIX"
echo ""

# --- build the external example (both modes) ---
cmake -S "$REPO_DIR/examples/synrad" -B "$BUILD_DIR" \
    -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_BUILD_TYPE=Release > "$RUN_DIR/cmake.log" 2>&1
cmake --build "$BUILD_DIR" >> "$RUN_DIR/cmake.log" 2>&1
GDML="$REPO_DIR/examples/synrad/synrad_bench.gdml"

# --- GPU run ---
echo "[GPU] Running synrad..."
"$BUILD_DIR/synrad" -g "$GDML" -n "$NPHOTON" -s "$SEED" -I "$BEAM" -f 0 -o "$RUN_DIR" 2>&1 | grep "^synrad:"

# --- G4 run (same photons, reference app) ---
echo "[G4]  Running synrad_g4..."
"$BUILD_DIR/synrad_g4" -g analytic -n "$NPHOTON" -s "$SEED" -I "$BEAM" -f 0 -o "$RUN_DIR" 2>&1 \
    | grep "^synrad-g4" | tee "$RUN_DIR/g4_summary.txt"

# --- G4 LOST-0 gate: every photon must end in a surface record ---
if ! grep -q "LOST 0 " "$RUN_DIR/g4_summary.txt"; then
    echo "FAIL: G4 mode lost tracks (LOST != 0)"
    exit 1
fi

# --- Compare ---
echo ""
echo "[COMPARE] GPU vs G4 wall-absorption records..."
echo ""

python3 "$REPO_DIR/optiphy/ana/synrad_test.py" \
    "$RUN_DIR/synrad_hits.npy" "$RUN_DIR/synrad_g4_hits.npy" --nphoton "$NPHOTON"
