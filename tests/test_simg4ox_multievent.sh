#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=${REPO_DIR:-$(cd "${SCRIPT_DIR}/.." && pwd)}

SIMG4OX_BIN=${SIMG4OX_BIN:-simg4ox}
PYTHON=${PYTHON:-python3}
RUN_LOG="${PWD}/simg4ox-multievent.log"
TIMING_CSV="${PWD}/events.csv"

export OPTICKS_HOME="${REPO_DIR}"
export SIMPHONY_CONFIG_DIR="${SIMPHONY_CONFIG_DIR:-${REPO_DIR}/config}"
export PYTHONPATH="${REPO_DIR}${PYTHONPATH:+:${PYTHONPATH}}"

rm -f "${PWD}/g_hits.npy" "${PWD}/s_hits.npy" "${RUN_LOG}" "${TIMING_CSV}" "${PWD}/events.manifest.json"

"${SIMG4OX_BIN}" \
    -g "${REPO_DIR}/tests/geom/opticks_raindrop.gdml" \
    -m "${REPO_DIR}/tests/run_5evt.mac" \
    -c dev \
    --timing-output "${TIMING_CSV}" 2>&1 | tee "${RUN_LOG}"

"${PYTHON}" "${REPO_DIR}/tests/check_simg4ox_timing.py" "${TIMING_CSV}" --events 5

"${PYTHON}" "${REPO_DIR}/tests/check_simg4ox_multievent.py" \
    --log "${RUN_LOG}" \
    --output-dir "${PWD}" \
    --events 5
