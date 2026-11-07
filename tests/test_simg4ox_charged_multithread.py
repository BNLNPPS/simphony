#!/usr/bin/env python3
"""Exercise charged MT collection, timing, and deterministic event isolation."""

import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import sys

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("case", choices=("cerenkov", "scintillation", "zero"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    particle = "geantino" if args.case == "zero" else "e-"
    scintillation = args.case == "scintillation"
    geometry = repo / "tests/geom" / (
        "opticks_raindrop_with_scintillation.gdml" if scintillation else "pfrich_min_FINAL.gdml"
    )
    baseline = None
    for threads in (2, 4):
        output = Path.cwd() / f"{args.case}-t{threads}"
        output.mkdir(exist_ok=True)
        config_dir = output / "config"
        config_dir.mkdir(exist_ok=True)
        config = json.loads((repo / "config/dev_timing.json").read_text())
        config["torch"].update(pos=[0, 0, 0], mom=[0, 0.2, 0.8])
        config["event"].update(event_mode="Minimal", output_dir=str(output))
        (config_dir / "charged.json").write_text(json.dumps(config))
        macro = output / "run.mac"
        # Repeating a run must reset event-local buffers, GPU ordering, and timing.
        macro.write_text("/run/verbose 0\n/run/initialize\n/run/eventModulo 1\n"
                         "/run/beamOn 12\n/run/beamOn 12\n")
        env = dict(os.environ, OPTICKS_HOME=str(repo), SIMPHONY_CONFIG_DIR=str(config_dir))
        command = [str(args.binary.resolve()), "--gdml", str(geometry), "--config", "charged",
                   "--macro", str(macro), "--threads", str(threads), "--particle", particle,
                   "--momentum-gev-c", "0.0014219697254161214" if scintillation else "20",
                   "--multiplicity", "2" if scintillation else "10", "--seed", "42"]
        with (output / "stdout.log").open("w") as log:
            result = subprocess.run(command, cwd=output, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=90)
        if result.returncode:
            raise AssertionError(f"Charged {threads}-worker run failed:\n"
                                 + (output / "stdout.log").read_text()[-5000:])
        validation = [sys.executable, str(repo / "tests/check_simg4ox_timing.py"),
                      str(output / "events.csv"), "--events", "12", "--threads", str(threads),
                      "--particle", particle]
        if args.case != "zero":
            validation.append("--require-photons")
        subprocess.run(validation, check=True)
        with (output / "events.csv").open() as stream:
            rows = list(csv.DictReader(stream))
        counts = [tuple(int(row[key]) for key in
                        ("num_gensteps", "num_photons", "num_gpu_hits", "num_g4_hits"))
                  for row in rows]
        assert all(count[3] == 0 for count in counts), "Optical secondaries reached the CPU stack"
        if args.case == "zero":
            assert all(count == (0, 0, 0, 0) for count in counts), "Zero-work event leaked gensteps"
        else:
            assert all(count[0] > 0 and count[1] > 0 for count in counts)
        # The timing validator checks configured threads and worker IDs. Geant4
        # need not assign an event to every worker in a short, zero-work run.
        hits = np.load(output / "s_hits.npy")
        assert hits.shape == (sum(count[2] for count in counts), 4, 4)
        assert np.load(output / "g_hits.npy").shape == (0, 4, 4)
        if baseline is None:
            baseline = counts, hits.view(np.uint32)
        else:
            assert counts == baseline[0], "Event counts changed with MT worker count"
            assert np.array_equal(hits.view(np.uint32), baseline[1]), "GPU event inputs were mixed"
    print(f"Charged MT {args.case}: repeat-run counts, hit arrays, and timing passed")


if __name__ == "__main__":
    main()
