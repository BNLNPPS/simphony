#!/usr/bin/env bash
#
# G4Trap / G4Trd geometry validation test suite.
#
# Compares Geant4 (CPU) and Opticks (GPU) photon hits on identical inputs,
# for each of the new convex-polyhedron-based solids. The branch under test
# is g4trap-to-convexpolyhedron; this script runs every check that was used
# to validate it.
#
# Usage:
#   ./tests/g4trap_validation.sh                          # all tests
#   ./tests/g4trap_validation.sh trap                     # trap-only test
#   ./tests/g4trap_validation.sh trd                      # trd-only test
#   ./tests/g4trap_validation.sh single_photon            # 1-photon golden
#   ./tests/g4trap_validation.sh cherenkov                # Cherenkov from e-
#   ./tests/g4trap_validation.sh scintillation            # Scint from e-
#
# Pre-requisites: build the branch in /opt/eic-opticks/build (or set
# EIC_OPTICKS_BIN/EIC_OPTICKS_CFG).

set -e

EIC_OPTICKS_BIN=${EIC_OPTICKS_BIN:-/opt/eic-opticks/bin}
EIC_OPTICKS_CFG=${EIC_OPTICKS_CFG:-/opt/eic-opticks/share/eic-opticks/config}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GEOM_DIR="${SCRIPT_DIR}/geom"
OUT_DIR=${OUT_DIR:-/tmp/g4trap_validation}

export CUDA_VISIBLE_DEVICES=${CUDA_VISIBLE_DEVICES:-1}
export OPTICKS_PROPAGATE_EPSILON=0.0001
export OPTICKS_PROPAGATE_EPSILON0=0.0001
export OPTICKS_MAX_BOUNCE=${OPTICKS_MAX_BOUNCE:-100}

mkdir -p "${OUT_DIR}"

# ------------------------------------------------------------------
# torch source configs (auto-installed to share/ on first run)
# ------------------------------------------------------------------
ensure_torch_config () {
    local name=$1
    local content=$2
    if [ ! -f "${EIC_OPTICKS_CFG}/${name}.json" ]; then
        echo "${content}" > "${EIC_OPTICKS_CFG}/${name}.json"
    fi
}

ensure_torch_config trap_disc '{
  "torch": {
    "gentype": "TORCH", "trackid": 0, "matline": 0,
    "numphoton": 10000,
    "pos": [0.0, 0.0, -150.0], "time": 0.0,
    "mom": [0.0, 0.0, 1.0], "weight": 0.0,
    "pol": [1.0, 0.0, 0.0], "wavelength": 420.0,
    "zenith":  [0.0, 1.0], "azimuth": [0.0, 1.0],
    "radius": 30.0, "distance": 0.0, "mode": 255, "type": "disc"
  },
  "event": {"mode": "DebugLite", "maxslot": 1000000}
}'

ensure_torch_config trap_iso '{
  "torch": {
    "gentype": "TORCH", "trackid": 0, "matline": 0,
    "numphoton": 50000,
    "pos": [20.0, 20.0, -50.0], "time": 0.0,
    "mom": [0.0, 0.0, 1.0], "weight": 0.0,
    "pol": [1.0, 0.0, 0.0], "wavelength": 420.0,
    "zenith":  [0.0, 1.0], "azimuth": [0.0, 1.0],
    "radius": 0.1, "distance": 0.0, "mode": 255, "type": "sphere_marsaglia"
  },
  "event": {"mode": "DebugLite", "maxslot": 1000000}
}'

ensure_torch_config single_photon_xside '{
  "torch": {
    "gentype": "TORCH", "trackid": 0, "matline": 0,
    "numphoton": 1,
    "pos": [0.0, 0.0, 0.0], "time": 0.0,
    "mom": [0.894427, 0.447214, 0.0], "weight": 0.0,
    "pol": [0.0, 0.0, 1.0], "wavelength": 420.0,
    "zenith":  [0.0, 0.0], "azimuth": [0.0, 0.0],
    "radius": 0.0, "distance": 0.0, "mode": 255, "type": "point"
  },
  "event": {"mode": "DebugLite", "maxslot": 1000000}
}'

# ------------------------------------------------------------------
# compare.py — extract hit count + per-axis chi^2 vs G4
# ------------------------------------------------------------------
COMPARE_PY="${OUT_DIR}/compare.py"
cat > "${COMPARE_PY}" <<'PYEOF'
import re, sys, numpy as np
PAT = re.compile(r'^\s*([\-\d.eE+]+)\s+([\-\d.eE+]+)\s+\(([^)]+)\)\s+\(([^)]+)\)')
def load(p):
    rows = []
    for line in open(p):
        m = PAT.match(line)
        if not m: continue
        rows.append((float(m.group(1)), float(m.group(2)),
                     *[float(x) for x in m.group(3).split(',')],
                     *[float(x) for x in m.group(4).split(',')]))
    return np.array(rows, float) if rows else np.empty((0,8))
def chi2_1d(a,b,bins):
    ha,_ = np.histogram(a, bins=bins); hb,_ = np.histogram(b, bins=bins)
    m = (ha+hb) > 0
    return float(np.sum((ha[m]-hb[m])**2 / (ha[m]+hb[m]))), int(m.sum())
g_path, o_path, label = sys.argv[1], sys.argv[2], sys.argv[3]
tolerance_count = float(sys.argv[4]) if len(sys.argv) > 4 else 5.0
tolerance_chi2  = float(sys.argv[5]) if len(sys.argv) > 5 else 5.0
g = load(g_path); o = load(o_path)
n_g, n_o = len(g), len(o)
rel = abs(n_o-n_g)/((n_o+n_g)/2)*100 if n_o+n_g>0 else 0
print(f"=== {label} ===")
print(f"  G4 hits:      {n_g}")
print(f"  Opticks hits: {n_o}")
print(f"  rel diff:     {rel:.3f}%   (tol={tolerance_count}%)")
fail = []
if rel > tolerance_count: fail.append(f"count rel-diff {rel:.2f}% > {tolerance_count}%")
if n_g == 0 or n_o == 0:
    print("  no hits, skip distributions")
else:
    for col, name, bins in [
        (2,'x',  np.linspace(min(g[:,2].min(),o[:,2].min()), max(g[:,2].max(),o[:,2].max()), 31)),
        (3,'y',  np.linspace(min(g[:,3].min(),o[:,3].min()), max(g[:,3].max(),o[:,3].max()), 31)),
        (5,'dx', np.linspace(-1, 1, 21)),
        (6,'dy', np.linspace(-1, 1, 21)),
        (7,'dz', np.linspace(-1, 1, 21)),
    ]:
        chi2, ndf = chi2_1d(g[:,col], o[:,col], bins)
        ratio = chi2/max(ndf,1)
        marker = "FAIL" if ratio > tolerance_chi2 else "ok"
        print(f"  {name:>2}: chi2/ndf = {chi2:7.2f}/{ndf} = {ratio:5.2f}  {marker}")
        if ratio > tolerance_chi2: fail.append(f"{name} chi2/ndf {ratio:.2f}")
if fail:
    print(f"  FAILED: {', '.join(fail)}")
    sys.exit(1)
print("  PASS")
PYEOF

# ------------------------------------------------------------------
# run helpers
# ------------------------------------------------------------------
G4_MACRO="${SCRIPT_DIR}/run_validate.mac"
G4_MACRO_10EVT="${SCRIPT_DIR}/run_validate_10evt_1t.mac"
G4_MACRO_5EVT="${SCRIPT_DIR}/run_validate_5evt_1t.mac"

run_torch_test () {
    local case=$1; local gdml=$2; local cfg=$3; local seed=${4:-42}
    local outd="${OUT_DIR}/${case}"
    mkdir -p "${outd}"; cd "${outd}"; rm -f *_hits_output.txt
    "${EIC_OPTICKS_BIN}/GPUPhotonSource" -g "${gdml}" -c "${cfg}" -m "${G4_MACRO}" -s ${seed} > run.log 2>&1
}

run_cherenkov_or_scint () {
    local case=$1; local gdml=$2; local macro=$3; local seed=${4:-42}
    local outd="${OUT_DIR}/${case}"
    mkdir -p "${outd}"; cd "${outd}"; rm -f *_hits_output.txt
    "${EIC_OPTICKS_BIN}/GPURaytrace" -g "${gdml}" -m "${macro}" -s ${seed} > run.log 2>&1
}

# ------------------------------------------------------------------
# tests
# ------------------------------------------------------------------
test_trap_disc () {
    echo
    echo "----- Test: trap, disc source straight +Z -----"
    run_torch_test trap_disc "${GEOM_DIR}/test_trap.gdml" trap_disc
    python3 "${COMPARE_PY}" "${OUT_DIR}/trap_disc/g4_hits_output.txt" "${OUT_DIR}/trap_disc/opticks_hits_output.txt" "trap disc"
}

test_trd_disc () {
    echo
    echo "----- Test: trd, disc source straight +Z -----"
    run_torch_test trd_disc "${GEOM_DIR}/test_trd.gdml" trap_disc
    python3 "${COMPARE_PY}" "${OUT_DIR}/trd_disc/g4_hits_output.txt" "${OUT_DIR}/trd_disc/opticks_hits_output.txt" "trd disc"
}

test_trap_iso () {
    echo
    echo "----- Test: trap, isotropic source (probes slanted walls) -----"
    run_torch_test trap_iso "${GEOM_DIR}/test_trap.gdml" trap_iso
    python3 "${COMPARE_PY}" "${OUT_DIR}/trap_iso/g4_hits_output.txt" "${OUT_DIR}/trap_iso/opticks_hits_output.txt" "trap iso"
}

test_trd_iso () {
    echo
    echo "----- Test: trd, isotropic source -----"
    run_torch_test trd_iso "${GEOM_DIR}/test_trd.gdml" trap_iso
    python3 "${COMPARE_PY}" "${OUT_DIR}/trd_iso/g4_hits_output.txt" "${OUT_DIR}/trd_iso/opticks_hits_output.txt" "trd iso"
}

test_single_photon () {
    echo
    echo "----- Test: single photon normal-incidence at trap +X wall -----"
    echo "  expected hit: (249.5, 124.634, 0) -- both binaries must agree"
    run_torch_test single_photon "${GEOM_DIR}/test_trap_side.gdml" single_photon_xside
    head -2 "${OUT_DIR}/single_photon/g4_hits_output.txt" "${OUT_DIR}/single_photon/opticks_hits_output.txt"
}

test_cherenkov () {
    echo
    echo "----- Test: Cherenkov from 10 x 5 GeV electrons -----"
    run_cherenkov_or_scint cherenkov "${GEOM_DIR}/test_trap_dispersive.gdml" "${G4_MACRO_10EVT}"
    python3 "${COMPARE_PY}" "${OUT_DIR}/cherenkov/g4_hits_output.txt" "${OUT_DIR}/cherenkov/opticks_hits_output.txt" "cherenkov" 5 8
}

test_scintillation () {
    echo
    echo "----- Test: Scintillation from 5 x 5 GeV electrons (single-thread G4) -----"
    run_cherenkov_or_scint scintillation "${GEOM_DIR}/test_trap_scint.gdml" "${G4_MACRO_5EVT}"
    python3 "${COMPARE_PY}" "${OUT_DIR}/scintillation/g4_hits_output.txt" "${OUT_DIR}/scintillation/opticks_hits_output.txt" "scint" 10 50
}

# ------------------------------------------------------------------
# dispatch
# ------------------------------------------------------------------
case "${1:-all}" in
    trap)         test_trap_disc; test_trap_iso ;;
    trd)          test_trd_disc; test_trd_iso ;;
    single_photon|sp) test_single_photon ;;
    cherenkov|ck)     test_cherenkov ;;
    scintillation|sc) test_scintillation ;;
    all|*)
        test_trap_disc
        test_trd_disc
        test_trap_iso
        test_trd_iso
        test_single_photon
        test_cherenkov
        test_scintillation
        ;;
esac
