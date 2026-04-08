#!/bin/bash
# photontimingandsteps.sh — Measure GPU vs G4 optical photon speedup and timing
#
# Methodology (3 runs):
#
#   Run 1 (--skip-gpu):          G4 tracks electron + propagates optical photons
#                                GPU is skipped. Genstep collection is skipped.
#                                Measures: G4 photon-only CPU time, per-process
#                                step timing, per-photon timing with percentiles.
#
#   Run 2 (setStackPhotons off): G4 tracks electron only, no photon tracks created.
#                                GPU is skipped.
#                                Measures: EM baseline time (electron tracking only).
#
#   Run 3 (setStackPhotons off,  G4 tracks electron and collects gensteps but does
#          GPU active):          NOT create photon tracks. GPU propagates photons.
#                                Measures: GPU-accelerated total wall time.
#
# Speedup calculations:
#   G4 photon-only CPU time  = Run 1 CPU  - Run 2 CPU
#   Photon-only speedup      = G4 photon-only / GPU sim time
#   Wall time speedup         = Run 1 wall / Run 3 wall
#
# Usage:
#   ./examples/photontiming_geant4/photontimingandsteps.sh [events]  # default: 1

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

echo "=== Optical Photon Timing & Speedup Benchmark ==="
echo "GDML=$GDML  eps=$EPS  eps0=$EPS0  events=$NEVENTS  config=$CONFIG"

# Macro: normal G4 (photons propagated)
MAC_NORMAL=$(mktemp /tmp/bench_normal_XXXXXX.mac)
cat > "$MAC_NORMAL" << EOF
/run/verbose 1
/run/numberOfThreads 1
/run/initialize
/run/beamOn $NEVENTS
EOF

# Macro: no photon tracks (gensteps still collected by SteppingAction)
MAC_NOPHOTON=$(mktemp /tmp/bench_nophoton_XXXXXX.mac)
cat > "$MAC_NOPHOTON" << EOF
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

# ---------------------------------------------------------------
# Run 1: G4 photon propagation (no GPU, no genstep collection)
# ---------------------------------------------------------------
echo ""
echo "--- Run 1: G4 photon propagation (--skip-gpu, single-threaded) ---"
LOG1=$(mktemp /tmp/bench1_XXXXXX.txt)
GPURaytrace -g "$GDML" -m "$MAC_NORMAL" -c "$CONFIG" -s 42 --skip-gpu &> "$LOG1"

G4_LINE=$(grep "^  User=" "$LOG1" | tail -1)
G4_PHOTON_CPU=$(echo "$G4_LINE" | grep -oP 'User=\K[0-9.]+')
G4_PHOTON_WALL=$(echo "$G4_LINE" | grep -oP 'Real=\K[0-9.]+')
G4_HITS=$(grep "Geant4: NumHits:" "$LOG1" | awk '{print $NF}')

echo "  G4 CPU time:           ${G4_PHOTON_CPU}s"
echo "  G4 wall time:          ${G4_PHOTON_WALL}s"
echo "  G4 hits:               $G4_HITS"

# Print per-process and per-photon timing
grep "Geant4: StepTime\|Geant4: PhotonTiming\|Geant4: PhotonPercentiles\|Geant4: PhotonSteps\|Geant4: StepPercentiles\|Geant4: PhotonTimeHist\|Geant4: OpticalSteps" "$LOG1" | sed 's/^/  /'

# ---------------------------------------------------------------
# Run 2: EM baseline (no photon tracks, no GPU)
# ---------------------------------------------------------------
echo ""
echo "--- Run 2: EM baseline (setStackPhotons false, --skip-gpu) ---"
LOG2=$(mktemp /tmp/bench2_XXXXXX.txt)
GPURaytrace -g "$GDML" -m "$MAC_NOPHOTON" -c "$CONFIG" -s 42 --skip-gpu &> "$LOG2"

G4_BASE_LINE=$(grep "^  User=" "$LOG2" | tail -1)
G4_BASE_CPU=$(echo "$G4_BASE_LINE" | grep -oP 'User=\K[0-9.]+')
echo "  EM baseline CPU:       ${G4_BASE_CPU}s"

# ---------------------------------------------------------------
# Run 3: GPU-accelerated (no G4 photon tracks, GPU propagates)
# ---------------------------------------------------------------
echo ""
echo "--- Run 3: GPU-accelerated (setStackPhotons false, GPU active) ---"
LOG3=$(mktemp /tmp/bench3_XXXXXX.txt)
GPURaytrace -g "$GDML" -m "$MAC_NOPHOTON" -c "$CONFIG" -s 42 &> "$LOG3"

GPU_TIME=$(grep "Simulation time:" "$LOG3" | awk '{print $3}')
GPU_HITS=$(grep "Opticks: NumHits:" "$LOG3" | awk '{print $NF}')
GPU_LINE=$(grep "^  User=" "$LOG3" | tail -1)
GPU_WALL=$(echo "$GPU_LINE" | grep -oP 'Real=\K[0-9.]+')
NPHOTONS=$(grep "NumCollected:" "$LOG3" | tail -1 | awk '{print $NF}')

echo "  GPU sim time:          ${GPU_TIME}s"
echo "  GPU total wall:        ${GPU_WALL}s"
echo "  GPU hits:              $GPU_HITS"
echo "  Photons:               $NPHOTONS"

# ---------------------------------------------------------------
# Results
# ---------------------------------------------------------------
echo ""
echo "=== Results ==="
python3 -c "
gpu_sim = float('${GPU_TIME:-0}')
g4_cpu = float('${G4_PHOTON_CPU:-0}')
g4_base = float('${G4_BASE_CPU:-0}')
g4_wall = float('${G4_PHOTON_WALL:-0}')
gpu_wall = float('${GPU_WALL:-0}')
nphotons = int('${NPHOTONS:-0}')
gpu_hits = int('${GPU_HITS:-0}')
g4_hits = int('${G4_HITS:-0}')

g4_photon_cpu = g4_cpu - g4_base
hit_diff = (gpu_hits - g4_hits) / g4_hits * 100 if g4_hits > 0 else 0

print()
print(f'Photons:                    {nphotons:>12,}')
print()
print(f'--- Photon-only speedup ---')
print(f'G4 photon CPU time:         {g4_photon_cpu:>12.2f} s')
print(f'GPU sim time:               {gpu_sim:>12.4f} s')
if gpu_sim > 0 and g4_photon_cpu > 0:
    print(f'Photon speedup:             {g4_photon_cpu/gpu_sim:>12.0f}x')
    print(f'GPU time/photon:            {gpu_sim/nphotons*1e9:>12.1f} ns')
    print(f'G4 time/photon (avg):       {g4_photon_cpu/nphotons*1e6:>12.1f} us')
print()
print(f'--- Wall time speedup ---')
print(f'G4 wall (EM + photons):     {g4_wall:>12.2f} s')
print(f'GPU wall (EM + GPU):        {gpu_wall:>12.4f} s')
if gpu_wall > 0 and g4_wall > 0:
    print(f'Wall speedup:               {g4_wall/gpu_wall:>12.0f}x')
print()
print(f'GPU hits:                   {gpu_hits:>12}')
print(f'G4 hits:                    {g4_hits:>12}')
print(f'Hit diff:                   {hit_diff:>+11.1f}%')
"

rm -f "$LOG1" "$LOG2" "$LOG3" "$MAC_NORMAL" "$MAC_NOPHOTON"
