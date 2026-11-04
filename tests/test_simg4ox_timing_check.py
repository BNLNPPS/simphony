"""CPU-only checks for the optical serial/MT timeline integration validator."""

import csv
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest


CHECKER = Path(__file__).with_name("check_simg4ox_timing.py")


def event_row(event_id, start, submit, gpu_start, gpu_end, release, end, threads=2):
    row = {
        "event_id": event_id, "dispatch_mode": "synchronous" if threads > 1 else "blocking",
        "start_offset_s": start, "cpu_pre_start_time_offset_s": start,
        "cpu_pre_end_time_offset_s": submit, "gpu_submit_time_offset_s": submit,
        "gpu_wait_start_time_offset_s": submit, "gpu_start_time_offset_s": gpu_start,
        "gpu_end_time_offset_s": gpu_end, "gpu_wait_end_time_offset_s": gpu_end,
        "cpu_post_start_time_offset_s": gpu_end, "cpu_post_end_time_offset_s": end,
        "end_offset_s": end, "gpu_release_time_offset_s": release,
        "runtime_s": end - start, "cpu_pre_runtime_s": submit - start,
        "cpu_post_runtime_s": end - gpu_end,
        "cpu_runtime_s": submit - start + end - gpu_end,
        "gpu_queue_delay_s": gpu_start - submit, "gpu_runtime_s": gpu_end - gpu_start,
        "gpu_wait_runtime_s": gpu_end - submit, "process_cpu_s": 0.1, "thread_cpu_s": 0.1,
        "cpu_post_locked_runtime_s": release - gpu_end,
        "cpu_post_unlocked_runtime_s": end - release, "gpu_gate_runtime_s": release - gpu_start,
        "primary_particle": "opticalphoton", "num_photons": 100,
        "worker_id": event_id % threads if threads > 1 else -1, "num_threads": threads,
    }
    return row


class TimingCheckTest(unittest.TestCase):
    def invoke(self, rows, *, threads=2, recorder_enabled=False):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "events.csv"
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            manifest = {
                "schema_version": 2, "dispatch_mode": "synchronous" if threads > 1 else "blocking",
                "event_count": len(rows), "num_threads": threads,
                "primary_particle": "opticalphoton", "primary_multiplicity": 100,
                "cpu_recorder_enabled": recorder_enabled, "torch_seed": 0,
            }
            path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            return subprocess.run(
                [sys.executable, str(CHECKER), str(path), "--events", str(len(rows)),
                 "--threads", str(threads), "--expected-photons", "100", "--expect-no-cpu-recorder"],
                capture_output=True, text=True, check=False,
            )

    def test_overlapping_cpu_intervals_and_serialized_gpu_gates_are_valid(self):
        result = self.invoke([
            event_row(0, 0, 0.3, 0.4, 0.6, 0.7, 0.8),
            event_row(1, 0.1, 0.5, 0.8, 1.0, 1.1, 1.2),
        ])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_gate_overlap_is_rejected_even_when_gpu_service_does_not_overlap(self):
        result = self.invoke([
            event_row(0, 0, 0.3, 0.4, 0.6, 0.7, 0.8),
            event_row(1, 0.1, 0.5, 0.65, 1.0, 1.1, 1.2),
        ])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GPU dispatch gate overlaps", result.stderr)

    def test_incorrect_primary_photon_count_is_rejected(self):
        row = event_row(0, 0, 0.3, 0.4, 0.6, 0.7, 0.8)
        row["num_photons"] = 0
        result = self.invoke([row])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected 100 photons", result.stderr)

    def test_matched_serial_control_accepts_the_worker_free_schema(self):
        result = self.invoke([event_row(0, 0, 0.3, 0.4, 0.6, 0.7, 0.8, threads=1)], threads=1)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_mt_requires_every_gpu_gate_measurement(self):
        for missing in ('gpu_release_time_offset_s', 'cpu_post_locked_runtime_s',
                        'cpu_post_unlocked_runtime_s', 'gpu_gate_runtime_s'):
            row = event_row(0, 0, 0.3, 0.4, 0.6, 0.7, 0.8)
            del row[missing]
            with self.subTest(missing=missing):
                result = self.invoke([row])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Missing MT gate measurements', result.stderr)

    def test_legacy_serial_schema_keeps_optional_gate_columns(self):
        row = event_row(0, 0, 0.3, 0.4, 0.6, 0.7, 0.8, threads=1)
        for name in ('gpu_release_time_offset_s', 'cpu_post_locked_runtime_s',
                     'cpu_post_unlocked_runtime_s', 'gpu_gate_runtime_s'):
            del row[name]
        result = self.invoke([row], threads=1)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_core_durations_must_match_their_recorded_boundaries(self):
        for column in ('runtime_s', 'cpu_pre_runtime_s', 'cpu_post_runtime_s',
                       'cpu_runtime_s', 'gpu_runtime_s', 'gpu_queue_delay_s'):
            row = event_row(0, 0, 0.3, 0.4, 0.6, 0.7, 0.8)
            row[column] = 1000.0
            with self.subTest(column=column):
                self.assertNotEqual(self.invoke([row]).returncode, 0)

    def test_cpu_recorder_mismatch_is_rejected(self):
        result = self.invoke([event_row(0, 0, 0.3, 0.4, 0.6, 0.7, 0.8)], recorder_enabled=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CPU recorder", result.stderr)


if __name__ == "__main__":
    unittest.main()
