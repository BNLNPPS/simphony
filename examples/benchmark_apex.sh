#!/bin/bash
# benchmark_apex.sh — Measure GPU vs G4 speedup on apex.gdml
#
# Usage:
#   ./examples/benchmark_apex.sh

GDML="apex.gdml"
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

python3 -c "
gpu = float('$GPU_TIME')
g4_cpu = float('$G4_CPU')
g4_wall = float('$G4_WALL')
nphotons = int('$NPHOTONS')
gpu_hits = int('$GPU_HITS')
g4_hits = int('$G4_HITS')
hit_diff = (gpu_hits - g4_hits) / g4_hits * 100 if g4_hits > 0 else 0

print()
print(f'Photons:        {nphotons:>10,}')
print(f'GPU sim time:   {gpu:>10.4f} s')
print(f'G4 CPU time:    {g4_cpu:>10.2f} s')
print(f'G4 wall time:   {g4_wall:>10.2f} s')
print()
print(f'Speedup (CPU):  {g4_cpu/gpu:>10.0f}x')
print(f'Speedup (wall): {g4_wall/gpu:>10.0f}x')
print()
print(f'GPU rate:       {nphotons/gpu/1e6:>10.1f} M photons/s')
print(f'G4 rate:        {nphotons/g4_cpu/1e3:>10.1f} k photons/s')
print()
print(f'GPU hits:       {gpu_hits:>10}')
print(f'G4 hits:        {g4_hits:>10}')
print(f'Hit diff:       {hit_diff:>+9.1f}%')
"

rm -f "$LOGFILE"

# Generate comparison plots if hit files exist
if [ -f "gpu_hits.npy" ] && [ -f "g4_hits.npy" ]; then
    echo ""
    echo "=== Generating comparison plots ==="
    python3 optiphy/ana/run_and_compare.py --gpu-hits gpu_hits.npy --g4-hits g4_hits.npy --outdir "$OUTDIR" 2>&1 | tail -15
    echo "Plots saved to $OUTDIR/"
fi
