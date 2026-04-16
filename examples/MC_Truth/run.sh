#!/usr/bin/env bash
# MC_Truth example: run GPURaytrace and inspect the G4 Track ID written per hit.
#
# Prerequisite: build and install from the mc-truth-hit-trackid branch:
#     cmake --build /opt/eic-opticks/build --parallel --target install
#
# The modified GPURaytrace.h (copied into this directory for reference)
# calls SEvt::getHitGenstepIndex(idx) and emits TrackID=<G4 track id>
# at the end of each hit line in opticks_hits_output.txt.

set -euo pipefail

REPO=/workspaces/eic-opticks
BIN=/opt/eic-opticks/bin/GPURaytrace
GDML=${GDML:-$REPO/tests/geom/opticks_raindrop.gdml}
MACRO=${MACRO:-$REPO/tests/run.mac}
SEED=${SEED:-42}
OUTDIR=${OUTDIR:-$(pwd)}

export USER=${USER:-fakeuser}
export GEOM=${GEOM:-fakegeom}

cd "$OUTDIR"
rm -f opticks_hits_output.txt g4_hits_output.txt

"$BIN" -g "$GDML" -m "$MACRO" -s "$SEED"

echo
echo "=== TrackID distribution in opticks_hits_output.txt ==="
awk -F'TrackID=' 'NF>1 {print $2}' opticks_hits_output.txt | sort | uniq -c | sort -rn
echo
echo "First 3 hits:"
head -3 opticks_hits_output.txt
