#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=${REPO_DIR:-$(cd "${SCRIPT_DIR}/.." && pwd)}

SIMG4OX_BIN=${SIMG4OX_BIN:-simg4ox}
PYTHON=${PYTHON:-python3}
COMPARE_AB="${REPO_DIR}/optiphy/ana/compare_ab.py"
TEST_CASE=${1:-raindrop}

export OPTICKS_HOME="${REPO_DIR}"
export SIMPHONY_CONFIG_DIR="${SIMPHONY_CONFIG_DIR:-${REPO_DIR}/config}"
export PYTHONPATH="${REPO_DIR}${PYTHONPATH:+:${PYTHONPATH}}"

usage() {
    echo "Usage: $(basename "$0") [test-case]" >&2
    echo "Test cases:" >&2
    echo "  raindrop" >&2
    echo "  dune_mock_wls_detector_box" >&2
    echo "  8x8SiPM_w_CSI_optial_grease" >&2
    echo "  opticks_two_spheres" >&2
    echo "  drich_direct_sensor" >&2
    echo "  drich_aerogel" >&2
    echo "  drich_mirror" >&2
    echo "  sibling_pair" >&2
}

if [[ $# -gt 1 ]]; then
    usage
    exit 2
fi

SEED=42
NSIGMA=3
MAC_FILE="${REPO_DIR}/tests/run.mac"

run_record_validation() {
    local geometry=$1
    local config_name=$2

    rm -rf "${PWD}/ALL0_no_opticks_event_name"

    echo "=== simg4ox ${geometry} record validation ==="
    "${SIMG4OX_BIN}" \
        -g "${REPO_DIR}/tests/geom/${geometry}.gdml" \
        -m "${MAC_FILE}" \
        -c "${config_name}"

    "${PYTHON}" "${COMPARE_AB}" --base "${PWD}" --geometry "${geometry}"
}

run_hit_validation() {
    local geometry=$1
    local config_name=$2
    local macro_file=${3:-${MAC_FILE}}
    local run_log="${PWD}/simg4ox.log"

    rm -f "${PWD}/g_hits.npy" "${PWD}/s_hits.npy" "${run_log}"

    echo "=== simg4ox ${geometry} CPU/GPU hit validation ==="
    "${SIMG4OX_BIN}" \
        -g "${REPO_DIR}/tests/geom/${geometry}.gdml" \
        -c "${config_name}" \
        -m "${macro_file}" \
        -s "${SEED}" > "${run_log}" 2>&1

    "${PYTHON}" "${COMPARE_AB}" hits "${PWD}/g_hits.npy" "${PWD}/s_hits.npy" \
        --count-nsigma "${NSIGMA}" \
        --chi2-ndf-tolerance 5 \
        --require-hits
}

case "${TEST_CASE}" in
    raindrop)
        run_record_validation raindrop dev
        ;;
    dune_mock_wls_detector_box)
        run_record_validation dune_mock_wls_detector_box dune_mock_wls_detector_box
        ;;
    8x8SiPM_w_CSI_optial_grease)
        run_hit_validation 8x8SiPM_w_CSI_optial_grease 8x8SiPM_crystal
        ;;
    opticks_two_spheres)
        # This geometry has two distinct SensDet logical volumes. It catches a
        # regression where each PhotonSD overwrites g_hits.npy with its own collection.
        run_hit_validation opticks_two_spheres dev
        ;;
    drich_direct_sensor)
        # Loading the full geometry exercises conversion of its partial-phi
        # sphere and tube solids. The torch aims identical optical photons at
        # a SiPM patch for a stable, nonzero CPU/GPU hit comparison.
        # Invoke Geant4 SDs for the EFFICIENCY=1 SiPM skin surfaces.
        run_hit_validation drich drich_direct_sensor "${REPO_DIR}/tests/run_validate.mac"
        ;;
    drich_aerogel)
        # Start inside the realistic aerogel aperture and aim photons through
        # the air gap and filter, off the mirror, and onto a SiPM patch.
        run_hit_validation drich drich_aerogel "${REPO_DIR}/tests/run_validate.mac"
        ;;
    drich_mirror)
        # Start in the dRICH gas and aim at the mirror so detected photons must
        # undergo one specular reflection before reaching a SiPM patch.
        run_hit_validation drich drich_mirror "${REPO_DIR}/tests/run_validate.mac"
        ;;
    sibling_pair)
        # The beam can reach the sensor only by crossing the exact shared face
        # from the left sibling into the right sibling while retaining the
        # correct current-medium properties for the segment ending at that face.
        run_hit_validation sibling_pair sibling_pair "${REPO_DIR}/tests/run_validate.mac"
        ;;
    *)
        echo "Unknown simg4ox test case: ${TEST_CASE}" >&2
        usage
        exit 2
        ;;
esac
