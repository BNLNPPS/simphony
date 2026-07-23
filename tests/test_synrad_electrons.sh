#!/usr/bin/env bash
#
# test_synrad_electrons.sh
# =========================
# End-to-end test: GPU vs G4 transport of electron-generated synchrotron
# radiation -- the physically weighted complement of the deterministic
# log-uniform pencil gate (test_synrad_example.sh).
#
# The generation run uses the Geant4 reference app: 18 GeV electrons
# (gamma = 35225) tracked through the benchmark dipole field with stock
# G4SynchrotronRadiation, every SR gamma above the benchmark's 30 eV cut
# recorded at birth in sphoton layout and stack-killed (-B), so the run
# is generation-only. The GPU transports photons only, so the SAME births
# are then fed (-i) to both engines -- the GPU reflect-or-absorb mode on
# the fused tessellated envelope and the synrad_g4 analytic-CSG
# reference -- and the wall-absorption records are compared with the
# same six statistical tests as the pencil gate
# (optiphy/ana/synrad_test.py).
#
# vs the pencil: real K_5/3 spectrum (eps_c ~ 26 keV), tangent emission
# directions, dipole z-distribution; ~3.2 photons/e- above the cut,
# end-cap fraction ~19% vs ~2%.
#
# GPU memory: OPTICKS_MAX_SLOT is sized from the actual births count
# (~64 B/photon). Runs on whatever single GPU the runner exposes -- no
# device pinning.
#
# Usage:
#   ./tests/test_synrad_electrons.sh [nelectron] [seed]
#
# Default 160k electrons -> ~517k photons >= 30 eV: generation ~5 s,
# GPU transport ~0.1 s, G4 transport ~7 s.
#
# Exit code 0 = PASS, 1 = FAIL
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
NELECTRON=${1:-160000}
SEED=${2:-42}

source /opt/simphony/simphony-env.sh 2>/dev/null || true
PREFIX="${SIMPHONY_PREFIX:-/opt/simphony}"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

BUILD_DIR=/tmp/simphony-synrad-electrons-build
RUN_DIR=/tmp/simphony-synrad-electrons-run
rm -rf "$BUILD_DIR" "$RUN_DIR"
mkdir -p "$RUN_DIR"

echo "========================================================"
echo " synrad Test: GPU vs G4 on electron-generated SR photons"
echo "========================================================"
echo "  Electrons: $NELECTRON (18 GeV)"
echo "  Seed:      $SEED"
echo "  Prefix:    $PREFIX"
echo ""

# --- build the external example (both modes) ---
cmake -S "$REPO_DIR/examples/synrad" -B "$BUILD_DIR" \
    -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_BUILD_TYPE=Release > "$RUN_DIR/cmake.log" 2>&1
cmake --build "$BUILD_DIR" >> "$RUN_DIR/cmake.log" 2>&1
GDML="$REPO_DIR/examples/synrad/synrad_bench.gdml"
BIRTHS="$RUN_DIR/sr_births.npy"

# --- generate the SR births (G4 electron mode, generation-only) ---
echo "[GEN] Running synrad_g4 -e $NELECTRON -B ..."
"$BUILD_DIR/synrad_g4" -g analytic -e "$NELECTRON" -B "$BIRTHS" -s "$SEED" -o "$RUN_DIR" 2>&1 \
    | grep "^synrad-g4-births"

NBIRTH=$(python3 -c "import numpy as np; print(np.load('$BIRTHS').shape[0])")
export OPTICKS_MAX_SLOT=$((NBIRTH + 100000))

# --- GPU transport of the births ---
echo "[GPU] Running synrad -i ..."
"$BUILD_DIR/synrad" -g "$GDML" -i "$BIRTHS" -s "$SEED" -o "$RUN_DIR" 2>&1 | grep "^synrad:"

# --- G4 reference transport of the same births ---
echo "[G4]  Running synrad_g4 -i ..."
"$BUILD_DIR/synrad_g4" -g analytic -i "$BIRTHS" -s "$SEED" -o "$RUN_DIR" 2>&1 \
    | grep "^synrad-g4" | tee "$RUN_DIR/g4_summary.txt"

# --- G4 LOST-0 gate: every birth must end in a surface record ---
if ! grep -q "LOST 0 " "$RUN_DIR/g4_summary.txt"; then
    echo "FAIL: G4 mode lost tracks (LOST != 0)"
    exit 1
fi

# --- Compare ---
echo ""
echo "[COMPARE] GPU vs G4 wall-absorption records..."
echo ""

python3 "$REPO_DIR/optiphy/ana/synrad_test.py" \
    "$RUN_DIR/synrad_hits.npy" "$RUN_DIR/synrad_g4_hits.npy" --nphoton "$NBIRTH"
