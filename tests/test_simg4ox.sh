#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
REPO_DIR=${REPO_DIR:-$(cd ${SCRIPT_DIR}/.. && pwd)}

SIMG4OX_BIN=${SIMG4OX_BIN:-simg4ox}
PYTHON=${PYTHON:-python3}
COMPARE_AB="${REPO_DIR}/optiphy/ana/compare_ab.py"

export OPTICKS_HOME="${REPO_DIR}"
export SIMPHONY_CONFIG_DIR="${SIMPHONY_CONFIG_DIR:-${REPO_DIR}/config}"
export PYTHONPATH="${REPO_DIR}${PYTHONPATH:+:${PYTHONPATH}}"

"${SIMG4OX_BIN}" -g "${REPO_DIR}/tests/geom/raindrop.gdml" -m "${REPO_DIR}/tests/run.mac" -c dev
"${PYTHON}" "${COMPARE_AB}"

SEED=42
NSIGMA=3
GEOM_FILE="${REPO_DIR}/tests/geom/8x8SiPM_w_CSI_optial_grease.gdml"
MAC_FILE="${REPO_DIR}/tests/run.mac"
HIT_OUTPUT_DIR="${PWD}/simg4ox_hits"

run_hit_validation() {
    local case_name=$1
    local geom_file=$2
    local config_name=$3
    local case_dir="${HIT_OUTPUT_DIR}/${case_name}"

    mkdir -p "${case_dir}"
    echo "=== simg4ox ${case_name} CPU/GPU hit validation ==="
    (
        cd "${case_dir}"
        "${SIMG4OX_BIN}" -g "${geom_file}" -c "${config_name}" -m "${MAC_FILE}" -s "${SEED}" > run.log 2>&1
    )

    "${PYTHON}" "${COMPARE_AB}" hits "${case_dir}/g_hits.npy" "${case_dir}/s_hits.npy" \
        --count-nsigma "${NSIGMA}" \
        --chi2-ndf-tolerance 5 \
        --require-hits
}

run_hit_validation 8x8-sipm "${GEOM_FILE}" 8x8SiPM_crystal

# This geometry has two distinct SensDet logical volumes.  It catches a
# regression where each PhotonSD overwrites g_hits.npy with its own collection.
run_hit_validation multi-sensdet "${REPO_DIR}/tests/geom/opticks_two_spheres.gdml" dev


GEOM_NAME=${1:-raindrop}
BUILD_TYPE=${2:-Debug}
KNOWN_GEOMETRIES=(raindrop nested_dune_module)

export OPTICKS_HOME=${REPO_DIR}
export GPHOX_CONFIG_DIR=${GPHOX_CONFIG_DIR:-${REPO_DIR}/config}
export PYTHONPATH=${REPO_DIR}${PYTHONPATH:+:${PYTHONPATH}}

usage() {
    cat >&2 <<EOF
Usage: $(basename $0) <geometry> <build-type>

Known geometries:
  raindrop
  nested_dune_module
EOF
}

if [[ $# -gt 2 ]]; then
    usage
    exit 2
fi

is_known_geometry() {
    local known

    for known in ${KNOWN_GEOMETRIES[@]}; do
        [[ ${GEOM_NAME} == ${known} ]] && return 0
    done

    return 1
}

if ! is_known_geometry; then
    echo Unknown simg4ox test geometry: ${GEOM_NAME} >&2
    usage
    exit 2
fi

GDML_FILE=${REPO_DIR}/tests/geom/${GEOM_NAME}.gdml

rm -rf ALL0_no_opticks_event_name g_hits.npy o_hits.npy run.npy run_meta.txt

echo === simg4ox: ${GEOM_NAME} ===
${SIMG4OX_BIN} -g ${GDML_FILE} -m ${REPO_DIR}/tests/run.mac -c ${GEOM_NAME}
${PYTHON} ${REPO_DIR}/optiphy/ana/compare_ab.py --base ${PWD} --geometry ${GEOM_NAME} --build-type ${BUILD_TYPE}
