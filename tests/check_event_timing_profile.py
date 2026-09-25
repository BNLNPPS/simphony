#!/usr/bin/env python3
"""Validate a detailed EventTimingProfile CSV."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


HEADER = (
    "name",
    "wall_time_us",
    "steady_time_ns",
    "vm_kb",
    "rss_kb",
    "metadata",
)
REQUIRED_PREFIXES = ("SEvt__", "CSGFoundry__", "CSGOptiX__", "A000_QSim__")
INTEGER_FIELDS = ("wall_time_us", "steady_time_ns", "vm_kb", "rss_kb")


def fail(path: Path, message: str) -> None:
    raise AssertionError(f"{path}: {message}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile_csv", type=Path)
    args = parser.parse_args()

    with args.profile_csv.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != HEADER:
            fail(args.profile_csv, f"expected header {HEADER}, found {reader.fieldnames}")
        rows = list(reader)

    if not rows:
        fail(args.profile_csv, "contains no profile records")

    prior_steady = -1
    names: set[str] = set()
    prefix_counts = {prefix: 0 for prefix in REQUIRED_PREFIXES}
    for line_number, row in enumerate(rows, start=2):
        name = row["name"]
        names.add(name)
        for prefix in REQUIRED_PREFIXES:
            prefix_counts[prefix] += name.startswith(prefix)

        values: dict[str, int] = {}
        for field in INTEGER_FIELDS:
            try:
                values[field] = int(row[field])
            except (TypeError, ValueError) as error:
                fail(args.profile_csv, f"row {line_number} {name!r} has invalid {field}={row[field]!r}: {error}")

        if values["steady_time_ns"] < prior_steady:
            fail(args.profile_csv, f"row {line_number} {name!r} has decreasing steady_time_ns")
        if values["vm_kb"] < 0 or values["rss_kb"] < 0:
            fail(args.profile_csv, f"row {line_number} {name!r} has negative memory")
        prior_steady = values["steady_time_ns"]

    missing = [prefix for prefix, count in prefix_counts.items() if count == 0]
    if missing:
        fail(args.profile_csv, f"missing records with prefixes: {', '.join(missing)}")

    launch_pairs = 0
    for name in names:
        if name.endswith("QSim__simulate_PREL"):
            launch_pairs += (name.removesuffix("PREL") + "POST") in names
    if launch_pairs == 0:
        fail(args.profile_csv, "contains no matching QSim PREL/POST launch pair")

    print(f"PROFILE_RECORDS={len(rows)}")
    print(f"PROFILE_LAUNCH_PAIRS={launch_pairs}")


if __name__ == "__main__":
    main()
