#!/usr/bin/env python3
"""
run_genstep_comparison.py
==========================

Runs GPU (simg4ox) and G4 (G4ValidationGenstep) simulations with the same
electron primary, then compares the optical photon hit distributions.

Usage:
    python run_genstep_comparison.py [--gdml det.gdml] [--energy 1.0] [--nevents 10] [--seed 42]
"""
import os
import sys
import subprocess
import argparse
import numpy as np
from pathlib import Path

def find_gpu_hits():
    """Find the most recent GPU hit.npy output."""
    base = Path(f"/tmp/{os.environ.get('USER','MISSING_USER')}/opticks")
    candidates = sorted(base.rglob("hit.npy"), key=lambda p: p.stat().st_mtime, reverse=True)
    return str(candidates[0]) if candidates else None

def run_g4(gdml, energy, nevents, seed, pos, direction):
    """Run pure G4 simulation with electron primary."""
    cmd = [
        "G4ValidationGenstep",
        "-g", gdml,
        "-e", str(energy),
        "-n", str(nevents),
        "-s", str(seed),
        "--pos", pos,
        "--dir", direction,
    ]
    print(f"=== Running G4: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)

    # Extract hit count from output
    g4_hits = 0
    for line in result.stdout.split('\n'):
        if "Total hits:" in line:
            g4_hits = int(line.split("Total hits:")[-1].strip())

    print(f"G4: {g4_hits} hits")
    if result.returncode != 0:
        print(f"G4 STDERR (last 5 lines):")
        for line in result.stderr.strip().split('\n')[-5:]:
            print(f"  {line}")
    return g4_hits

def run_gpu(gdml, config, macro, seed):
    """Run GPU simulation via simg4ox."""
    env = os.environ.copy()
    env["OPTICKS_INTEGRATION_MODE"] = "1"  # Minimal mode: G4 tracks electron, GPU propagates optical

    cmd = [
        "simg4ox",
        "-g", gdml,
        "-c", config,
        "-m", macro,
    ]
    print(f"\n=== Running GPU: {' '.join(cmd)}")
    print(f"    OPTICKS_INTEGRATION_MODE=1 (Minimal)")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=600, env=env)

    if result.returncode != 0:
        print(f"GPU STDERR (last 10 lines):")
        for line in result.stderr.strip().split('\n')[-10:]:
            print(f"  {line}")
        return 0

    # Find hit output
    hit_path = find_gpu_hits()
    if hit_path and os.path.exists(hit_path):
        hits = np.load(hit_path)
        print(f"GPU: {len(hits)} hits (from {hit_path})")
        return len(hits)
    else:
        print("GPU: no hit.npy found")
        return 0

def compare_hits(g4_path, gpu_path):
    """Compare G4 and GPU hit arrays."""
    if not os.path.exists(g4_path):
        print(f"G4 hits not found: {g4_path}")
        return
    if not gpu_path or not os.path.exists(gpu_path):
        print(f"GPU hits not found")
        return

    g4 = np.load(g4_path)
    gpu = np.load(gpu_path)

    print(f"\n{'='*60}")
    print(f"HIT COMPARISON")
    print(f"{'='*60}")
    print(f"  G4 hits:  {len(g4)}")
    print(f"  GPU hits: {len(gpu)}")

    if len(g4) > 0 and len(gpu) > 0:
        diff = len(gpu) - len(g4)
        pct = 100 * diff / len(g4) if len(g4) > 0 else 0
        sign = "+" if diff > 0 else ""
        print(f"  Diff:     {sign}{diff} ({sign}{pct:.1f}%)")

    # Position distributions
    if len(g4) > 0:
        g4_pos = g4[:, 0, :3]
        print(f"\n  G4 hit positions:")
        print(f"    x: [{g4_pos[:,0].min():.1f}, {g4_pos[:,0].max():.1f}] mm")
        print(f"    y: [{g4_pos[:,1].min():.1f}, {g4_pos[:,1].max():.1f}] mm")
        print(f"    z: [{g4_pos[:,2].min():.1f}, {g4_pos[:,2].max():.1f}] mm")

    if len(gpu) > 0:
        gpu_pos = gpu[:, 0, :3]
        print(f"\n  GPU hit positions:")
        print(f"    x: [{gpu_pos[:,0].min():.1f}, {gpu_pos[:,0].max():.1f}] mm")
        print(f"    y: [{gpu_pos[:,1].min():.1f}, {gpu_pos[:,1].max():.1f}] mm")
        print(f"    z: [{gpu_pos[:,2].min():.1f}, {gpu_pos[:,2].max():.1f}] mm")

    # Wavelength distributions
    if len(g4) > 0:
        g4_wl = g4[:, 2, 3]
        print(f"\n  G4 wavelength:  mean={g4_wl.mean():.1f} std={g4_wl.std():.1f} nm")
    if len(gpu) > 0:
        gpu_wl = gpu[:, 2, 3]
        print(f"  GPU wavelength: mean={gpu_wl.mean():.1f} std={gpu_wl.std():.1f} nm")

    # Time distributions
    if len(g4) > 0:
        g4_t = g4[:, 0, 3]
        print(f"\n  G4 time:  mean={g4_t.mean():.2f} max={g4_t.max():.2f} ns")
    if len(gpu) > 0:
        gpu_t = gpu[:, 0, 3]
        print(f"  GPU time: mean={gpu_t.mean():.2f} max={gpu_t.max():.2f} ns")


def main():
    parser = argparse.ArgumentParser(description="Compare GPU vs G4 electron genstep simulation")
    parser.add_argument("--gdml", default="det.gdml", help="GDML geometry file")
    parser.add_argument("--energy", type=float, default=1.0, help="Electron energy in MeV")
    parser.add_argument("--nevents", type=int, default=10, help="Number of events")
    parser.add_argument("--seed", type=int, default=42, help="Random seed")
    parser.add_argument("--pos", default="0,0,100", help="Electron position x,y,z mm")
    parser.add_argument("--dir", default="0,0,1", help="Electron direction x,y,z")
    args = parser.parse_args()

    # Run G4
    g4_hits = run_g4(args.gdml, args.energy, args.nevents, args.seed, args.pos, args.dir)

    # Compare
    g4_path = "g4_genstep_hits.npy"
    gpu_path = find_gpu_hits()

    if os.path.exists(g4_path):
        g4 = np.load(g4_path)
        print(f"\n{'='*60}")
        print(f"G4 RESULTS ({args.nevents} events, {args.energy} MeV electron)")
        print(f"{'='*60}")
        print(f"  Total hits: {len(g4)}")
        print(f"  Hits/event: {len(g4)/args.nevents:.1f}")
        if len(g4) > 0:
            g4_wl = g4[:, 2, 3]
            g4_pos = g4[:, 0, :3]
            print(f"  Wavelength: mean={g4_wl.mean():.1f} nm")
            print(f"  Hit y range: [{g4_pos[:,1].min():.1f}, {g4_pos[:,1].max():.1f}] mm")


if __name__ == "__main__":
    main()
