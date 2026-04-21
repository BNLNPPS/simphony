#!/usr/bin/env python3
"""
Benchmark: GPU speedup for optical photon simulation on the EPIC dRICH detector.

Runs three configurations with the full dRICH geometry (100 pi+ at 10 GeV):

  cpu      -- Optical photons generated AND tracked on CPU by Geant4
  baseline -- Optical photons generated but NOT tracked (SetStackPhotons=false)
  gpu      -- Optical photons simulated on GPU via simphony

Speedup = CPU_optical_time / GPU_simulate_time
  where CPU_optical_time = T(cpu) - T(baseline)
  and   GPU_simulate_time = wall time of G4CXOpticks::simulate()

Prerequisites:
  - Spack environment activated with DD4hep, Geant4, simphony
  - `spack load epic` (sets DETECTOR_PATH to EPIC geometry)
  - DD4HEP_LIBRARY_PATH includes /opt/local/lib (eic-opticks plugins)

Usage:
  python3 benchmark_drich.py                                # all modes, 10 events
  python3 benchmark_drich.py --events 100 --photon-threshold 5000000
  python3 benchmark_drich.py --mode gpu --events 10
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys
import time


_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def find_compact_file(geometry="1sector"):
    """Locate the dRICH compact XML.

    geometry: '1sector' (default) uses the bundled single-sector XML,
              'full' uses EPIC's epic_drich_only.xml (all 6 sectors).
    """
    if geometry == "1sector":
        compact = os.path.join(_SCRIPT_DIR, "geometry", "epic_drich_1sector.xml")
        os.environ["DRICH_1SECTOR_XML"] = os.path.join(
            _SCRIPT_DIR, "geometry", "drich_1sector.xml")
    else:
        detector_path = os.environ.get("DETECTOR_PATH", "")
        if not detector_path:
            print("ERROR: DETECTOR_PATH not set. Run: spack load epic",
                  file=sys.stderr)
            sys.exit(1)
        compact = os.path.join(detector_path, "epic_drich_only.xml")

    if not os.path.exists(compact):
        print(f"ERROR: {compact} not found", file=sys.stderr)
        sys.exit(1)

    return compact


def run_single_mode(mode, num_events, photon_threshold=0, multiplicity=100,
                    geometry="1sector"):
    """Run one benchmark mode inside the current process."""
    import cppyy
    import DDG4
    from g4units import GeV

    cppyy.include("G4OpticalParameters.hh")
    from cppyy.gbl import G4OpticalParameters

    compact = find_compact_file(geometry)

    # ---- kernel & geometry ------------------------------------------------
    kernel = DDG4.Kernel()
    print(f"Loading geometry: {compact}")
    kernel.loadGeometry(str("file:" + compact))

    geant4 = DDG4.Geant4(kernel)
    geant4.printDetectors()
    geant4.setupUI(typ="tcsh", vis=False, ui=False)

    # ---- particle gun: pi+ at 10 GeV toward dRICH sector 0 ---------------
    # dRICH acceptance: eta 1.6-3.5 => theta 3.5-22.8 deg
    # ideal theta: eta=2.0 => ~15.4 deg (full rings in one sector)
    # 1-sector geometry: sector 0 sensors are at phi ~ -167 deg
    # Full geometry: sector 0 at same phi; other sectors fill the ring
    eta = 2.0
    theta = 2.0 * math.atan(math.exp(-eta))  # ~15.4 deg
    phi = math.radians(-167.0) if geometry == "1sector" else 0.0
    geant4.setupGun(
        "Gun",
        particle="pi+",
        energy=10 * GeV,
        position=(0, 0, 0),
        isotrop=False,
        direction=(
            math.sin(theta) * math.cos(phi),
            math.sin(theta) * math.sin(phi),
            math.cos(theta),
        ),
        multiplicity=multiplicity,
    )

    # ---- physics ----------------------------------------------------------
    geant4.setupPhysics("QGSP_BERT")
    ph = DDG4.PhysicsList(kernel, "Geant4PhysicsList/OpticalPhys")
    ph.addPhysicsConstructor(str("G4OpticalPhysics"))
    kernel.physicsList().adopt(ph)

    # ---- mode-specific setup ----------------------------------------------
    if mode == "cpu":
        # Track optical photons on CPU with proper optical tracker
        seq, act = geant4.setupDetector("DRICH", "Geant4OpticalTrackerAction")
        filt = DDG4.Filter(
            kernel, "ParticleSelectFilter/OpticalPhotonSelector"
        )
        filt.particle = "opticalphoton"
        seq.adopt(filt)

    elif mode == "baseline":
        # Need a tracker so DD4hep is happy, but no photons will arrive
        geant4.setupTracker("DRICH")

    elif mode == "gpu":
        # Opticks env vars for hit gathering
        os.environ.setdefault("OPTICKS_EVENT_MODE", "Minimal")
        os.environ.setdefault("OPTICKS_INTEGRATION_MODE", "1")

        # Block CPU G4Step hits; only GPU-injected hits pass
        seq, act = geant4.setupTracker("DRICH")
        filt = DDG4.Filter(kernel, "EnergyDepositMinimumCut/Block")
        filt.Cut = 1e12
        seq.adopt(filt)

        # eic-opticks DDG4 plugins
        stepping = DDG4.SteppingAction(
            kernel, "OpticsSteppingAction/OpticsStep1"
        )
        stepping.Verbose = 0
        kernel.steppingAction().adopt(stepping)

        run_action = DDG4.RunAction(kernel, "OpticsRun/OpticsRun1")
        run_action.SaveGeometry = False
        kernel.runAction().adopt(run_action)

        evt_action = DDG4.EventAction(kernel, "OpticsEvent/OpticsEvt1")
        evt_action.Verbose = 1
        if photon_threshold > 0:
            evt_action.PhotonThreshold = photon_threshold
        kernel.eventAction().adopt(evt_action)

    # ---- configure & initialize -------------------------------------------
    kernel.NumEvents = num_events
    kernel.configure()

    # Disable photon stacking BEFORE initialize so G4Cerenkov reads it
    if mode in ("baseline", "gpu"):
        G4OpticalParameters.Instance().SetCerenkovStackPhotons(False)
        G4OpticalParameters.Instance().SetScintStackPhotons(False)

    kernel.initialize()

    # BoundaryInvokeSD AFTER initialize (runtime parameter)
    if mode == "cpu":
        G4OpticalParameters.Instance().SetBoundaryInvokeSD(True)

    # ---- run & time -------------------------------------------------------
    t0 = time.perf_counter()
    kernel.run()
    t1 = time.perf_counter()
    wall_s = t1 - t0

    kernel.terminate()

    result = {
        "mode": mode,
        "events": num_events,
        "wall_s": round(wall_s, 4),
        "per_event_ms": round(wall_s / num_events * 1000, 2),
        "photon_threshold": photon_threshold,
        "multiplicity": multiplicity,
        "geometry": geometry,
    }
    print(f"BENCHMARK_RESULT:{json.dumps(result)}", flush=True)
    return result


# ---------------------------------------------------------------------------
# "all" mode — runs each config as a subprocess, then computes speedup
# ---------------------------------------------------------------------------

def _run_subprocess(mode, num_events, photon_threshold=0, multiplicity=100,
                    geometry="1sector"):
    """Run a single mode in a child process, return (result_dict, raw_output)."""
    script = os.path.abspath(__file__)
    cmd = [
        sys.executable, script,
        "--mode", mode,
        "--events", str(num_events),
        "--photon-threshold", str(photon_threshold),
        "--multiplicity", str(multiplicity),
        "--geometry", geometry,
    ]
    proc = subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    output = proc.stdout

    if proc.returncode != 0:
        print(f"ERROR: subprocess exited with code {proc.returncode}",
              file=sys.stderr)
        return None, output

    result = None
    for line in output.splitlines():
        if line.startswith("BENCHMARK_RESULT:"):
            result = json.loads(line[len("BENCHMARK_RESULT:"):])
            break

    return result, output


def _parse_gpu_times(output):
    """Extract per-event GPU simulate() times from C++ info output."""
    times = []
    for line in output.splitlines():
        m = re.search(r"OPTICKS_GPU_TIME event=(\d+) ms=([\d.]+)", line)
        if m:
            times.append((int(m.group(1)), float(m.group(2))))
    return times


def _parse_gpu_photons(output):
    """Extract total photon count from GPU output."""
    total = 0
    for line in output.splitlines():
        m = re.search(r"OPTICKS_GPU_TIME.*photons=(\d+)", line)
        if m:
            total += int(m.group(1))
    return total


def run_all(num_events, photon_threshold=0, multiplicity=100, geometry="1sector"):
    """Run all three modes and print speedup analysis."""
    results = {}
    gpu_event_times = []
    gpu_total_photons = 0

    for mode in ("baseline", "cpu", "gpu"):
        pt = photon_threshold if mode == "gpu" else 0
        label = {
            "baseline": "baseline (no photon tracking)",
            "cpu": "cpu (photons tracked on CPU)",
            "gpu": "gpu (photons on GPU via simphony)",
        }[mode]
        extra = f", threshold={pt}" if pt > 0 else ""
        print(f"\n{'='*60}")
        print(f"  {label}  --  {num_events} events{extra}")
        phi_str = "phi=-167 (sector 0)" if geometry == "1sector" else "phi=0"
        print(f"  {multiplicity} pi+ at 10 GeV, eta=2.0 (theta~15.4deg), {phi_str}")
        print(f"{'='*60}")

        result, output = _run_subprocess(mode, num_events, pt, multiplicity,
                                         geometry)

        if result is None:
            print(f"ERROR: mode={mode} failed. Output:\n{output[-3000:]}")
            sys.exit(1)

        results[mode] = result

        # Show selected output lines
        for line in output.splitlines():
            if line.startswith("BENCHMARK_RESULT:"):
                continue
            if any(kw in line for kw in (
                "OPTICKS_GPU_TIME", "gensteps", "hits from GPU",
                "Detected photons",
            )):
                print(f"  {line.strip()}")

        print(f"  Wall time: {result['wall_s']:.3f} s  "
              f"({result['per_event_ms']:.1f} ms/event)")

        if mode == "gpu":
            gpu_event_times = _parse_gpu_times(output)
            gpu_total_photons = _parse_gpu_photons(output)

    # ------------------------------------------------------------------
    # Summary
    # ------------------------------------------------------------------
    T_b = results["baseline"]["wall_s"]
    T_c = results["cpu"]["wall_s"]
    T_g = results["gpu"]["wall_s"]
    cpu_optical = T_c - T_b
    gpu_overhead = T_g - T_b

    photons_per_event = gpu_total_photons / num_events if num_events > 0 else 0

    print(f"\n{'='*60}")
    print(f"  RESULTS  ({num_events} events, {multiplicity} pi+ @ 10 GeV in dRICH)")
    print(f"{'='*60}")
    print(f"\n  {'Mode':<12} {'Total (s)':<12} {'Per event (ms)'}")
    print(f"  {'-'*40}")
    for m in ("baseline", "cpu", "gpu"):
        r = results[m]
        print(f"  {m:<12} {r['wall_s']:<12.3f} {r['per_event_ms']:.1f}")

    print(f"\n  CPU optical photon tracking: {cpu_optical:.3f} s"
          f"  ({cpu_optical / num_events * 1000:.1f} ms/event)")
    print(f"  GPU total overhead:          {gpu_overhead:.3f} s"
          f"  ({gpu_overhead / num_events * 1000:.1f} ms/event)")

    if gpu_total_photons > 0:
        print(f"\n  Total Cerenkov photons: {gpu_total_photons:,}"
              f"  (~{photons_per_event:,.0f}/event)")

    if gpu_event_times:
        all_ms = [t for _, t in gpu_event_times]
        total_ms = sum(all_ms)
        print(f"\n  GPU simulate() calls: {len(all_ms)}")
        # Show first 5 and last 2 if many events
        show = gpu_event_times
        if len(show) > 10:
            show = gpu_event_times[:5] + [("...", None)] + gpu_event_times[-2:]
        for item in show:
            if item[1] is None:
                print(f"    ...")
            else:
                evt_id, ms = item
                tag = " (includes OptiX warmup)" if evt_id == 0 else ""
                print(f"    event {evt_id:>3d}:  {ms:>8.1f} ms{tag}")
        print(f"    {'total':>9s}:  {total_ms:>8.1f} ms")

        if len(all_ms) > 1:
            warm_ms = all_ms[1:]
            avg_warm = sum(warm_ms) / len(warm_ms)
            print(f"    avg (excl. first): {avg_warm:>5.1f} ms")

        if total_ms > 0:
            speedup = cpu_optical / (total_ms / 1000)
            print(f"\n  >>> SPEEDUP (CPU optical / GPU simulate):  {speedup:.1f}x <<<")

        if len(all_ms) > 1 and avg_warm > 0:
            speedup_warm = (cpu_optical / num_events * 1000) / avg_warm
            print(f"  >>> SPEEDUP (per-event, excl. warmup):     {speedup_warm:.1f}x <<<")

    if gpu_overhead > 0:
        speedup_total = cpu_optical / gpu_overhead
        print(f"  >>> SPEEDUP (CPU optical / GPU overhead):   {speedup_total:.1f}x <<<")

    print(f"{'='*60}\n")


# ---------------------------------------------------------------------------
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Benchmark GPU vs CPU optical photon speedup on EPIC dRICH"
    )
    parser.add_argument(
        "--mode",
        choices=["cpu", "baseline", "gpu", "all"],
        default="all",
        help="cpu=photons on CPU, baseline=no photon tracking, "
             "gpu=photons on GPU, all=run all three",
    )
    parser.add_argument(
        "--events", type=int, default=10,
        help="number of events (default: 10)",
    )
    parser.add_argument(
        "--photon-threshold", type=int, default=0,
        help="GPU batch mode: accumulate photons across events and simulate "
             "when this count is reached (default: 0 = per-event)",
    )
    parser.add_argument(
        "--multiplicity", type=int, default=100,
        help="number of pi+ per event (default: 100)",
    )
    parser.add_argument(
        "--geometry", choices=["1sector", "full"], default="1sector",
        help="1sector=single dRICH sector (default), full=all 6 sectors",
    )
    args = parser.parse_args()

    if args.mode == "all":
        run_all(args.events, args.photon_threshold, args.multiplicity,
                args.geometry)
    else:
        run_single_mode(args.mode, args.events, args.photon_threshold,
                        args.multiplicity, args.geometry)
