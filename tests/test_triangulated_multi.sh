#!/usr/bin/env bash
# Demonstrate SELECTIVE, per-solid triangulation. opticks_two_spheres.gdml holds
# two curved water detectors on the 5 GeV e- track: WATER_A at the beam origin
# and WATER_B downstream. stree__force_triangulate_solid takes a comma-separated
# list of EXACT solid names and triangulates only those; everything else stays
# analytic CSG. Coarsening every G4Orb to 12 rotation steps
# (U4Mesh__NumberOfRotationSteps_entityType_G4Orb=12) makes a triangulated
# detector lose a clear, deterministic number of hits, so the four runs separate:
#
#   analytic   : 51087
#   force A    : 50522   (only A triangulated)
#   force B    : 50338   (only B triangulated)
#   force A,B  : 49706   (both)
#
# Selectivity check: forceA and forceB each sit strictly BETWEEN analytic and
# forceA,B. If naming A also triangulated B, forceA would equal forceA,B -- so
# this ordering proves each named solid triangulates only itself.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

GDML="$SCRIPT_DIR/geom/opticks_two_spheres.gdml"
MAC="$SCRIPT_DIR/run.mac"
SEED=42
RES=U4Mesh__NumberOfRotationSteps_entityType_G4Orb=12
hits(){ awk '/Opticks: NumHits:/ {print $NF}'; }

ANA=$(GPURaytrace -g "$GDML" -m "$MAC" -s "$SEED" 2>&1 | hits)
A=$( env "$RES" stree__force_triangulate_solid=WATER_A_solid               GPURaytrace -g "$GDML" -m "$MAC" -s "$SEED" 2>&1 | hits)
B=$( env "$RES" stree__force_triangulate_solid=WATER_B_solid               GPURaytrace -g "$GDML" -m "$MAC" -s "$SEED" 2>&1 | hits)
AB=$(env "$RES" stree__force_triangulate_solid=WATER_A_solid,WATER_B_solid GPURaytrace -g "$GDML" -m "$MAC" -s "$SEED" 2>&1 | hits)

echo "analytic=$ANA forceA=$A forceB=$B forceAB=$AB"
for v in "$ANA" "$A" "$B" "$AB"; do
    [ -n "$v" ] || { echo "FAILED: missing hit count (analytic=$ANA A=$A B=$B AB=$AB)"; exit 1; }
done

OK=$(awk -v ana="$ANA" -v a="$A" -v b="$B" -v ab="$AB" \
    'BEGIN{ print (ana>a && ana>b && a>ab && b>ab) ? 1 : 0 }')
[ "$OK" = "1" ] || { echo "FAILED: expected analytic > forceA,forceB > forceA,B (selective triangulation)"; exit 1; }
echo "PASSED"
