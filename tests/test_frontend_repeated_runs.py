"""Exercise run-end event cleanup after hit output succeeds or cannot open."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    binary, geometry, config_dir, mode = sys.argv[1:]
    env = os.environ.copy()
    env["G4FORCENUMBEROFTHREADS"] = "1"
    env["SIMPHONY_CONFIG_DIR"] = config_dir

    with tempfile.TemporaryDirectory(dir=Path.cwd()) as directory:
        work = Path(directory)
        macro = work / "two_runs.mac"
        macro.write_text(
            "/run/initialize\n"
            "/process/optical/scintillation/setStackPhotons false\n"
            "/process/optical/cerenkov/setStackPhotons false\n"
            "/run/beamOn 1\n/run/beamOn 1\n"
        )
        output = work / "opticks_hits_output.txt"
        if mode == "output_failure":
            output.mkdir()
        result = subprocess.run(
            [binary, "-g", geometry, "-c", "dev", "-m", str(macro)],
            cwd=work, env=env, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=90,
        )
        Path("run.log").write_text(result.stdout)
        assert result.returncode == 0, result.stdout
        counts = re.findall(r"Opticks: NumHits:\s+(\d+)", result.stdout)
        assert len(counts) == 2 and all(int(n) > 0 for n in counts), result.stdout
        if mode == "output_failure":
            assert result.stdout.count("Error opening output file!") == 2, result.stdout
        else:
            assert output.is_file() and output.stat().st_size > 0
        print(f"{Path(binary).name}: two runs passed ({mode}), hits={counts}")


if __name__ == "__main__":
    main()
