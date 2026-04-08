#!/bin/bash
# benchmark_apex.sh — Measure GPU vs G4 optical photon propagation speedup
#
# Methodology:
#   Run 1 (--skip-gpu):         G4 electrons + G4 photon propagation, no GPU
#   Run 2 (setStackPhotons off): G4 electrons only, no photon tracks at all
#   Run 3 (normal):             G4 electrons + GPU photon propagation
#
#   G4 photon-only CPU time = Run 1 - Run 2
#   GPU photon time          = "Simulation time" from Run 3
#   Speedup                  = G4 photon-only / GPU time
#
# The --skip-gpu flag prevents the GPU simulate() call so that G4's
# "User=" time reflects only CPU-side photon propagation without
# GPU contamination.
#
# Usage:
#   ./examples/benchmark_apex.sh [events]    # default: 1

set -e

GDML="apex.gdml"
EPS="${OPTICKS_PROPAGATE_EPSILON:-0.00001}"
EPS0="${OPTICKS_PROPAGATE_EPSILON0:-0.0006}"
CONFIG="${BENCHMARK_CONFIG:-det_debug}"
NEVENTS="${1:-1}"

if [ ! -f "$GDML" ]; then
    echo "ERROR: $GDML not found. Run from the eic-opticks root directory."
    exit 1
fi

echo "=== apex.gdml Benchmark ==="
echo "eps=$EPS  eps0=$EPS0  events=$NEVENTS  config=$CONFIG"

MAC_NORMAL=$(mktemp /tmp/bench_normal_XXXXXX.mac)
cat > "$MAC_NORMAL" << EOF
/run/verbose 1
/run/numberOfThreads 1
/run/initialize
/run/beamOn $NEVENTS
EOF

MAC_BASELINE=$(mktemp /tmp/bench_baseline_XXXXXX.mac)
cat > "$MAC_BASELINE" << EOF
/run/verbose 1
/run/numberOfThreads 1
/run/initialize
/process/optical/scintillation/setStackPhotons false
/process/optical/cerenkov/setStackPhotons false
/run/beamOn $NEVENTS
EOF

export OPTICKS_MAX_BOUNCE=1000
export OPTICKS_PROPAGATE_EPSILON=$EPS
export OPTICKS_PROPAGATE_EPSILON0=$EPS0

# --- Run 1: G4 photon propagation (skip GPU) ---
echo ""
echo "--- Run 1: G4 photon propagation (--skip-gpu, single-threaded) ---"
LOG1=$(mktemp /tmp/bench1_XXXXXX.txt)
GPURaytrace -g "$GDML" -m "$MAC_NORMAL" -c "$CONFIG" -s 42 --skip-gpu &> "$LOG1"

G4_LINE=$(grep "^  User=" "$LOG1" | tail -1)
G4_PHOTON_CPU=$(echo "$G4_LINE" | grep -oP 'User=\K[0-9.]+')
G4_HITS=$(grep "Geant4: NumHits:" "$LOG1" | awk '{print $NF}')
NPHOTONS=$(grep "NumCollected:" "$LOG1" | tail -1 | awk '{print $NF}')

echo "  G4 CPU (with photons): ${G4_PHOTON_CPU}s"
echo "  G4 hits:               $G4_HITS"
echo "  Photons:               $NPHOTONS"

# Print per-process and per-photon timing from Run 1
grep "Geant4: StepTime\|Geant4: PhotonTiming\|Geant4: PhotonPercentiles\|Geant4: PhotonSteps\|Geant4: StepPercentiles\|Geant4: PhotonTimeHist" "$LOG1" | sed 's/^/  /'

# --- Run 2: Baseline (no photon tracks) ---
echo ""
echo "--- Run 2: Baseline (setStackPhotons false) ---"
LOG2=$(mktemp /tmp/bench2_XXXXXX.txt)
GPURaytrace -g "$GDML" -m "$MAC_BASELINE" -c "$CONFIG" -s 42 --skip-gpu &> "$LOG2"

G4_BASE_LINE=$(grep "^  User=" "$LOG2" | tail -1)
G4_BASE_CPU=$(echo "$G4_BASE_LINE" | grep -oP 'User=\K[0-9.]+')
echo "  G4 baseline CPU:       ${G4_BASE_CPU}s (electrons only)"

# --- Run 3: GPU propagation ---
echo ""
echo "--- Run 3: GPU photon propagation ---"
LOG3=$(mktemp /tmp/bench3_XXXXXX.txt)
GPURaytrace -g "$GDML" -m "$MAC_NORMAL" -c "$CONFIG" -s 42 &> "$LOG3"

GPU_TIME=$(grep "Simulation time:" "$LOG3" | awk '{print $3}')
GPU_HITS=$(grep "Opticks: NumHits:" "$LOG3" | awk '{print $NF}')
echo "  GPU sim time:          ${GPU_TIME}s"
echo "  GPU hits:              $GPU_HITS"

# --- Results ---
echo ""
echo "=== Results ==="
python3 -c "
gpu = float('${GPU_TIME:-0}')
g4_full = float('${G4_PHOTON_CPU:-0}')
g4_base = float('${G4_BASE_CPU:-0}')
nphotons = int('${NPHOTONS:-0}')
gpu_hits = int('${GPU_HITS:-0}')
g4_hits = int('${G4_HITS:-0}')

g4_photon = g4_full - g4_base
hit_diff = (gpu_hits - g4_hits) / g4_hits * 100 if g4_hits > 0 else 0

print()
print(f'Photons:                    {nphotons:>12,}')
print()
print(f'G4 with photons (CPU):      {g4_full:>12.2f} s')
print(f'G4 baseline (CPU):          {g4_base:>12.2f} s')
print(f'G4 photon-only (CPU):       {g4_photon:>12.2f} s')
print(f'GPU sim time:               {gpu:>12.4f} s')
print()
if gpu > 0 and g4_photon > 0:
    speedup = g4_photon / gpu
    print(f'Speedup:                    {speedup:>12.0f}x')
    print()
    print(f'GPU rate:                   {nphotons/gpu/1e6:>12.1f} M photons/s')
    print(f'G4 rate:                    {nphotons/g4_photon/1e3:>12.1f} k photons/s')
    print(f'GPU time/photon:            {gpu/nphotons*1e9:>12.1f} ns')
    print(f'G4 time/photon (avg):       {g4_photon/nphotons*1e6:>12.1f} us')
print()
print(f'GPU hits:                   {gpu_hits:>12}')
print(f'G4 hits:                    {g4_hits:>12}')
print(f'Hit diff:                   {hit_diff:>+11.1f}%')
"

rm -f "$LOG1" "$LOG2" "$LOG3" "$MAC_NORMAL" "$MAC_BASELINE"
