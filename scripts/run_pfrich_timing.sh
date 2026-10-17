#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
SIMG4OX_BIN=${SIMG4OX_BIN:-simg4ox}
OUTPUT_DIR=${1:-${REPO_DIR}/pfrich_timing}
CONFIG_NAME=${2:-pfrich}

mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR=$(cd "${OUTPUT_DIR}" && pwd -P)

export OPTICKS_HOME="${REPO_DIR}"
export SIMPHONY_CONFIG_DIR="${SIMPHONY_CONFIG_DIR:-${REPO_DIR}/config}"
export OPTICKS_EVENT_MODE=Minimal
export PYTHONPATH="${REPO_DIR}${PYTHONPATH:+:${PYTHONPATH}}"
export CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-0}"

CONFIG_PATH="${SIMPHONY_CONFIG_DIR}/${CONFIG_NAME}.json"
mapfile -t PROFILE_CONFIG < <(
    python3 -c '
import json
import pathlib
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    config = json.load(stream)
profile = config.get("event_timing", {}).get("profile", {})
base = pathlib.Path(config.get("event", {}).get("output_dir", sys.argv[2]))
if not base.is_absolute():
    base = pathlib.Path(sys.argv[2]) / base
output = pathlib.Path(profile.get("output", "event_timing_profile.csv"))
if not output.is_absolute():
    output = base / output
path = str(output)
if "%" in path:
    path %= int(profile.get("path_index", 0))
print("1" if profile.get("enabled", False) else "0")
print(path)
' "${CONFIG_PATH}" "${OUTPUT_DIR}"
)
PROFILE_ENABLED=${PROFILE_CONFIG[0]}
PROFILE_CSV=${PROFILE_CONFIG[1]}
rm -f -- "${PROFILE_CSV}"

cd "${OUTPUT_DIR}"

RUN_LOG="${OUTPUT_DIR}/simg4ox.stdout.log"

if ! "${SIMG4OX_BIN}" \
    --gdml "${REPO_DIR}/tests/geom/pfrich_min_FINAL.gdml" \
    --macro "${REPO_DIR}/tests/run_pfrich_timing.mac" \
    --config "${CONFIG_NAME}" \
    --particle mu- \
    --momentum-gev-c 5 \
    --multiplicity 1 \
    --seed 42 > "${RUN_LOG}" 2>&1; then
    tail -n 100 "${RUN_LOG}" >&2
    exit 1
fi

grep -E "^(Primary source:|RunAction::EndOfRunAction:)" "${RUN_LOG}" || true

python3 "${REPO_DIR}/tests/check_simg4ox_timing.py" \
    "${OUTPUT_DIR}/events.csv" \
    --events 3 \
    --particle mu- \
    --require-photons

if [[ "${PROFILE_ENABLED}" == "1" ]]; then
    if [[ ! -f "${PROFILE_CSV}" ]]; then
        echo "Expected configured event timing profile: ${PROFILE_CSV}" >&2
        exit 1
    fi
    python3 "${REPO_DIR}/tests/check_event_timing_profile.py" "${PROFILE_CSV}"
elif [[ -e "${PROFILE_CSV}" ]]; then
    echo "Detailed event timing profile was written while disabled: ${PROFILE_CSV}" >&2
    exit 1
fi
