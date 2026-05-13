#!/usr/bin/env bash

set -e
export OPTICKS_MAX_SLOT=M1

SEED=42
PASS=true
PHOTON_FILE=$(mktemp /tmp/test_photons_XXXXXX.txt)
HITS_FILE=opticks_hits_output.txt

cleanup() {
    rm -f "$PHOTON_FILE" "$HITS_FILE"
}
trap cleanup EXIT

# ---- Test 1: Basic file loading and GPU simulation ----
echo "=== Test 1: 10 photons from text file ==="

cat > "$PHOTON_FILE" <<'EOF'
# pos_x pos_y pos_z time mom_x mom_y mom_z pol_x pol_y pol_z wavelength
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  420.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  420.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  420.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  450.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  450.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  500.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  500.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  500.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  500.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  500.0
EOF

echo "Running GPUPhotonFileSource with seed $SEED ..."
OUTPUT=$(GPUPhotonFileSource \
    -g "$OPTICKS_HOME/tests/geom/opticks_raindrop.gdml" \
    -p "$PHOTON_FILE" \
    -m "$OPTICKS_HOME/tests/run.mac" \
    -s "$SEED" 2>&1)

LOADED=$(echo "$OUTPUT" | grep "Loaded" | awk '{print $2}')
OPTICKS_HITS=$(echo "$OUTPUT" | grep "Opticks: NumHits:" | awk '{print $NF}')

echo "Loaded:  $LOADED photons"
echo "Opticks: NumHits: $OPTICKS_HITS"

if [ "$LOADED" -eq 10 ]; then
    echo "PASSED: Loaded correct number of photons (10)"
else
    echo "FAILED: Expected 10 loaded photons, got $LOADED"
    PASS=false
fi

if [ "$OPTICKS_HITS" -eq 10 ]; then
    echo "PASSED: All 10 photons produced hits"
else
    echo "FAILED: Expected 10 hits, got $OPTICKS_HITS"
    PASS=false
fi

# Check output file exists and has correct number of lines
if [ ! -f "$HITS_FILE" ]; then
    echo "FAILED: Output file $HITS_FILE not found"
    PASS=false
else
    HIT_LINES=$(wc -l < "$HITS_FILE")
    if [ "$HIT_LINES" -eq 10 ]; then
        echo "PASSED: Output file has 10 hit lines"
    else
        echo "FAILED: Expected 10 lines in output, got $HIT_LINES"
        PASS=false
    fi

    # Check wavelengths are preserved (3 at 420, 2 at 450, 5 at 500)
    N420=$(grep -c " 420 " "$HITS_FILE" || true)
    N450=$(grep -c " 450 " "$HITS_FILE" || true)
    N500=$(grep -c " 500 " "$HITS_FILE" || true)
    echo "Wavelength counts: 420nm=$N420 450nm=$N450 500nm=$N500"

    if [ "$N420" -eq 3 ] && [ "$N450" -eq 2 ] && [ "$N500" -eq 5 ]; then
        echo "PASSED: Wavelengths preserved correctly"
    else
        echo "FAILED: Expected 420nm=3, 450nm=2, 500nm=5"
        PASS=false
    fi
fi

# ---- Test 2: Comments and blank lines are skipped ----
echo ""
echo "=== Test 2: Comments and blank lines ==="

cat > "$PHOTON_FILE" <<'EOF'
# This is a comment

# Another comment
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  420.0

-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  450.0
# Trailing comment
EOF

echo "Running GPUPhotonFileSource ..."
OUTPUT=$(GPUPhotonFileSource \
    -g "$OPTICKS_HOME/tests/geom/opticks_raindrop.gdml" \
    -p "$PHOTON_FILE" \
    -m "$OPTICKS_HOME/tests/run.mac" \
    -s "$SEED" 2>&1)

LOADED=$(echo "$OUTPUT" | grep "Loaded" | awk '{print $2}')
OPTICKS_HITS=$(echo "$OUTPUT" | grep "Opticks: NumHits:" | awk '{print $NF}')

echo "Loaded:  $LOADED photons"
echo "Opticks: NumHits: $OPTICKS_HITS"

if [ "$LOADED" -eq 2 ]; then
    echo "PASSED: Correctly skipped comments and blank lines (loaded 2)"
else
    echo "FAILED: Expected 2 photons, got $LOADED"
    PASS=false
fi

# ---- Test 3: Missing --photons argument should fail ----
echo ""
echo "=== Test 3: Missing --photons argument ==="

if GPUPhotonFileSource \
    -g "$OPTICKS_HOME/tests/geom/opticks_raindrop.gdml" \
    -m "$OPTICKS_HOME/tests/run.mac" 2>&1; then
    echo "FAILED: Should have exited with error for missing --photons"
    PASS=false
else
    echo "PASSED: Correctly failed when --photons not provided"
fi

# ---- Summary ----
echo ""
if [ "$PASS" = true ]; then
    echo "All tests passed"
    exit 0
else
    echo "Some tests FAILED"
    exit 1
fi
