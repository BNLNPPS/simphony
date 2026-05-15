#!/usr/bin/env bash
# Forcing triangulation of a box-shaped solid (here G4_WATER_solid in
# opticks_raindrop.gdml) must produce bit-identical GPU hit counts vs
# the analytic CSG path: a G4Box triangulates to 12 flat triangles that
# coincide with the analytic surface, so the only thing this test can
# detect breaking is the triangulated-GAS wiring (pipeline flags, second
# hitgroup PG, per-record SBT header packing).
set -e

GDML="$OPTICKS_HOME/tests/geom/opticks_raindrop.gdml"
MAC="$OPTICKS_HOME/tests/run.mac"
SEED=42

ANA=$(USER=ci GEOM=triraindrop_ana \
    GPURaytrace -g "$GDML" -m "$MAC" -s "$SEED" 2>&1 \
    | awk '/Opticks: NumHits:/ {print $NF}')

TRI=$(USER=ci GEOM=triraindrop_tri stree__force_triangulate_solid=G4_WATER_solid \
    GPURaytrace -g "$GDML" -m "$MAC" -s "$SEED" 2>&1 \
    | awk '/Opticks: NumHits:/ {print $NF}')

echo "analytic=$ANA triangulated=$TRI"
[ -n "$ANA" ] && [ "$ANA" = "$TRI" ] || { echo "FAILED: triangulated($TRI) != analytic($ANA)"; exit 1; }
echo "PASSED"
