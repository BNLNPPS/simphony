#!/usr/bin/env bash
# run.sh — build and run the synrad example on both bundled geometries
#
# Usage:
#   SIMPHONY_PREFIX=/path/to/simphony/install ./run.sh

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

build="${SIMPHONY_SYNRAD_BUILD_DIR:-/tmp/simphony-synrad-build}"
prefix="${SIMPHONY_SYNRAD_INSTALL_PREFIX:-/tmp/simphony-synrad-install}"
run_dir="${SIMPHONY_SYNRAD_RUN_DIR:-/tmp/simphony-synrad-run}"
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

# coarse envelope (1252 facets, the analytic-benchmark-equivalent chamber)
"$prefix/bin/synrad" -g "$script_dir/synrad_bench.gdml" -n 100000 -o "$run_dir"
test -f "$run_dir/synrad_hits.npy"

# fine CAD-like tessellation of the same chamber (25k facets)
"$prefix/bin/synrad" -g "$script_dir/synrad_bench_tess.gdml" -n 100000 -o "$run_dir"
test -f "$run_dir/synrad_hits.npy"
