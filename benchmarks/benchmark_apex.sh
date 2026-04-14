#!/bin/bash
# benchmark_apex.sh — Measure GPU vs G4 speedup on apex.gdml
#
# Usage:
#   ./benchmarks/benchmark_apex.sh

GDML="tests/geom/apex.gdml"
MACRO="tests/run.mac"
EPS="0.00001"
EPS0="0.0006"
OUTDIR="plots"
CONFIG="det_debug"

if [ ! -f "$GDML" ]; then
    echo "ERROR: $GDML not found. Run from the eic-opticks root directory."
    exit 1
fi

echo "=== apex.gdml Benchmark ==="
echo "eps=$EPS, eps0=$EPS0"
echo "Running..."

LOGFILE=$(mktemp /tmp/bench_XXXXXX.txt)
OPTICKS_MAX_BOUNCE=1000 \
OPTICKS_PROPAGATE_EPSILON=$EPS \
OPTICKS_PROPAGATE_EPSILON0=$EPS0 \
GPURaytrace -g "$GDML" -m "$MACRO" -c "$CONFIG" &> "$LOGFILE" || true

GPU_TIME=$(grep "Simulation time:" "$LOGFILE" | awk '{print $3}')
G4_LINE=$(grep "^  User=" "$LOGFILE" | tail -1)
G4_CPU=$(echo "$G4_LINE" | grep -oP 'User=\K[0-9.]+')
G4_WALL=$(echo "$G4_LINE" | grep -oP 'Real=\K[0-9.]+')
NPHOTONS=$(grep "NumCollected:" "$LOGFILE" | tail -1 | awk '{print $NF}')
GPU_HITS=$(grep "Opticks: NumHits:" "$LOGFILE" | awk '{print $NF}')
G4_HITS=$(grep "Geant4: NumHits:" "$LOGFILE" | awk '{print $NF}')

if [ -z "$GPU_TIME" ] || [ -z "$G4_CPU" ]; then
    echo "ERROR: Could not parse timing from output"
    tail -30 "$LOGFILE"
    rm -f "$LOGFILE"
    exit 1
fi

python3 optiphy/ana/print_speedup.py \
    "$GPU_TIME" "$G4_CPU" "$G4_WALL" "$NPHOTONS" "$GPU_HITS" "$G4_HITS"

rm -f "$LOGFILE"

# Generate comparison plots if hit files exist
if [ -f "gpu_hits.npy" ] && [ -f "g4_hits.npy" ]; then
    echo ""
    echo "=== Generating comparison plots ==="
    python3 optiphy/ana/run_and_compare.py --gpu-hits gpu_hits.npy --g4-hits g4_hits.npy --outdir "$OUTDIR" 2>&1 | tail -15
    echo "Plots saved to $OUTDIR/"
fi
