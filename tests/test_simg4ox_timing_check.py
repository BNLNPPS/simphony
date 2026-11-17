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


def gpu_row(row, device=0):
    return dict(row, gpu_dispatch="pool", gpu_device_id=device, gpu_uuid=f"GPU-{device}",
                gpu_service_start_offset_s=row["gpu_start_time_offset_s"],
                gpu_service_end_offset_s=row["gpu_end_time_offset_s"] - .01,
                gpu_cleanup_end_offset_s=row["gpu_end_time_offset_s"])


def direct_row(row):
    return dict(gpu_row(row), gpu_dispatch="direct",
                gpu_service_end_offset_s=row["gpu_end_time_offset_s"],
                gpu_cleanup_end_offset_s=row["gpu_release_time_offset_s"])


class TimingCheckTest(unittest.TestCase):
    def invoke(self, rows, *, threads=2, recorder_enabled=False, gpu_dispatch=None, expected_photons=100):
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
            if "gpu_dispatch" in rows[0]:
                manifest["gpu_dispatch"] = gpu_dispatch or rows[0]["gpu_dispatch"]
                manifest["gpu_devices"] = [{"ordinal": i, "uuid": f"GPU-{i}"}
                    for i in sorted({row["gpu_device_id"] for row in rows})]
            path.with_suffix(".manifest.json").write_text(json.dumps(manifest))
            return subprocess.run(
                [sys.executable, str(CHECKER), str(path), "--events", str(len(rows)),
                 "--threads", str(threads), "--expected-photons", str(expected_photons), "--expect-no-cpu-recorder"],
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

    def test_different_gpu_services_can_overlap(self):
        result = self.invoke([
            gpu_row(event_row(0, 0, .2, .3, .7, .8, .9), 0),
            gpu_row(event_row(1, .1, .2, .4, .6, .7, .8), 1),
        ])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_same_gpu_service_overlap_is_rejected(self):
        result = self.invoke([
            gpu_row(event_row(0, 0, .2, .3, .7, .8, .9)),
            gpu_row(event_row(1, .1, .2, .4, .6, .7, .8)),
        ])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GPU dispatch gate overlaps", result.stderr)

    def test_pool_completions_need_not_follow_event_id(self):
        result = self.invoke([
            gpu_row(event_row(0, 0, .8, 1., 2., 2.1, 2.2)),
            gpu_row(event_row(1, .1, .2, .3, .5, .6, .7)),
        ])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_direct_gates_cannot_run_in_reverse_event_order(self):
        result = self.invoke([
            direct_row(event_row(0, 0, .8, 1., 2., 2.1, 2.2)),
            direct_row(event_row(1, .1, .2, .3, .5, .6, .7)),
        ])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GPU dispatch gate overlaps", result.stderr)

    def test_empty_direct_events_also_advance_the_ordered_gate(self):
        def empty(row):
            return dict(direct_row(row), num_photons=0, gpu_device_id=-1, gpu_uuid='',
                        gpu_service_start_offset_s=0, gpu_service_end_offset_s=0,
                        gpu_cleanup_end_offset_s=0)

        later = empty(event_row(0, 0, .8, 1., 1., 1.1, 1.2))
        earlier = empty(event_row(1, .1, .2, .3, .3, .4, .5))
        result = self.invoke([later, earlier], expected_photons=0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GPU dispatch gate overlaps", result.stderr)

    def test_ordered_direct_gates_allow_out_of_order_event_completion(self):
        result = self.invoke([
            direct_row(event_row(0, 0, .2, .3, .5, .6, 2.2)),
            direct_row(event_row(1, .1, .2, .7, .8, .9, 1.0)),
        ])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_helper_service_outside_parent_call_is_rejected(self):
        row = gpu_row(event_row(0, 0, .2, .3, .7, .8, .9))
        row["gpu_service_start_offset_s"] = .1
        result = self.invoke([row])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("helper service", result.stderr)

    def test_incorrect_primary_photon_count_is_rejected(self):
        row = event_row(0, 0, 0.3, 0.4, 0.6, 0.7, 0.8)
        row["num_photons"] = 0
        result = self.invoke([row])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected 100 photons", result.stderr)

    def test_gpu_backend_must_be_known_and_match_manifest(self):
        for backend in ('direct', 'unknown'):
            row = gpu_row(event_row(0, 0, .2, .3, .7, .8, .9))
            row['gpu_dispatch'] = backend
            with self.subTest(backend=backend):
                result = self.invoke([row], gpu_dispatch='pool')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('GPU backend', result.stderr)

    def test_direct_service_matches_parent_boundaries(self):
        row = gpu_row(event_row(0, 0, .2, .3, .7, .8, .9))
        row.update(gpu_dispatch='direct', gpu_service_end_offset_s=.7, gpu_cleanup_end_offset_s=.8)
        self.assertEqual(self.invoke([row]).returncode, 0)
        for key in ('gpu_service_start_offset_s', 'gpu_service_end_offset_s', 'gpu_cleanup_end_offset_s'):
            bad = dict(row)
            bad[key] += .001 if key.endswith('start_offset_s') else -.001
            with self.subTest(boundary=key):
                result = self.invoke([bad])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('direct service', result.stderr)

    def test_zero_work_has_no_device_or_service(self):
        row = gpu_row(event_row(0, 0, .2, .3, .7, .8, .9))
        row['num_photons'] = 0
        result = self.invoke([row], expected_photons=0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('zero work', result.stderr)
        row.update(num_photons=0, gpu_device_id=-1, gpu_uuid='', gpu_service_start_offset_s=0,
                   gpu_service_end_offset_s=0, gpu_cleanup_end_offset_s=0)
        self.assertEqual(self.invoke([row], expected_photons=0).returncode, 0)
        row['gpu_cleanup_end_offset_s'] = .1
        result = self.invoke([row], expected_photons=0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('empty GPU service', result.stderr)

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
