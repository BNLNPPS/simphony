#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
SIMG4OX_BIN=${SIMG4OX_BIN:-simg4ox}
OUTPUT_DIR=${1:-${REPO_DIR}/pfrich_timing}

mkdir -p "${OUTPUT_DIR}"

export OPTICKS_HOME="${REPO_DIR}"
export SIMPHONY_CONFIG_DIR="${SIMPHONY_CONFIG_DIR:-${REPO_DIR}/config}"
export OPTICKS_EVENT_MODE=Minimal
export PYTHONPATH="${REPO_DIR}${PYTHONPATH:+:${PYTHONPATH}}"
export CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-0}"

cd "${OUTPUT_DIR}"

RUN_LOG="${OUTPUT_DIR}/simg4ox.stdout.log"

if ! "${SIMG4OX_BIN}" \
    --gdml "${REPO_DIR}/tests/geom/pfrich_min_FINAL.gdml" \
    --macro "${REPO_DIR}/tests/run_pfrich_timing.mac" \
    --config pfrich \
    --particle mu- \
    --momentum-gev-c 5 \
    --multiplicity 1 \
    --seed 42 \
    --timing-output "${OUTPUT_DIR}/events.csv" > "${RUN_LOG}" 2>&1; then
    tail -n 100 "${RUN_LOG}" >&2
    exit 1
fi

grep -E "^(Primary source:|RunAction::EndOfRunAction:)" "${RUN_LOG}" || true

python3 "${REPO_DIR}/tests/check_simg4ox_timing.py" \
    "${OUTPUT_DIR}/events.csv" \
    --events 3 \
    --particle mu- \
    --require-photons
