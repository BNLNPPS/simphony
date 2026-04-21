import argparse
import subprocess
import re
import os
from pathlib import Path

run_mac_template = """
/run/numberOfThreads {threads}
/run/verbose 1
/process/optical/cerenkov/setStackPhotons {flag}
/run/initialize
/run/beamOn 500
"""

os.environ["OPTICKS_EVENT_MODE"] = "Minimal"

def get_opticks_home():
    """Get OPTICKS_HOME from environment, warn if not set."""
    opticks_home = os.environ.get("OPTICKS_HOME")
    if opticks_home is None:
        print("Warning: $OPTICKS_HOME is not defined, so this script should be called from the simphony directory.")
        return Path(".")
    return Path(opticks_home)

def parse_real_time(time_str):
    # Parses 'real\t0m41.149s' to seconds
    match = re.search(r'real\s+(\d+)m([\d.]+)s', time_str)
    if match:
        minutes = int(match.group(1))
        seconds = float(match.group(2))
        return minutes * 60 + seconds
    return None

def parse_sim_time(output):
    match = re.search(r"Simulation time:\s*([\d.]+)\s*seconds", output)
    if match:
        return float(match.group(1))
    return None

def main():
    opticks_home = get_opticks_home()
    
    parser = argparse.ArgumentParser()
    parser.add_argument('-g', '--gdml', type=Path, default=None, help="Path to a custom GDML geometry file (relative to current directory)")
    parser.add_argument('-o', '--outpath', type=Path, default=Path('./'), help="Path where the output file will be saved")
    args = parser.parse_args()

    # If gdml not provided, use default path relative to OPTICKS_HOME
    if args.gdml is None:
        gdml_path = opticks_home / 'tests/geom/opticks_raindrop.gdml'
    else:
        # User-provided path is used as-is (relative to current directory or absolute)
        gdml_path = args.gdml

    # run.mac is created in OPTICKS_HOME
    run_mac_path = opticks_home / "run.mac"

    geant_file = args.outpath / "timing_geant.txt"
    optix_file = args.outpath / "timing_optix.txt"
    log_file = args.outpath / "g4logs.txt"

    with open(geant_file, "w") as gfile, open(optix_file, "w") as ofile, open(log_file, "w") as logfile:
        for threads in range(50, 0, -1):
            times = {}
            sim_time_true = None
            for flag in ['true', 'false']:
                # Write run.mac with current flag
                with open(run_mac_path, "w") as rm:
                    rm.write(run_mac_template.format(threads=threads, flag=flag))

                # Run with time in bash to capture real/user/sys
                cmd = f"time GPUCerenkov -g {gdml_path} -m {run_mac_path}"
                print(f"Running {threads} threads: {cmd}")
                result = subprocess.run(
                    ["bash", "-c", cmd],
                    capture_output=True, text=True
                )
                stdout = result.stdout
                stderr = result.stderr
                # Save full output to log file
                logfile.write(f"\n{'='*60}\n")
                logfile.write(f"Threads: {threads}, StackPhotons: {flag}\n")
                logfile.write(f"{'='*60}\n")
                logfile.write(f"--- STDOUT ---\n{stdout}\n")
                logfile.write(f"--- STDERR ---\n{stderr}\n")
                logfile.flush()

                # Save simulation time for true run only
                if flag == 'true':
                    sim_time_true = parse_sim_time(stdout + stderr)
                    if sim_time_true is not None:
                        ofile.write(f"{threads} {sim_time_true}\n")
                        ofile.flush()

                # Extract real time
                real_match = re.search(r"real\s+\d+m[\d.]+s", stderr)
                if real_match:
                    real_sec = parse_real_time(real_match.group())
                    times[flag] = real_sec
                else:
                    print(f"[!] Could not find 'real' time for threads={threads} flag={flag}")

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
