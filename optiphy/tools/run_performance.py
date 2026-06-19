import argparse
import os
import re
import subprocess
import tempfile
import time
from pathlib import Path

run_mac_template = """
/run/numberOfThreads {threads}
/run/verbose 1
/process/optical/cerenkov/setStackPhotons {flag}
/run/initialize
/run/beamOn 500
"""

os.environ["OPTICKS_EVENT_MODE"] = "Minimal"
SIMPHONY_GEOM_FILE_ENV = "SIMPHONY_GEOM_FILE"

def parse_sim_time(output):
    match = re.search(r"Simulation time:\s*([\d.]+)\s*seconds", output)
    if match:
        return float(match.group(1))
    return None

def resolve_gdml_path(cli_gdml: Path | None) -> Path:
    """Resolve the GDML path from CLI or environment."""
    if cli_gdml is not None:
        return cli_gdml

    env_gdml = os.environ.get(SIMPHONY_GEOM_FILE_ENV)
    if env_gdml:
        return Path(env_gdml)

    raise ValueError(
        f"Provide --gdml or set {SIMPHONY_GEOM_FILE_ENV} to the test geometry path."
    )

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-g', '--gdml', type=Path, default=None, help=f"Path to the GDML geometry file. If omitted, uses ${SIMPHONY_GEOM_FILE_ENV}.")
    parser.add_argument('-o', '--outpath', type=Path, default=Path('./'), help="Path where the output file will be saved")
    args = parser.parse_args()

    try:
        gdml_path = resolve_gdml_path(args.gdml)
    except ValueError as err:
        parser.error(str(err))

    out_dir = args.outpath.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    geant_file = out_dir / "timing_geant.txt"
    optix_file = out_dir / "timing_optix.txt"
    log_file = out_dir / "g4logs.txt"

    with tempfile.TemporaryDirectory(prefix="simphony-run-performance-") as tmpdir:
        run_mac_path = Path(tmpdir) / "run.mac"

        with open(geant_file, "w") as gfile, open(optix_file, "w") as ofile, open(log_file, "w") as logfile:
            for threads in range(50, 0, -1):
                times = {}
                sim_time_true = None
                for flag in ['true', 'false']:
                    run_mac_path.write_text(run_mac_template.format(threads=threads, flag=flag))

                    cmd = ["GPUCerenkov", "-g", str(gdml_path), "-m", str(run_mac_path)]
                    print(f"Running {threads} threads: {' '.join(cmd)}")
                    t0 = time.perf_counter()
                    result = subprocess.run(cmd, capture_output=True, text=True)
                    elapsed_sec = time.perf_counter() - t0
                    stdout = result.stdout
                    stderr = result.stderr
                    # Save full output to log file
                    logfile.write(f"\n{'='*60}\n")
                    logfile.write(f"Threads: {threads}, StackPhotons: {flag}\n")
                    logfile.write(f"{'='*60}\n")
                    logfile.write(f"--- STDOUT ---\n{stdout}\n")
                    logfile.write(f"--- STDERR ---\n{stderr}\n")
                    logfile.flush()

                    if result.returncode != 0:
                        raise subprocess.CalledProcessError(result.returncode, cmd, output=stdout, stderr=stderr)

                    # Save simulation time for true run only
                    if flag == 'true':
                        sim_time_true = parse_sim_time(stdout + stderr)
                        if sim_time_true is not None:
                            ofile.write(f"{threads} {sim_time_true}\n")
                            ofile.flush()

                    times[flag] = elapsed_sec

                # Write the difference to timing_geant.txt (true - false)
                if 'true' in times and 'false' in times:
                    diff = times['true'] - times['false']
                    gfile.write(f"{threads} {diff}\n")
                    gfile.flush()
                else:
                    print(f"[!] Missing times for threads={threads}")

    print("Done.")

if __name__ == '__main__':
    main()
