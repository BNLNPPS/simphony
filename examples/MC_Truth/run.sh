#!/usr/bin/env bash
# MC_Truth example: standalone build of GPUMCTruth that demonstrates
# appending the G4 Track ID of the creating particle to each hit line in
# opticks_hits_output.txt when OPTICKS_MC_TRUTH is set.
#
# Setting OPTICKS_MC_TRUTH_BENCH prints an extra "Bench baseline /
# mctruth / delta" block that isolates the MC-truth lookup cost from
# the file I/O dominating the regular hit-write loop.
#
# Build (once):
#     cd examples/MC_Truth
#     cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/eic-opticks
#     cmake --build build --parallel
#
# Run (this script):
#     ./run.sh

set -euo pipefail

REPO=${REPO:-/workspaces/eic-opticks}
BIN=${BIN:-$(dirname "$(realpath "$0")")/build/GPUMCTruth}
GDML=${GDML:-$REPO/tests/geom/opticks_raindrop.gdml}
MACRO=${MACRO:-$REPO/tests/run.mac}
SEED=${SEED:-42}
OUTDIR=${OUTDIR:-$(pwd)}

export USER=${USER:-fakeuser}
export GEOM=${GEOM:-fakegeom}
export OPTICKS_MC_TRUTH=1
export OPTICKS_MC_TRUTH_BENCH=1

cd "$OUTDIR"
rm -f opticks_hits_output.txt g4_hits_output.txt

"$BIN" -g "$GDML" -m "$MACRO" -s "$SEED"

echo
echo "=== TrackID distribution in opticks_hits_output.txt ==="
awk -F'TrackID=' 'NF>1 {print $2}' opticks_hits_output.txt | sort | uniq -c | sort -rn
echo
echo "First 3 hits:"
head -3 opticks_hits_output.txt
