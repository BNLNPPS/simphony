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
# eps=0.001 mm (= 1 µm) was the sweet spot in an eps-scan over 1e-5..5e-2.
# Smaller values cause photons to get stuck at boundaries (float32 ulp issues);
# larger values cause leakage. eps=0.001 matched G4 best across hit count
# (within 0.14%) and all spatial+temporal chi^2 (all < 5.5).
export OPTICKS_PROPAGATE_EPSILON=${OPTICKS_PROPAGATE_EPSILON:-0.001}
export OPTICKS_PROPAGATE_EPSILON0=${OPTICKS_PROPAGATE_EPSILON0:-0.001}
# MAX_BOUNCE=10000 keeps the long late-tail in Opticks. G4's effective
# cap is much lower (~50-100 from G4Transportation looper-kill) — for a
# pure G4-count match in lossless media use MAX_BOUNCE=100, but the
# right physics setting is 10000 with realistic ABSLENGTH (< ~100 m).
export OPTICKS_MAX_BOUNCE=${OPTICKS_MAX_BOUNCE:-10000}

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
G4_MACRO_5EVT="${SCRIPT_DIR}/run_validate_5evt_1t.mac"

run_torch_test () {
    local case=$1; local gdml=$2; local cfg=$3; local seed=${4:-42}
    local outd="${OUT_DIR}/${case}"
    mkdir -p "${outd}"; cd "${outd}"; rm -f *_hits_output.txt
    "${EIC_OPTICKS_BIN}/GPUPhotonSource" -g "${gdml}" -c "${cfg}" -m "${G4_MACRO}" -s ${seed} > run.log 2>&1
}

# ------------------------------------------------------------------
# tests
# ------------------------------------------------------------------
# (1) Simple test class: isotropic torch source in the new solid, no
#     scintillation / Cherenkov physics. Validates the convex-polyhedron
#     conversion under heavy TIR / multi-bounce. Runs on BOTH trap and trd
#     so each new solid is exercised.
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

# (2) Full-physics test: 5 GeV electrons in synthetic-scintillator Quartz
#     solid with finite ABSLENGTH=100m. Folds Cerenkov + Scintillation +
#     absorption + slanted walls + multi-bounce. Run on BOTH trap and trd
#     so each solid sees the full physics chain. Single-thread G4 for
#     deterministic file output; ~3 min wall time per solid.
test_scintillation_trap () {
    echo
    echo "----- Test: Scintillation+Cherenkov on trap, 5 x 5 GeV electrons -----"
    local outd="${OUT_DIR}/scintillation_trap"
    mkdir -p "${outd}"; cd "${outd}"; rm -f *_hits_output.txt
    "${EIC_OPTICKS_BIN}/GPURaytrace" -g "${GEOM_DIR}/test_trap_scint.gdml" -m "${G4_MACRO_5EVT}" -s 42 > run.log 2>&1
    python3 "${COMPARE_PY}" "${outd}/g4_hits_output.txt" "${outd}/opticks_hits_output.txt" "scintillation trap" 10 50
}

test_scintillation_trd () {
    echo
    echo "----- Test: Scintillation+Cherenkov on trd, 5 x 5 GeV electrons -----"
    local outd="${OUT_DIR}/scintillation_trd"
    mkdir -p "${outd}"; cd "${outd}"; rm -f *_hits_output.txt
    "${EIC_OPTICKS_BIN}/GPURaytrace" -g "${GEOM_DIR}/test_trd_scint.gdml" -m "${G4_MACRO_5EVT}" -s 42 > run.log 2>&1
    python3 "${COMPARE_PY}" "${outd}/g4_hits_output.txt" "${outd}/opticks_hits_output.txt" "scintillation trd" 10 50
}

# ------------------------------------------------------------------
# dispatch
# ------------------------------------------------------------------
case "${1:-all}" in
    trap|iso_trap)              test_trap_iso ;;
    trd|iso_trd)                test_trd_iso ;;
    scintillation|sc)           test_scintillation_trap; test_scintillation_trd ;;
    scintillation_trap|sc_trap) test_scintillation_trap ;;
    scintillation_trd|sc_trd)   test_scintillation_trd ;;
    all|*)
        test_trap_iso
        test_trd_iso
        test_scintillation_trap
        test_scintillation_trd
        ;;
esac
