#!/usr/bin/env python3
"""Validate simg4ox per-event CPU/GPU timing and run metadata."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path


ORDERED_COLUMNS = (
    "start_offset_s",
    "cpu_pre_start_time_offset_s",
    "cpu_pre_end_time_offset_s",
    "gpu_submit_time_offset_s",
    "gpu_wait_start_time_offset_s",
    "gpu_start_time_offset_s",
    "gpu_end_time_offset_s",
    "gpu_wait_end_time_offset_s",
    "cpu_post_start_time_offset_s",
    "cpu_post_end_time_offset_s",
    "end_offset_s",
)

NONNEGATIVE_COLUMNS = (
    "runtime_s",
    "cpu_pre_runtime_s",
    "cpu_post_runtime_s",
    "cpu_runtime_s",
    "gpu_queue_delay_s",
    "gpu_runtime_s",
    "gpu_wait_runtime_s",
    "process_cpu_s",
    "thread_cpu_s",
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("timing_csv", type=Path)
    parser.add_argument("--events", type=int, required=True)
    parser.add_argument("--particle")
    parser.add_argument("--require-photons", action="store_true")
    args = parser.parse_args()

    with args.timing_csv.open(newline="") as stream:
        rows = list(csv.DictReader(stream))

    if len(rows) != args.events:
        raise AssertionError(f"Expected {args.events} timing rows, found {len(rows)}")
    if [int(row["event_id"]) for row in rows] != list(range(args.events)):
        raise AssertionError("Event IDs are not consecutive and zero-based")

    for row in rows:
        event_id = int(row["event_id"])
        ordered = [float(row[column]) for column in ORDERED_COLUMNS]
        if not all(math.isfinite(value) for value in ordered):
            raise AssertionError(f"Event {event_id} contains non-finite timestamps")
        if any(later + 1e-9 < earlier for earlier, later in zip(ordered, ordered[1:])):
            raise AssertionError(f"Event {event_id} stage timestamps are not monotonic: {ordered}")
        for column in NONNEGATIVE_COLUMNS:
            value = float(row[column])
            if not math.isfinite(value) or value < 0:
                raise AssertionError(f"Event {event_id} has invalid {column}={value}")
        expected_wait = float(row["gpu_queue_delay_s"]) + float(row["gpu_runtime_s"])
        if not math.isclose(
            float(row["gpu_wait_runtime_s"]),
            expected_wait,
            rel_tol=1e-6,
            abs_tol=1e-9,
        ):
            raise AssertionError(f"Event {event_id} blocking GPU wait does not span queue plus execution")
        if args.particle and row["primary_particle"] != args.particle:
            raise AssertionError(f"Event {event_id} particle is {row['primary_particle']}, expected {args.particle}")
        if args.require_photons and int(row["num_photons"]) <= 0:
            raise AssertionError(f"Event {event_id} did not produce GPU optical photons")

    manifest_path = args.timing_csv.with_suffix(".manifest.json")
    manifest = json.loads(manifest_path.read_text())
    if manifest["schema_version"] != 2:
        raise AssertionError("Manifest does not use the shared timeline schema version")
    if manifest["dispatch_mode"] != "blocking":
        raise AssertionError("simg4ox timing manifest must identify blocking dispatch")
    if manifest["event_count"] != args.events:
        raise AssertionError("Manifest event_count does not match timing CSV")
    if args.particle and manifest["primary_particle"] != args.particle:
        raise AssertionError("Manifest particle does not match the requested source")

    print(f"TIMING_EVENTS={len(rows)}")
    print(f"TIMING_CPU_BUSY_S={sum(float(row['cpu_runtime_s']) for row in rows):.9f}")
    print(f"TIMING_GPU_BUSY_S={sum(float(row['gpu_runtime_s']) for row in rows):.9f}")


if __name__ == "__main__":
    main()
