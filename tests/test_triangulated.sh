#!/usr/bin/env bash
# Force-triangulate a CURVED solid (the water sphere in
# opticks_raindrop_sphere.gdml) and require the GPU hit count to match the
# analytic CSG sphere within tolerance. Unlike a box -- whose 12 triangles
# coincide with the analytic faces, giving bit-identical counts -- a sphere
# tessellates to an INSCRIBED polyhedron, so the triangulated count sits
# just below analytic and converges as the rotation-step resolution rises:
#
#   rotation steps :   6     12     24     48     96   | analytic
#   Opticks hits   : 10938  11950  12313  12322  12323 |  12330
#
# Resolution is user-controllable per solid via U4Mesh env vars, e.g.
#   U4Mesh__NumberOfRotationSteps_solidName_G4_WATER_solid=48  (exact name)
#   U4Mesh__NumberOfRotationSteps_entityType_G4Orb=48          (by solid type)
# and which solids triangulate is the comma-separated stree__force_triangulate_solid
# list. At the default 24 steps the count is within 0.14% of analytic; the 2%
# tolerance below absorbs cross-OptiX-version numerics while still failing hard
# if the triangulated-GAS wiring breaks (zero / grossly wrong hits).
set -e

GDML="$OPTICKS_HOME/tests/geom/opticks_raindrop_sphere.gdml"
MAC="$OPTICKS_HOME/tests/run.mac"
SEED=42
TOL=0.02

ANA=$(USER=ci GEOM=trisphere_ana \
    GPURaytrace -g "$GDML" -m "$MAC" -s "$SEED" 2>&1 \
    | awk '/Opticks: NumHits:/ {print $NF}')

TRI=$(USER=ci GEOM=trisphere_tri stree__force_triangulate_solid=G4_WATER_solid \
    GPURaytrace -g "$GDML" -m "$MAC" -s "$SEED" 2>&1 \
    | awk '/Opticks: NumHits:/ {print $NF}')

echo "analytic=$ANA triangulated=$TRI tol=$TOL"
[ -n "$ANA" ] && [ -n "$TRI" ] || { echo "FAILED: missing hit count (ANA=$ANA TRI=$TRI)"; exit 1; }
OK=$(awk -v a="$ANA" -v t="$TRI" -v tol="$TOL" \
    'BEGIN{ if(a+0<=0){print 0; exit} d=(a>t?a-t:t-a)/a; print (d<=tol)?1:0 }')
[ "$OK" = "1" ] || { echo "FAILED: |analytic-triangulated|/analytic > $TOL (ANA=$ANA TRI=$TRI)"; exit 1; }
echo "PASSED"
