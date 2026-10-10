#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]
then
    echo "usage: $0 READELF EXECUTABLE..." >&2
    exit 2
fi

readelf_executable=$1
shift

for executable in "$@"
do
    if [[ ! -x "${executable}" ]]
    then
        echo "missing executable: ${executable}" >&2
        exit 2
    fi

    dynamic_section=$("${readelf_executable}" -d "${executable}")
    if [[ "${dynamic_section}" == *"libG4CX.so"* ]]
    then
        echo "${executable} still depends on libG4CX.so" >&2
        exit 1
    fi
done
