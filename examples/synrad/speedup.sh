#!/usr/bin/env bash
# speedup.sh — GPU vs Geant4 speedup demonstration on the synrad example
#
# Times the photon transport four ways and prints the ratios:
#
#   G4 analytic : synrad_g4 on the benchmark's native analytic CSG solids,
#                 the pure pencil beam — the leanest Geant4
#                 (production geometry, in-place reflection, one core)
#   G4 tess     : synrad_g4 -g tess (CAD-like meshed drifts, ~24k facets,
#                 analytic arc) driven by its native workload, the electron
#                 mode: stock G4SynchrotronRadiation illuminates the drift
#                 walls below the bend angle. Transport-only via the Delta
#                 method: BeamOn(full) minus BeamOn(--killphotons), per
#                 generated photon.
#   GPU coarse / GPU fine : the same pencil photons through the two fused
#                 envelope GDMLs (1252 / ~25k facets), single CPU thread.
#
# For every run it prints the end-to-end wall time (whole process) and the
# transport-only time (BeamOn / gx->simulate bracket, minus the killphotons
# baseline for the Delta run). The GPU-vs-G4-analytic ratio is the
# production-comparison number; the GPU-vs-G4-tess ratio is the
# CAD-geometry case (G4 navigation pays heavily for the fine mesh, the GPU
# triangle intersection does not; the two runs carry different but
# comparably grazing illuminations).
#
# Usage:
#   SIMPHONY_PREFIX=/path/to/simphony/install ./speedup.sh [nphoton] [seed]
#
# Note: the G4 tessellated Delta run tracks the electrons through the meshed
# geometry twice (minutes at the default 500k-photon scale).

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

NPHOTON=${1:-500000}
SEED=${2:-42}
NEL=$(( NPHOTON*10/32 ))                       # ~3.2 photons >= 30 eV per e-
BEAM="0,0,100,0,0.007,1,0.3,19.4"              # pure 7 mrad pencil (see README)

build="${SIMPHONY_SYNRAD_BUILD_DIR:-/tmp/simphony-synrad-build}"
run_dir="${SIMPHONY_SYNRAD_RUN_DIR:-/tmp/simphony-synrad-speedup}"
cmake_prefix_path="${SIMPHONY_PREFIX:-${CMAKE_PREFIX_PATH:-}}"

if [[ -z "$cmake_prefix_path" ]]; then
    echo "SIMPHONY_PREFIX or CMAKE_PREFIX_PATH must point to a Simphony install prefix" >&2
    exit 1
fi

cmake -S "$script_dir" -B "$build" -DCMAKE_PREFIX_PATH="$cmake_prefix_path" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$build" > /dev/null

simphony_lib_dir="${SIMPHONY_LIB_DIR:-${SIMPHONY_PREFIX:-$cmake_prefix_path}/lib}"
export LD_LIBRARY_PATH="$simphony_lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

mkdir -p "$run_dir"

# run <key> <binary> <args...> : end-to-end wall time around the process,
# transport time and photon count parsed from the app's own summary lines
declare -A E2E TRANS NGAM
run() {
    local key="$1" bin="$2" ; shift 2
    local t0 t1 out
    t0=$(date +%s.%N)
    out=$("$build/$bin" "$@" -o "$run_dir" 2>&1 | grep -E "^synrad")
    t1=$(date +%s.%N)
    E2E[$key]=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')
    TRANS[$key]=$(echo "$out" | grep -oE "transport [0-9.e+-]+ s" | head -1 | awk '{print $2}')
    NGAM[$key]=$(echo "$out" | grep -oE "photons [0-9]+" | head -1 | awk '{print $2}')
    echo "$out" | head -1 | sed 's/^/  /'
}

echo "=============================================="
echo " synrad speedup: Geant4 1T vs GPU"
echo "=============================================="
echo "  Photons: $NPHOTON   Seed: $SEED   (tess Delta run: $NEL e-)"
echo ""

echo "--- Geant4, analytic CSG solids (1 core, pencil beam)"
run g4_analytic synrad_g4 -g analytic -n "$NPHOTON" -s "$SEED" -I "$BEAM" -f 0

echo "--- Geant4, tessellated drifts (1 core, SR electrons, Delta method)"
echo "[full]"
run g4_tess_full synrad_g4 -g tess -e "$NEL" -s "$SEED"
echo "[killphotons baseline]"
run g4_tess_kill synrad_g4 -g tess -e "$NEL" -s "$SEED" --killphotons

echo "--- GPU, coarse fused envelope (1252 facets)"
run gpu_coarse synrad -g "$script_dir/synrad_bench.gdml" -n "$NPHOTON" -s "$SEED" -I "$BEAM" -f 0

echo "--- GPU, fine fused envelope (~25k facets)"
run gpu_fine synrad -g "$script_dir/synrad_bench_tess.gdml" -n "$NPHOTON" -s "$SEED" -I "$BEAM" -f 0

# tess Delta: pure photon transport per generated photon
DT_TESS=$(awk -v f="${TRANS[g4_tess_full]}" -v k="${TRANS[g4_tess_kill]}" 'BEGIN{printf "%.3f", f-k}')

echo ""
printf "%-34s %14s %15s %13s\n" "" "end-to-end [s]" "transport [s]" "[us/photon]"
printf "%-34s %14s %15s %13s\n" "G4 1T analytic (pencil)" "${E2E[g4_analytic]}" "${TRANS[g4_analytic]}" \
    "$(awk -v t="${TRANS[g4_analytic]}" -v n="${NGAM[g4_analytic]}" 'BEGIN{printf "%.3f", 1e6*t/n}')"
printf "%-34s %14s %15s %13s\n" "G4 1T tess (SR e-, Delta)" "${E2E[g4_tess_full]}" "$DT_TESS" \
    "$(awk -v t="$DT_TESS" -v n="${NGAM[g4_tess_full]}" 'BEGIN{printf "%.3f", 1e6*t/n}')"
printf "%-34s %14s %15s %13s\n" "GPU coarse (pencil)" "${E2E[gpu_coarse]}" "${TRANS[gpu_coarse]}" \
    "$(awk -v t="${TRANS[gpu_coarse]}" -v n="${NGAM[gpu_coarse]}" 'BEGIN{printf "%.4f", 1e6*t/n}')"
printf "%-34s %14s %15s %13s\n" "GPU fine (pencil)" "${E2E[gpu_fine]}" "${TRANS[gpu_fine]}" \
    "$(awk -v t="${TRANS[gpu_fine]}" -v n="${NGAM[gpu_fine]}" 'BEGIN{printf "%.4f", 1e6*t/n}')"

echo ""
awk -v ga="${TRANS[g4_analytic]}"  -v na="${NGAM[g4_analytic]}" \
    -v gt="$DT_TESS"               -v nt="${NGAM[g4_tess_full]}" \
    -v pc="${TRANS[gpu_coarse]}"   -v nc="${NGAM[gpu_coarse]}" \
    -v pf="${TRANS[gpu_fine]}"     -v nf="${NGAM[gpu_fine]}" 'BEGIN{
    ua=1e6*ga/na; ut=1e6*gt/nt; uc=1e6*pc/nc; uf=1e6*pf/nf;
    printf "production cmp  : GPU vs G4-1T on analytic CSG   %.0fx  (%.2f / %.4f us/photon)\n", ua/uc, ua, uc ;
    printf "CAD-layout      : GPU vs G4-1T on meshed drifts  %.0fx  (%.2f / %.4f us/photon)\n", ut/uf, ut, uf ;
    printf "mesh-fidelity   : G4 pays %.1fx for the mesh, the GPU %.2fx\n", ut/ua, uf/uc ;
}'
echo "(per single CPU core; end-to-end includes per-process init: GDML+CUDA+OptiX"
echo " setup on the GPU side, geometry+physics init and the killphotons-baseline"
echo " electron tracking on the G4 Delta run)"
