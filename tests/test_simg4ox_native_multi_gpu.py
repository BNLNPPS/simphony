#!/usr/bin/env python3
"""Compare asynchronous MT events with device-matched one-GPU controls."""

import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np


def simulate(binary, case, mask, output):
    output.mkdir()
    repo = Path(__file__).resolve().parents[1]
    config = json.loads((repo / "config/dev_timing.json").read_text())
    config["event"].update(event_mode="Minimal", maxslot=1_000_000, output_dir=str(output))
    if case != "torch":
        config["torch"].update(pos=[0, 0, 0], mom=[0, 0.2, 0.8])
    config["event"]["max_genstep"] = 100000
    (output / "native.json").write_text(json.dumps(config))
    scint = case == "scintillation"
    geometry = {"torch": "opticks_raindrop.gdml", "scintillation": "opticks_raindrop_with_scintillation.gdml",
                "zero": "opticks_raindrop.gdml", "cerenkov": "pfrich_min_FINAL.gdml"}[case]
    # Many short torch events keep the queue supplied, including at the Geant4 run boundary.
    events = 48 if case == "torch" else 16
    macro = ["/run/verbose 0", "/run/initialize", "/run/eventModulo 1"]
    if scint:
        macro.append("/process/inactivate msc")
    macro.append(f"/run/beamOn {events}")
    (output / "run.mac").write_text("\n".join(macro) + "\n")
    env = dict(os.environ, OPTICKS_HOME=str(repo), SIMPHONY_CONFIG_DIR=str(output),
               CUDA_VISIBLE_DEVICES=",".join(mask))
    if scint:
        env["U4__CollectGenstep_G4Cerenkov_modified_DISABLE"] = "1"
    cmd = [str(binary), "-g", str(repo / "tests/geom" / geometry), "-c", "native", "-m", "run.mac",
           "--threads", "4", "--seed", "42"]
    if case == "torch":
        cmd.append("--no-cpu-recorder")
    else:
        cmd.extend(["--particle", "geantino" if case == "zero" else "e-",
                    "--momentum-gev-c", "0.0014219697254161214" if scint else "20",
                    "--multiplicity", "2" if scint else "10"])
    proc = subprocess.run(cmd, cwd=output, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, timeout=180)
    assert proc.returncode == 0, proc.stdout[-5000:]
    manifest = json.loads((output / "events.manifest.json").read_text())
    assert manifest["num_gpus"] == len(mask) and manifest["dispatch_mode"] == "asynchronous"
    with (output / "events.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    assert [int(r["event_id"]) for r in rows] == list(range(events))
    for field, name in [("num_gpu_hits", "s_hits.npy"), ("num_g4_hits", "g_hits.npy")]:
        hits = np.load(output / name)
        assert hits.shape == (sum(int(r[field]) for r in rows), 4, 4)
        # Split the persisted file at its event boundaries to detect output reordering.
        offsets = np.cumsum([int(r[field]) for r in rows[:-1]])
        for r, event_hits in zip(rows, np.split(hits.view(np.uint32), offsets)):
            r[name] = event_hits
    if case == "zero":
        for r in rows:
            assert all(int(r[f]) == 0 for f in ("num_gensteps", "num_photons", "num_gpu_hits", "num_g4_hits"))
            assert int(r["gpu_device_id"]) == -1 and not r["gpu_uuid"]
            assert not any(r[k] for k in ("gpu_service_start_offset_s", "gpu_service_end_offset_s", "gpu_cleanup_end_offset_s"))
    else:
        assert all(int(r["num_photons"]) > 0 for r in rows)
        assert any(int(r["num_gpu_hits"]) for r in rows)
        assert {int(r["gpu_device_id"]) for r in rows} == set(range(len(mask)))
        # The producer-end stamp is taken under the queue lock on admission.
        # At most N events can wait in the queue alongside N device workers.
        changes = []
        for row in rows:
            changes.append((float(row["cpu_end_time_offset_s"]), 1))
            changes.append((float(row["end_offset_s"]), -1))
        inflight = 0
        for _, delta in sorted(changes):
            inflight += delta
            assert 0 <= inflight <= 2 * len(mask), f"{inflight} admitted events on {len(mask)} devices"
        assert inflight == 0
    return rows


def check_overlap(rows):
    def time(row, field):
        return float(row[field + "_offset_s"])

    assert any(a["gpu_uuid"] != b["gpu_uuid"] and
               max(time(a, "gpu_service_start"), time(b, "gpu_service_start")) <
               min(time(a, "gpu_service_end"), time(b, "gpu_service_end"))
               for a in rows for b in rows), "No overlapping GPU service on distinct devices"

    for worker in {r["worker_id"] for r in rows}:
        assigned = sorted((r for r in rows if r["worker_id"] == worker), key=lambda r: time(r, "start"))
        if any(time(a, "cpu_end_time") <= time(b, "start") < time(a, "gpu_service_end")
               for a, b in zip(assigned, assigned[1:])):
            return
    raise AssertionError("No Geant4 producer started its next event before its previous GPU result")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("device_probe", type=Path)
    parser.add_argument("case", choices=["torch", "cerenkov", "scintillation", "zero"])
    args = parser.parse_args()
    # The probe returns 77 on hosts with fewer than two CUDA-visible devices.
    probe = subprocess.run([str(args.device_probe.resolve())], timeout=30)
    if probe.returncode == 77:
        return 77
    probe.check_returncode()
    mask = os.environ.get("CUDA_VISIBLE_DEVICES", "0,1").split(",")[:2]
    assert len(mask) == 2
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        results = simulate(binary, args.case, mask, work / "two")
        if args.case == "zero":
            return 0
        for ordinal, token in enumerate(mask):
            reference = simulate(binary, args.case, [token], work / f"one-{ordinal}")
            for event, control in zip(results, reference):
                if int(event["gpu_device_id"]) != ordinal:
                    continue
                assert event["gpu_uuid"] == control["gpu_uuid"]
                for field in ("num_gensteps", "num_photons", "num_gpu_hits", "num_g4_hits"):
                    assert event[field] == control[field], (event["event_id"], field)
                for field in ("s_hits.npy", "g_hits.npy"):
                    np.testing.assert_array_equal(event[field], control[field], err_msg=f"Event {event['event_id']} {field}")
        if args.case == "torch":
            check_overlap(results)
    return 0


if __name__ == "__main__":
    sys.exit(main())
