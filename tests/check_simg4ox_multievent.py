#!/usr/bin/env python3
"""Validate that simg4ox saves run-wide hit arrays for a multi-event run."""

import argparse
import re
from pathlib import Path

import numpy as np


EVENT_PATTERNS = {
    "GPU": re.compile(r"EventAction::EndOfEventAction: Event (\d+): Collected GPU hits:\s*(\d+)"),
    "G4": re.compile(r"EventAction::EndOfEventAction: Event (\d+): Collected G4\s+hits:\s*(\d+)"),
}
PHOTON_PATTERN = re.compile(
    r"EventAction::SimulateOnGPU: Event (\d+): Simulated GPU photons:\s*(\d+)"
)
TOTAL_PATTERNS = {
    "GPU": re.compile(r"Total GPU hits:\s*(\d+)"),
    "G4": re.compile(r"Total G4\s+hits:\s*(\d+)"),
}


def order_event_counts(matches, label, events):
    parsed = [(int(event_id), int(value)) for event_id, value in matches]
    event_ids = [event_id for event_id, _ in parsed]

    if len(parsed) != events:
        raise AssertionError(f"Expected {events} {label} event counts, found {len(parsed)}")
    if sorted(event_ids) != list(range(events)):
        raise AssertionError(f"Expected {label} event ids 0..{events - 1}, found {event_ids}")

    counts_by_event = dict(parsed)
    return [counts_by_event[event_id] for event_id in range(events)]


def parse_counts(log_text, label, events):
    event_counts = order_event_counts(EVENT_PATTERNS[label].findall(log_text), label, events)
    total_matches = TOTAL_PATTERNS[label].findall(log_text)

    if len(total_matches) != 1:
        raise AssertionError(f"Expected one {label} run total, found {len(total_matches)}")

    total = int(total_matches[0])
    if total != sum(event_counts):
        raise AssertionError(f"{label} run total {total} != per-event sum {sum(event_counts)}")
    if total == 0:
        raise AssertionError(f"Expected non-empty {label} hits")

    return event_counts, total


def parse_photon_counts(log_text, events):
    photon_counts = order_event_counts(PHOTON_PATTERN.findall(log_text), "GPU photon", events)
    if any(value == 0 for value in photon_counts):
        raise AssertionError(f"Expected non-empty GPU photon counts, found {photon_counts}")

    return photon_counts


def check_array(path, expected_rows):
    if not path.is_file():
        raise FileNotFoundError(f"Missing run hit array: {path}")

    hits = np.load(path)
    expected_shape = (expected_rows, 4, 4)
    if hits.shape != expected_shape:
        raise AssertionError(f"{path.name} shape {hits.shape} != {expected_shape}")
    if hits.dtype != np.float32:
        raise AssertionError(f"{path.name} dtype {hits.dtype} != float32")

    return hits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--events", type=int, default=5)
    args = parser.parse_args()

    log_text = args.log.read_text()
    gpu_photon_counts = parse_photon_counts(log_text, args.events)
    gpu_event_counts, gpu_total = parse_counts(log_text, "GPU", args.events)
    g4_event_counts, g4_total = parse_counts(log_text, "G4", args.events)

    gpu_hits = check_array(args.output_dir / "s_hits.npy", gpu_total)
    g4_hits = check_array(args.output_dir / "g_hits.npy", g4_total)

    print(f"GPU_EVENT_PHOTON_COUNTS={gpu_photon_counts}")
    print(f"GPU_EVENT_COUNTS={gpu_event_counts}")
    print(f"G4_EVENT_COUNTS={g4_event_counts}")
    print(f"S_HITS_SHAPE={gpu_hits.shape}")
    print(f"G_HITS_SHAPE={g4_hits.shape}")


if __name__ == "__main__":
    main()
