#!/usr/bin/env bash

set -e


SEED=42
TOLERANCE=113
PASS=true

check_hits() {
    local label=$1
    local actual=$2
    local expected=$3
    local lo=$((expected - TOLERANCE))
    local hi=$((expected + TOLERANCE))
    if [ "$actual" -ge "$lo" ] && [ "$actual" -le "$hi" ]; then
        echo "PASSED: $label ($actual) is within [$lo, $hi]"
    else
        echo "FAILED: $label ($actual) is outside [$lo, $hi]"
        PASS=false
    fi
}

# ---- Test 1: Cerenkov only (opticks_raindrop.gdml) ----
echo "=== Test 1: Cerenkov only (opticks_raindrop.gdml) ==="
echo "Running GPURaytrace with seed $SEED ..."
OUTPUT=$(USER=fakeuser GEOM=fakegeom GPURaytrace \
    -g "$OPTICKS_HOME/tests/geom/opticks_raindrop.gdml" \
    -m "$OPTICKS_HOME/tests/run.mac" \
    -s "$SEED" 2>&1)

G4_HITS=$(echo "$OUTPUT" | grep "Geant4: NumHits:" | awk '{print $NF}')
OPTICKS_HITS=$(echo "$OUTPUT" | grep "Opticks: NumHits:" | awk '{print $NF}')

echo "Geant4:  NumHits: $G4_HITS  (expected 12672 +/- $TOLERANCE)"
echo "Opticks: NumHits: $OPTICKS_HITS  (expected 12664 +/- $TOLERANCE)"

check_hits "Geant4 NumHits"  "$G4_HITS"      12672
check_hits "Opticks NumHits" "$OPTICKS_HITS"  12664

# ---- Test 2: Cerenkov + Scintillation (opticks_raindrop_with_scintillation.gdml) ----
echo ""
echo "=== Test 2: Cerenkov + Scintillation (opticks_raindrop_with_scintillation.gdml) ==="
echo "Running GPURaytrace with seed $SEED ..."
OUTPUT=$(USER=fakeuser GEOM=fakegeom GPURaytrace \
    -g "$OPTICKS_HOME/tests/geom/opticks_raindrop_with_scintillation.gdml" \
    -m "$OPTICKS_HOME/tests/run.mac" \
    -s "$SEED" 2>&1)

G4_HITS=$(echo "$OUTPUT" | grep "Geant4: NumHits:" | awk '{print $NF}')
OPTICKS_HITS=$(echo "$OUTPUT" | grep "Opticks: NumHits:" | awk '{print $NF}')

echo "Geant4:  NumHits: $G4_HITS  (expected 17473 +/- $TOLERANCE)"
echo "Opticks: NumHits: $OPTICKS_HITS  (expected 17443 +/- $TOLERANCE)"

check_hits "Geant4 NumHits"  "$G4_HITS"      17473
check_hits "Opticks NumHits" "$OPTICKS_HITS"  17443

# ---- Summary ----
echo ""
if [ "$PASS" = true ]; then
    echo "All tests passed"
    exit 0
else
    echo "Some tests FAILED"
    exit 1
fi
