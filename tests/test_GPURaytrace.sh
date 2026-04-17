#!/usr/bin/env bash

set -e

SEED=42
TOLERANCE=113
PASS=true

GEANT4_VERSION=$(geant4-version version)
GEANT4_SERIES=$(geant4-version series)

declare -Ar SUPPORTED_GEANT4_SERIES=(
    ["11.3"]=1
    ["11.4+"]=1
)

declare -Ar EXPECTED_G4_HITS=(
    ["11.3:cerenkov"]=12672
    ["11.3:cerenkov_scintillation"]=17473
    ["11.4+:cerenkov"]=11842
    ["11.4+:cerenkov_scintillation"]=21411
)

declare -Ar EXPECTED_OPTICKS_HITS=(
    ["11.3:cerenkov"]=12664
    ["11.3:cerenkov_scintillation"]=17443
    ["11.4+:cerenkov"]=11827
    ["11.4+:cerenkov_scintillation"]=21390
)

set_expected_hits() {
    local test_name=$1
    local key="${GEANT4_SERIES}:${test_name}"

    if [[ -z ${SUPPORTED_GEANT4_SERIES[$GEANT4_SERIES]+x} ]]; then
        echo "FAILED: unsupported Geant4 version $GEANT4_VERSION. Add hit-count references for this release."
        exit 1
    fi

    if [[ -z ${EXPECTED_G4_HITS[$key]+x} || -z ${EXPECTED_OPTICKS_HITS[$key]+x} ]]; then
        echo "FAILED: unknown test name: $test_name"
        exit 1
    fi

    EXPECTED_G4=${EXPECTED_G4_HITS[$key]}
    EXPECTED_OPTICKS=${EXPECTED_OPTICKS_HITS[$key]}
}

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

echo "Using Geant4 version $GEANT4_VERSION"

# ---- Test 1: Cerenkov only (opticks_raindrop.gdml) ----
echo "=== Test 1: Cerenkov only (opticks_raindrop.gdml) ==="
set_expected_hits cerenkov

echo "Running GPURaytrace with seed $SEED ..."
OUTPUT=$(USER=fakeuser GEOM=fakegeom GPURaytrace \
    -g "$OPTICKS_HOME/tests/geom/opticks_raindrop.gdml" \
    -m "$OPTICKS_HOME/tests/run.mac" \
    -s "$SEED" 2>&1)

G4_HITS=$(echo "$OUTPUT" | grep "Geant4: NumHits:" | awk '{print $NF}')
OPTICKS_HITS=$(echo "$OUTPUT" | grep "Opticks: NumHits:" | awk '{print $NF}')

echo "Geant4:  NumHits: $G4_HITS  (expected $EXPECTED_G4 +/- $TOLERANCE)"
echo "Opticks: NumHits: $OPTICKS_HITS  (expected $EXPECTED_OPTICKS +/- $TOLERANCE)"

check_hits "Geant4 NumHits"  "$G4_HITS"      "$EXPECTED_G4"
check_hits "Opticks NumHits" "$OPTICKS_HITS" "$EXPECTED_OPTICKS"

# ---- Test 2: Cerenkov + Scintillation (opticks_raindrop_with_scintillation.gdml) ----
echo ""
echo "=== Test 2: Cerenkov + Scintillation (opticks_raindrop_with_scintillation.gdml) ==="
set_expected_hits cerenkov_scintillation

echo "Running GPURaytrace with seed $SEED ..."
OUTPUT=$(USER=fakeuser GEOM=fakegeom GPURaytrace \
    -g "$OPTICKS_HOME/tests/geom/opticks_raindrop_with_scintillation.gdml" \
    -m "$OPTICKS_HOME/tests/run.mac" \
    -s "$SEED" 2>&1)

G4_HITS=$(echo "$OUTPUT" | grep "Geant4: NumHits:" | awk '{print $NF}')
OPTICKS_HITS=$(echo "$OUTPUT" | grep "Opticks: NumHits:" | awk '{print $NF}')

echo "Geant4:  NumHits: $G4_HITS  (expected $EXPECTED_G4 +/- $TOLERANCE)"
echo "Opticks: NumHits: $OPTICKS_HITS  (expected $EXPECTED_OPTICKS +/- $TOLERANCE)"

check_hits "Geant4 NumHits"  "$G4_HITS"      "$EXPECTED_G4"
check_hits "Opticks NumHits" "$OPTICKS_HITS" "$EXPECTED_OPTICKS"

# ---- Test 3: Cerenkov + Re-emission, no Scintillation (opticks_raindrop_reemit_no_scint.gdml) ----
echo ""
echo "=== Test 3: Cerenkov + Re-emission, no Scintillation (opticks_raindrop_reemit_no_scint.gdml) ==="

echo "Running GPURaytrace with seed $SEED ..."
USER=fakeuser GEOM=fakegeom GPURaytrace \
    -g "$OPTICKS_HOME/tests/geom/opticks_raindrop_reemit_no_scint.gdml" \
    -m "$OPTICKS_HOME/tests/run.mac" \
    -s "$SEED"

# ---- Summary ----
echo ""
if [ "$PASS" = true ]; then
    echo "All tests passed"
    exit 0
else
    echo "Some tests FAILED"
    exit 1
fi
