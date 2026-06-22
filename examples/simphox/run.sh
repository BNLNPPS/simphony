#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

build="${SIMPHONY_SIMPHOX_BUILD_DIR:-/tmp/simphony-examples-build}"
prefix="${SIMPHONY_SIMPHOX_INSTALL_PREFIX:-/tmp/simphony-examples-install}"
run_dir="${SIMPHONY_SIMPHOX_RUN_DIR:-/tmp}"
cmake_prefix_path="${SIMPHONY_PREFIX:-${CMAKE_PREFIX_PATH:-}}"

if [[ -z "$cmake_prefix_path" ]]; then
    echo "SIMPHONY_PREFIX or CMAKE_PREFIX_PATH must point to a Simphony install prefix" >&2
    exit 1
fi

cmake -S "$script_dir" -B "$build" -DCMAKE_PREFIX_PATH="$cmake_prefix_path" -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$build" --target install

simphony_lib_dir="${SIMPHONY_LIB_DIR:-${SIMPHONY_PREFIX:-$cmake_prefix_path}/lib}"
export LD_LIBRARY_PATH="$simphony_lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

mkdir -p "$run_dir"
cd "$run_dir"
"$prefix/bin/simphox" --cpu
test -f "$run_dir/out/photons.npy"
rm -f "$run_dir/out/photons.npy"
"$prefix/bin/simphox" --gpu
test -f "$run_dir/out/photons.npy"
