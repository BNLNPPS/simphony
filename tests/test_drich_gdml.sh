#!/bin/bash
#
# test_drich_gdml.sh
# ==================
# CI gate: Opticks GPU vs Geant4 CPU sensor hits for the standalone (no-DD4hep)
# dRICH GDML geometry (examples/drich). The driver runs both transports in one
# process and prints "gpu_hits=N cpu_hits=M" per event; this asserts they agree
# within Poisson statistics.
#
# This test ALWAYS runs and yields only PASS (0) or FAIL (1) -- there is no skip
# branch. It requires the dRICH geometry fixes (matline carry, phi-wedge
# primitives, wavelength-linear bnd lookup): without the phi-wedge fix the
# segmented dRICH G4Sphere fails to load and the driver aborts, so this FAILS by
# design on a base that lacks those PRs.
#
# Comparison (Poisson rule): for GPU count G and CPU count C the difference error
# is sqrt(G+C); pass when |G-C| <= NSIGMA*sqrt(G+C).
#
# Env (optional): DRICH_BIN MULT NEVENTS SEED NSIGMA TIMEOUT_S MIN_HITS
#                 OPTICKS_PREFIX OPTICKS_MAX_SLOT MAX_STEP_WATCHDOG
#
# Usage:  ./tests/test_drich_gdml.sh        Exit: 0 = PASS, 1 = FAIL

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GDML="$REPO_DIR/examples/drich/drich_ag02.gdml"

MULT="${MULT:-20}"
NEVENTS="${NEVENTS:-5}"
SEED="${SEED:-12345}"
NSIGMA="${NSIGMA:-3}"
TIMEOUT_S="${TIMEOUT_S:-600}"
MIN_HITS="${MIN_HITS:-200}"

fail(){ echo "FAIL: $*"; exit 1; }

echo "=============================================="
echo " dRICH GDML CI gate: Opticks GPU vs Geant4 CPU"
echo "=============================================="

# Hard preconditions -- loud failures, never skipped.
command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi -L >/dev/null 2>&1 \
    || fail "no GPU visible (nvidia-smi -L) -- this gate must run on a GPU runner"
[ -f "$GDML" ] || fail "geometry not found: $GDML"

source /opt/eic-opticks/eic-opticks-env.sh 2>/dev/null || true

# Driver: prefer an installed/explicit binary, else build the example against the
# installed library. A build failure is a real failure, not a skip.
BIN="${DRICH_BIN:-}"
[ -z "$BIN" ] && command -v drich_gdml_main >/dev/null 2>&1 && BIN="drich_gdml_main"
if [ -z "$BIN" ]; then
    BUILD="${TMPDIR:-/tmp}/drich_ci_build"
    LOG="${TMPDIR:-/tmp}/drich_ci_build.log"
    echo "[build] examples/drich ..."
    if cmake -S "$REPO_DIR/examples/drich" -B "$BUILD" \
            -DCMAKE_PREFIX_PATH="${OPTICKS_PREFIX:-/opt/eic-opticks}" >"$LOG" 2>&1 \
       && cmake --build "$BUILD" >>"$LOG" 2>&1; then
        BIN="$BUILD/drich_gdml_main"
    else
        tail -25 "$LOG"
        fail "examples/drich did not build"
    fi
fi

echo "  driver:   $BIN"
echo "  geometry: $GDML"
echo "  config:   MULT=$MULT NEVENTS=$NEVENTS SEED=$SEED NSIGMA=$NSIGMA TIMEOUT_S=$TIMEOUT_S"

# Single deterministic run. MAX_STEP_WATCHDOG kills any photon a Geant4 11.x
# navigator could get stuck on at the aerogel/airgap coincident face, so the run
# always completes; the timeout is only a backstop. A non-zero exit (timeout or
# geometry abort) is a FAILURE, not a skip.
OUT=$(QBND_FILTER_POINT=1 KILL_OPTICAL=0 \
    GDML_FILE="$GDML" OPTICKS_MAX_SLOT="${OPTICKS_MAX_SLOT:-2000000}" \
    MULT="$MULT" SEED="$SEED" NEVENTS="$NEVENTS" PHI_DEG=30 ETA=2.0 \
    MAX_STEP_WATCHDOG="${MAX_STEP_WATCHDOG:-100000}" \
    timeout --signal=KILL "${TIMEOUT_S}s" "$BIN" 2>&1)
rc=$?
[ "$rc" -ne 0 ] && { echo "$OUT" | tail -20; fail "driver exited $rc (timeout/abort) -- geometry may lack the dRICH fixes, or an unrecoverable navigator hang"; }

G=$(echo "$OUT" | grep -oE "gpu_hits=[0-9]+" | grep -oE "[0-9]+" | awk '{s+=$1} END{print s+0}')
C=$(echo "$OUT" | grep -oE "cpu_hits=[0-9]+" | grep -oE "[0-9]+" | awk '{s+=$1} END{print s+0}')
echo "  result:   GPU=$G  CPU=$C"
{ [ "$G" -ge "$MIN_HITS" ] && [ "$C" -ge "$MIN_HITS" ]; } \
    || fail "hit count below floor $MIN_HITS (GPU=$G CPU=$C) -- geometry didn't load or produced no photons"

awk -v g="$G" -v c="$C" -v ns="$NSIGMA" 'BEGIN{
    d=g-c; ad=(d<0)?-d:d; s=ad/sqrt(g+c);
    printf "  ratio GPU/CPU=%.4f  |GPU-CPU|=%d  sqrt(G+C)=%.1f  significance=%.2f sigma (tol %d)\n", g/c, ad, sqrt(g+c), s, ns;
    if (s <= ns) { print "PASS: GPU and CPU agree within tolerance"; exit 0 }
    else         { print "FAIL: GPU vs CPU hit-count difference exceeds tolerance"; exit 1 }
}'
exit $?
