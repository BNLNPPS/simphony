#!/usr/bin/env python3
"""
compare_ab.py : pass/fail comparison of A/B event records
=========================================================

Validates persisted `record.npy` outputs from paired A/B event directories by
comparing aligned Opticks/G4 records and checking the known
Geant4-version-dependent mismatch indices.
"""

import argparse
import os
import sys
from pathlib import Path

import numpy as np


REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from optiphy.geant4_version import detect_geant4_version, geant4_series


EXPECTED_DIFF = {
    "11.3": {
        "default": [14, 22, 32, 34, 40, 81, 85],
        "Release": [14, 16, 22, 32, 40, 67, 81],
    },
    "11.4+": {
        "default": [0, 30, 32, 34, 42, 69, 78, 85, 86],
        "Release": [0, 16, 30, 32, 42, 67, 69, 78, 86],
    },
}


def detect_build_type(base):
    build_type = os.environ.get("SIMPHONY_BUILD_TYPE") or os.environ.get("CMAKE_BUILD_TYPE")
    if build_type:
        return build_type

    for path in (base, *base.parents):
        cache_path = path / "CMakeCache.txt"
        if not cache_path.is_file():
            continue

        for line in cache_path.read_text().splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:"):
                return line.split("=", 1)[1].strip()

    return "default"


def expected_diff_for_version(version, build_type):
    expected_by_build = EXPECTED_DIFF[geant4_series(version)]
    return expected_by_build.get(build_type, expected_by_build["default"])


def load_records(base, a_record, b_record):
    a_path = base / a_record
    b_path = base / b_record

    if not a_path.is_file():
        raise FileNotFoundError(f"Missing Opticks record file: {a_path}")
    if not b_path.is_file():
        raise FileNotFoundError(f"Missing Geant4 record file: {b_path}")

    return np.load(a_path), np.load(b_path)


def compare_records(a, b):
    if a.shape != b.shape:
        raise AssertionError(f"Shape mismatch: {a.shape} != {b.shape}")

    return [
        index
        for index, (a_row, b_row) in enumerate(zip(a, b))
        if not np.allclose(a_row, b_row, rtol=0.0, atol=1e-5)
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default=".", help="directory containing the A/B event outputs")
    parser.add_argument(
        "--a-record",
        default="ALL0_no_opticks_event_name/A000/record.npy",
        help="path to the A-side record.npy relative to --base",
    )
    parser.add_argument(
        "--b-record",
        default="ALL0_no_opticks_event_name/B000/f000/record.npy",
        help="path to the B-side record.npy relative to --base",
    )
    args = parser.parse_args()

    base = Path(args.base).resolve()
    geant4_version = detect_geant4_version()
    build_type = detect_build_type(base)
    expected_diff = expected_diff_for_version(geant4_version, build_type)

    a, b = load_records(base, Path(args.a_record), Path(args.b_record))
    diff = compare_records(a, b)

    print(f"BASE={base}")
    print(f"A_SHAPE={a.shape}")
    print(f"B_SHAPE={b.shape}")
    print(f"GEANT4_VERSION={geant4_version}")
    print(f"BUILD_TYPE={build_type}")
    print(f"EXPECTED_DIFF={expected_diff}")
    print(f"ACTUAL_DIFF={diff}")

    if diff != expected_diff:
        raise AssertionError(f"Mismatch indices differ: expected {expected_diff}, got {diff}")


if __name__ == "__main__":
    main()
