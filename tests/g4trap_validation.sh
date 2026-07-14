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
#   ./tests/g4trap_validation.sh trap                     # trap iso-source only
#   ./tests/g4trap_validation.sh trd                      # trd iso-source only
#   ./tests/g4trap_validation.sh scintillation            # scint+Cherenkov (trap & trd)
#   ./tests/g4trap_validation.sh scintillation_trap       # scint+Cherenkov trap only
#   ./tests/g4trap_validation.sh scintillation_trd        # scint+Cherenkov trd only
#
# Pre-requisites: simg4ox and GPURaytrace on PATH (any standard
# install of simphony puts them in the chosen install prefix's `bin/`,
# which is added to PATH in the Dockerfile and devcontainer).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR=${REPO_DIR:-$(cd "${SCRIPT_DIR}/.." && pwd)}
GEOM_DIR="${SCRIPT_DIR}/geom"
OUT_DIR=${OUT_DIR:-/tmp/g4trap_validation}
PYTHON=${PYTHON:-python3}

mkdir -p "${OUT_DIR}"

COMPARE_PY="${REPO_DIR}/optiphy/ana/compare_ab.py"

# ------------------------------------------------------------------
# run helpers
# ------------------------------------------------------------------
G4_MACRO="${SCRIPT_DIR}/run_validate.mac"
G4_MACRO_5EVT="${SCRIPT_DIR}/run_validate_5evt_1t.mac"

run_torch_test () {
    local case=$1; local gdml=$2; local cfg=$3; local seed=${4:-42}
    local outd="${OUT_DIR}/${case}"
    mkdir -p "${outd}"; cd "${outd}"; rm -f g_hits.npy s_hits.npy
    simg4ox -g "${gdml}" -c "${cfg}" -m "${G4_MACRO}" -s "${seed}" > run.log 2>&1
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
    "${PYTHON}" "${COMPARE_PY}" hits "${OUT_DIR}/trap_iso/g_hits.npy" "${OUT_DIR}/trap_iso/s_hits.npy" \
        --count-relative-tolerance 5 --chi2-ndf-tolerance 5
}

test_trd_iso () {
    echo
    echo "----- Test: trd, isotropic source -----"
    run_torch_test trd_iso "${GEOM_DIR}/test_trd.gdml" trap_iso
    "${PYTHON}" "${COMPARE_PY}" hits "${OUT_DIR}/trd_iso/g_hits.npy" "${OUT_DIR}/trd_iso/s_hits.npy" \
        --count-relative-tolerance 5 --chi2-ndf-tolerance 5
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
    GPURaytrace -g "${GEOM_DIR}/test_trap_scint.gdml" -m "${G4_MACRO_5EVT}" -s 42 > run.log 2>&1
    "${PYTHON}" "${COMPARE_PY}" hits "${outd}/g4_hits_output.txt" "${outd}/opticks_hits_output.txt" \
        --count-relative-tolerance 10 --chi2-ndf-tolerance 50
}

test_scintillation_trd () {
    echo
    echo "----- Test: Scintillation+Cherenkov on trd, 5 x 5 GeV electrons -----"
    local outd="${OUT_DIR}/scintillation_trd"
    mkdir -p "${outd}"; cd "${outd}"; rm -f *_hits_output.txt
    GPURaytrace -g "${GEOM_DIR}/test_trd_scint.gdml" -m "${G4_MACRO_5EVT}" -s 42 > run.log 2>&1
    "${PYTHON}" "${COMPARE_PY}" hits "${outd}/g4_hits_output.txt" "${outd}/opticks_hits_output.txt" \
        --count-relative-tolerance 10 --chi2-ndf-tolerance 50
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
