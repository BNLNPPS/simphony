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
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--expected-photons", type=int)
    parser.add_argument("--expect-no-cpu-recorder", action="store_true")
    args = parser.parse_args()
    if args.threads < 1:
        parser.error("--threads must be positive")

    with args.timing_csv.open(newline="") as stream:
        rows = list(csv.DictReader(stream))

    if len(rows) != args.events:
        raise AssertionError(f"Expected {args.events} timing rows, found {len(rows)}")
    if [int(row["event_id"]) for row in rows] != list(range(args.events)):
        raise AssertionError("Event IDs are not consecutive and zero-based")

    if args.threads > 1:
        gate_columns = {'gpu_release_time_offset_s', 'cpu_post_locked_runtime_s',
                        'cpu_post_unlocked_runtime_s', 'gpu_gate_runtime_s'}
        for row in rows:
            missing = gate_columns - row.keys()
            if missing:
                raise AssertionError(f"Missing MT gate measurements: {sorted(missing)}")

    manifest_path = args.timing_csv.with_suffix(".manifest.json")
    manifest = json.loads(manifest_path.read_text())
    expected_dispatch = manifest["dispatch_mode"]
    valid_modes = {"asynchronous", "synchronous"} if args.threads > 1 else {"blocking"}
    if expected_dispatch not in valid_modes:
        raise AssertionError("Manifest dispatch mode does not match thread count")
    asynchronous = expected_dispatch == "asynchronous"
    if asynchronous and manifest.get("num_gpus", 0) <= 0:
        raise AssertionError("An asynchronous run must declare its GPU count")
    services = {}
    devices = {}
    producers = {}
    for row in rows:
        event_id = int(row["event_id"])
        device = int(row["gpu_device_id"]) if asynchronous else 0
        detached = asynchronous and device >= 0
        # For detached events the producer waits only for a queue slot.
        columns = tuple(c for c in ORDERED_COLUMNS if c != "gpu_wait_end_time_offset_s") if detached else ORDERED_COLUMNS
        ordered = [float(row[column]) for column in columns]
        if not all(math.isfinite(value) for value in ordered):
            raise AssertionError(f"Event {event_id} contains non-finite timestamps")
        if any(later + 1e-9 < earlier for earlier, later in zip(ordered, ordered[1:])):
            raise AssertionError(f"Event {event_id} stage timestamps are not monotonic: {ordered}")
        for column in NONNEGATIVE_COLUMNS:
            value = float(row[column])
            if not math.isfinite(value) or value < 0:
                raise AssertionError(f"Event {event_id} has invalid {column}={value}")
        identities = {
            'runtime_s': float(row['end_offset_s']) - float(row['start_offset_s']),
            'cpu_pre_runtime_s': float(row['gpu_submit_time_offset_s']) - float(row['start_offset_s']),
            'cpu_post_runtime_s': float(row['end_offset_s']) - float(row['gpu_end_time_offset_s']),
            'gpu_runtime_s': float(row['gpu_end_time_offset_s']) - float(row['gpu_start_time_offset_s']),
            'gpu_queue_delay_s': float(row['gpu_start_time_offset_s']) - float(row['gpu_submit_time_offset_s']),
        }
        identities['cpu_runtime_s'] = identities['cpu_pre_runtime_s'] + identities['cpu_post_runtime_s']
        for column, expected in identities.items():
            if not math.isclose(float(row[column]), expected, rel_tol=1e-6, abs_tol=1e-9):
                raise AssertionError(f"Event {event_id} has inconsistent {column}")
        submit = float(row["gpu_submit_time_offset_s"])
        gpu_start = float(row["gpu_start_time_offset_s"])
        producer_end = float(row["cpu_end_time_offset_s"]) if detached else float(row["end_offset_s"])
        if detached:
            if not submit <= producer_end <= gpu_start or producer_end != float(row["gpu_wait_end_time_offset_s"]):
                raise AssertionError(f"Event {event_id} has an invalid producer handoff")
            expected_wait = producer_end - submit
        else:
            expected_wait = float(row["gpu_queue_delay_s"]) + float(row["gpu_runtime_s"])
        if not math.isclose(
            float(row["gpu_wait_runtime_s"]),
            expected_wait,
            rel_tol=1e-6,
            abs_tol=1e-9,
        ):
            raise AssertionError(f"Event {event_id} GPU wait differs from its recorded bounds")
        release = float(row.get("gpu_release_time_offset_s", row["gpu_end_time_offset_s"]))
        gpu_end = float(row["gpu_end_time_offset_s"])
        event_end = float(row["end_offset_s"])
        if not math.isfinite(release) or not gpu_end <= release <= event_end:
            raise AssertionError(f"Event {event_id} has an invalid GPU gate release boundary")
        if detached:
            uuid = row["gpu_uuid"]
            if not uuid or device >= manifest["num_gpus"] or devices.setdefault(device, uuid) != uuid:
                raise AssertionError(f"Event {event_id} has an invalid GPU identity")
            service = [float(row[c]) for c in ("gpu_service_start_offset_s", "gpu_service_end_offset_s",
                                               "gpu_cleanup_end_offset_s")]
            if service != [gpu_start, gpu_end, release]:
                raise AssertionError(f"Event {event_id} has inconsistent GPU service bounds")
        elif asynchronous:
            if device != -1 or int(row["num_photons"]) or row["gpu_uuid"]:
                raise AssertionError(f"Event {event_id} has missing GPU service")
        if device >= 0:
            services.setdefault(device, []).append((gpu_start, release))
        for column, expected in (
            ("cpu_post_locked_runtime_s", release - gpu_end),
            ("cpu_post_unlocked_runtime_s", event_end - release),
            ("gpu_gate_runtime_s", release - float(row["gpu_start_time_offset_s"])),
        ):
            if column in row and not math.isclose(float(row[column]), expected, rel_tol=1e-6, abs_tol=1e-9):
                raise AssertionError(f"Event {event_id} has inconsistent {column}")
        if row.get("dispatch_mode", "blocking") != expected_dispatch:
            raise AssertionError(f"Event {event_id} dispatch mode does not match thread count")
        if int(row.get("num_threads", 1)) != args.threads:
            raise AssertionError(f"Event {event_id} num_threads differs from configured workers")
        if args.threads > 1:
            worker_id = int(row["worker_id"])
            if not 0 <= worker_id < args.threads:
                raise AssertionError(f"Event {event_id} has an invalid worker ID")
            producers.setdefault(worker_id, []).append((float(row["start_offset_s"]), producer_end))
        if args.expected_photons is not None and int(row["num_photons"]) != args.expected_photons:
            raise AssertionError(f"Event {event_id} expected {args.expected_photons} photons")
        if args.particle and row["primary_particle"] != args.particle:
            raise AssertionError(f"Event {event_id} particle is {row['primary_particle']}, expected {args.particle}")
        if args.require_photons and int(row["num_photons"]) <= 0:
            raise AssertionError(f"Event {event_id} did not produce GPU optical photons")

    if len(set(devices.values())) != len(devices):
        raise AssertionError("Multiple GPU workers used the same device UUID")
    for label, groups in (("GPU dispatch gate", services), ("Geant4 producer", producers)):
        for worker, spans in groups.items():
            spans.sort()
            if any(start + 1e-9 < end for (_, end), (start, _) in zip(spans, spans[1:])):
                raise AssertionError(f"{label} overlaps on worker {worker}")

    if manifest["schema_version"] != 2:
        raise AssertionError("Manifest does not use the shared timeline schema version")
    if manifest.get("num_threads", 1) != args.threads:
        raise AssertionError("Manifest num_threads does not match configured workers")
    if args.expect_no_cpu_recorder and manifest.get("cpu_recorder_enabled") is not False:
        raise AssertionError("Manifest CPU recorder state does not match the serial/MT control")
    if args.expect_no_cpu_recorder and manifest.get("torch_seed") != 0:
        raise AssertionError("Matched optical timing requires the declared fixed torch input seed zero")
    if manifest["event_count"] != args.events:
        raise AssertionError("Manifest event_count does not match timing CSV")
    if args.particle and manifest["primary_particle"] != args.particle:
        raise AssertionError("Manifest particle does not match the requested source")

    print(f"TIMING_EVENTS={len(rows)}")
    print(f"TIMING_CPU_BUSY_S={sum(float(row['cpu_runtime_s']) for row in rows):.9f}")
    print(f"TIMING_GPU_BUSY_S={sum(float(row['gpu_runtime_s']) for row in rows):.9f}")


if __name__ == "__main__":
    main()
