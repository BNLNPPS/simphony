#!/usr/bin/env python3
"""Pass/fail comparisons for persisted Geant4 and GPU outputs.

With no subcommand, validates paired ``record.npy`` outputs.  The ``hits``
subcommand compares Geant4 and GPU hit data stored either as ``sphoton``
NPY arrays or in GPURaytrace's legacy text format.
"""

import argparse
import math
import os
import re
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

HIT_TEXT_PATTERN = re.compile(
    r"^\s*([\-\d.eE+]+)\s+([\-\d.eE+]+)\s+\(([^)]+)\)\s+\(([^)]+)\)"
)


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


def record_parser():
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
    return parser


def compare_records_main(args):
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


def load_hits(path):
    path = Path(path)
    if not path.is_file():
        raise FileNotFoundError(f"Missing hit file: {path}")

    if path.suffix == ".npy":
        hits = np.load(path)
        if hits.ndim != 3 or hits.shape[1:] != (4, 4):
            raise ValueError(f"Expected sphoton array with shape (N, 4, 4), got {hits.shape} from {path}")

        return np.column_stack((hits[:, 0, 3], hits[:, 1, 3], hits[:, 0, :3], hits[:, 1, :3])), "npy"

    rows = []
    for line in path.read_text().splitlines():
        match = HIT_TEXT_PATTERN.match(line)
        if not match:
            continue
        rows.append(
            (
                float(match.group(1)),
                float(match.group(2)),
                *[float(value) for value in match.group(3).split(",")],
                *[float(value) for value in match.group(4).split(",")],
            )
        )

    return (np.array(rows, dtype=float) if rows else np.empty((0, 8))), "text"


def chi2_1d(a, b, bins):
    a_histogram, _ = np.histogram(a, bins=bins)
    b_histogram, _ = np.histogram(b, bins=bins)
    populated = (a_histogram + b_histogram) > 0
    chi2 = np.sum(
        (a_histogram[populated] - b_histogram[populated]) ** 2
        / (a_histogram[populated] + b_histogram[populated])
    )
    return float(chi2), int(populated.sum())


def hit_label(g4_path, gpu_path):
    common_parent = hit_output_dir(g4_path, gpu_path)
    return common_parent.name or str(common_parent)


def hit_output_dir(g4_path, gpu_path):
    return Path(os.path.commonpath((Path(g4_path).resolve().parent, Path(gpu_path).resolve().parent)))


def hit_parser():
    parser = argparse.ArgumentParser(description="Compare Geant4 and GPU hit files.")
    parser.add_argument("g4_hits", help="Geant4 hit file (.npy or legacy text)")
    parser.add_argument("gpu_hits", help="GPU hit file (.npy or legacy text)")
    count_policy = parser.add_mutually_exclusive_group()
    count_policy.add_argument(
        "--count-relative-tolerance",
        type=float,
        default=5.0,
        help="maximum relative hit-count difference in percent (default: %(default)s)",
    )
    count_policy.add_argument(
        "--count-nsigma",
        type=float,
        help="maximum Poisson count difference in standard deviations",
    )
    parser.add_argument(
        "--chi2-ndf-tolerance",
        type=float,
        default=5.0,
        help="maximum chi2/ndf for each position/direction distribution (default: %(default)s)",
    )
    parser.add_argument("--require-hits", action="store_true", help="fail when either input has no hits")
    return parser


def distribution_bins(g4_values, gpu_values):
    lower = min(g4_values.min(), gpu_values.min())
    upper = max(g4_values.max(), gpu_values.max())
    if lower == upper:
        padding = max(abs(lower) * 0.01, 1e-6)
        lower -= padding
        upper += padding
    return np.linspace(lower, upper, 31)


def compare_hits(args):
    if args.count_relative_tolerance < 0:
        raise ValueError("--count-relative-tolerance must be non-negative")
    if args.count_nsigma is not None and args.count_nsigma <= 0:
        raise ValueError("--count-nsigma must be positive")
    if args.chi2_ndf_tolerance < 0:
        raise ValueError("--chi2-ndf-tolerance must be non-negative")

    g4, g4_format = load_hits(args.g4_hits)
    gpu, gpu_format = load_hits(args.gpu_hits)
    n_g4, n_gpu = len(g4), len(gpu)
    label = hit_label(args.g4_hits, args.gpu_hits)
    output_dir = hit_output_dir(args.g4_hits, args.gpu_hits)
    failures = []

    print(f"=== {label} ===")
    print(f"  Output dir:   {output_dir}")
    print(f"  G4 input:     {Path(args.g4_hits).resolve()} ({g4_format})")
    print(f"  GPU input:    {Path(args.gpu_hits).resolve()} ({gpu_format})")
    print(f"  G4 hits:      {n_g4}")
    print(f"  GPU hits:     {n_gpu}")

    if args.require_hits and (n_g4 == 0 or n_gpu == 0):
        failures.append("empty required hit input")

    difference = abs(n_gpu - n_g4)
    if args.count_nsigma is None:
        total = n_g4 + n_gpu
        relative_difference = difference / (total / 2) * 100 if total else 0.0
        print(f"  rel diff:     {relative_difference:.3f}%   (tol={args.count_relative_tolerance}%)")
        if relative_difference > args.count_relative_tolerance:
            failures.append(f"count rel-diff {relative_difference:.2f}% > {args.count_relative_tolerance}%")
    else:
        threshold = math.floor(args.count_nsigma * math.sqrt(n_g4 + n_gpu) + 1)
        print(f"  count diff:   {difference}   ({args.count_nsigma}-sigma threshold={threshold})")
        if difference > threshold:
            failures.append(f"count difference {difference} > {args.count_nsigma}-sigma threshold {threshold}")

    if n_g4 == 0 or n_gpu == 0:
        print("  no hits, skip distributions")
    else:
        for column, name, bins in (
            (2, "x", distribution_bins(g4[:, 2], gpu[:, 2])),
            (3, "y", distribution_bins(g4[:, 3], gpu[:, 3])),
            (5, "dx", np.linspace(-1, 1, 21)),
            (6, "dy", np.linspace(-1, 1, 21)),
            (7, "dz", np.linspace(-1, 1, 21)),
        ):
            chi2, ndf = chi2_1d(g4[:, column], gpu[:, column], bins)
            ratio = chi2 / max(ndf, 1)
            marker = "FAIL" if ratio > args.chi2_ndf_tolerance else "ok"
            print(f"  {name:>2}: chi2/ndf = {chi2:7.2f}/{ndf} = {ratio:5.2f}  {marker}")
            if ratio > args.chi2_ndf_tolerance:
                failures.append(f"{name} chi2/ndf {ratio:.2f} > {args.chi2_ndf_tolerance}")

    if failures:
        print(f"  FAILED: {', '.join(failures)}")
        return 1

    print("  PASS")
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "hits":
        return compare_hits(hit_parser().parse_args(sys.argv[2:]))

    args = record_parser().parse_args()

    compare_records_main(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
