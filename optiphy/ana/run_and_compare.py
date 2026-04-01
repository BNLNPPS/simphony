#!/usr/bin/env python3
"""Run GPU and G4 simulations and compare hit distributions.

Runs GPURaytrace with a given GDML and config, then plots:
  1. Hit count with sqrt(N) error bars
  2. WLS-shifted wavelength distribution
  3. Full wavelength distribution
  4. Arrival time (bulk, truncated)
  5. Arrival time (full range, no overflow)
  6. 3D hit position scatter for GPU and G4

Usage:
    python optiphy/ana/run_and_compare.py -g det.gdml -s 42 [--outdir plots]

    # Skip simulation, use existing .npy files:
    python optiphy/ana/run_and_compare.py --gpu-hits gpu_hits.npy --g4-hits g4_hits.npy
"""
import argparse
import os
import subprocess
import sys
import math

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D


def run_simulation(gdml, config, macro, seed):
    """Run GPURaytrace and return (gpu_hits_path, g4_hits_path, gpu_nhits, g4_nhits)."""
    cmd = ["/opt/eic-opticks/bin/GPURaytrace",
           "-g", gdml, "-m", macro, "-s", str(seed)]
    if config:
        cmd += ["-c", config]

    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    output = result.stdout + result.stderr

    gpu_nhits = g4_nhits = 0
    for line in output.split('\n'):
        if 'Opticks: NumHits:' in line:
            gpu_nhits = int(line.strip().split()[-1])
        if 'Geant4: NumHits:' in line:
            g4_nhits = int(line.strip().split()[-1])

    print(f"  GPU: {gpu_nhits} hits, G4: {g4_nhits} hits")
    return "gpu_hits.npy", "g4_hits.npy", gpu_nhits, g4_nhits


def load_hits(path, expected_cols=None):
    """Load hit array and reshape to (N, ?, 4)."""
    a = np.load(path)
    if a.ndim == 2:
        ncols = a.shape[1] // 4
        a = a.reshape(-1, ncols, 4)
    return a


def plot_with_errors(ax, data1, data2, bins, label1, label2, xlabel):
    """Plot two histograms as points with sqrt(N) error bars."""
    h1, edges = np.histogram(data1, bins=bins)
    h2, _ = np.histogram(data2, bins=bins)
    centers = (edges[:-1] + edges[1:]) / 2
    width = (edges[1] - edges[0]) * 0.35
    ax.errorbar(centers - width / 2, h1, yerr=np.sqrt(np.maximum(h1, 1)),
                fmt='o', color='dodgerblue', markersize=4, capsize=2,
                linewidth=1, label=label1)
    ax.errorbar(centers + width / 2, h2, yerr=np.sqrt(np.maximum(h2, 1)),
                fmt='s', color='orangered', markersize=4, capsize=2,
                linewidth=1, label=label2)
    ax.set_xlabel(xlabel)
    ax.set_ylabel('Counts')
    ax.legend()


def make_plots(gpu, g4, outdir, title_extra=""):
    os.makedirs(outdir, exist_ok=True)

    gpu_wl = gpu[:, 2, 3]
    g4_wl = g4[:, 2, 3]
    gpu_t = gpu[:, 0, 3]
    g4_t = g4[:, 0, 3]
    gpu_pos = gpu[:, 0, :3]
    g4_pos = g4[:, 0, :3]

    diff = 100 * (len(gpu) / len(g4) - 1) if len(g4) > 0 else 0
    z_score = (len(gpu) - len(g4)) / math.sqrt(len(gpu) + len(g4)) if (len(gpu) + len(g4)) > 0 else 0
    header = f"GPU={len(gpu)} G4={len(g4)} ({diff:+.1f}%, {z_score:+.1f}σ)"
    if title_extra:
        header = f"{title_extra}\n{header}"

    # 1. Hit count
    fig, ax = plt.subplots(figsize=(6, 5))
    vals = [len(gpu), len(g4)]
    errs = [math.sqrt(v) for v in vals]
    ax.errorbar([0], [vals[0]], yerr=[errs[0]], fmt='o', markersize=12,
                capsize=8, linewidth=2, color='dodgerblue', label='GPU')
    ax.errorbar([1], [vals[1]], yerr=[errs[1]], fmt='s', markersize=12,
                capsize=8, linewidth=2, color='orangered', label='G4')
    ax.set_xticks([0, 1])
    ax.set_xticklabels(['GPU', 'G4'])
    ax.set_ylabel('Hits')
    ax.set_title(f'Hit Count\n{header}')
    ax.set_xlim(-0.5, 1.5)
    ax.legend()
    for i, (v, e) in enumerate(zip(vals, errs)):
        ax.text(i + 0.15, v, f'{v}±{e:.0f}', va='center', fontsize=10)
    plt.tight_layout()
    plt.savefig(f'{outdir}/hits.png', dpi=150)
    plt.close()

    # 2. WLS-shifted wavelength
    fig, ax = plt.subplots(figsize=(8, 5))
    gpu_s = gpu_wl[gpu_wl > 380]
    g4_s = g4_wl[g4_wl > 380]
    plot_with_errors(ax, gpu_s, g4_s, np.arange(380, 550, 15),
                     f'GPU ({len(gpu_s)})', f'G4 ({len(g4_s)})',
                     'Wavelength (nm)')
    ax.set_title(f'WLS-shifted Wavelength (>380nm)\n{header}')
    plt.tight_layout()
    plt.savefig(f'{outdir}/wavelength_shifted.png', dpi=150)
    plt.close()

    # 3. Full wavelength
    fig, ax = plt.subplots(figsize=(8, 5))
    plot_with_errors(ax, gpu_wl, g4_wl, np.arange(330, 550, 15),
                     f'GPU ({len(gpu)})', f'G4 ({len(g4)})',
                     'Wavelength (nm)')
    ax.set_title(f'Full Wavelength\n{header}')
    plt.tight_layout()
    plt.savefig(f'{outdir}/wavelength_full.png', dpi=150)
    plt.close()

    # 4. Arrival time (bulk, truncated at 99th percentile)
    fig, ax = plt.subplots(figsize=(8, 5))
    t_cut = max(np.percentile(gpu_t, 99), np.percentile(g4_t, 99))
    t_bins = np.linspace(0, t_cut, 30)
    gpu_over = (gpu_t > t_cut).sum()
    g4_over = (g4_t > t_cut).sum()
    plot_with_errors(ax, gpu_t[gpu_t <= t_cut], g4_t[g4_t <= t_cut], t_bins,
                     f'GPU (overflow={gpu_over})', f'G4 (overflow={g4_over})',
                     'Time (ns)')
    ax.set_title(f'Arrival Time (t < {t_cut:.0f}ns)\n{header}')
    plt.tight_layout()
    plt.savefig(f'{outdir}/time_bulk.png', dpi=150)
    plt.close()

    # 5. Arrival time (full range, no overflow)
    fig, ax = plt.subplots(figsize=(8, 5))
    t_max = max(gpu_t.max(), g4_t.max()) * 1.05
    t_bins_full = np.linspace(0, t_max, 50)
    plot_with_errors(ax, gpu_t, g4_t, t_bins_full,
                     f'GPU ({len(gpu)})', f'G4 ({len(g4)})',
                     'Time (ns)')
    ax.set_title(f'Arrival Time (full range)\n{header}')
    plt.tight_layout()
    plt.savefig(f'{outdir}/time_full.png', dpi=150)
    plt.close()

    # 6. 3D hit positions
    fig = plt.figure(figsize=(14, 6))

    ax1 = fig.add_subplot(121, projection='3d')
    ax1.scatter(gpu_pos[:, 0], gpu_pos[:, 1], gpu_pos[:, 2],
                c='dodgerblue', s=3, alpha=0.5)
    ax1.set_xlabel('X (mm)')
    ax1.set_ylabel('Y (mm)')
    ax1.set_zlabel('Z (mm)')
    ax1.set_title(f'GPU hit positions ({len(gpu)})')

    ax2 = fig.add_subplot(122, projection='3d')
    ax2.scatter(g4_pos[:, 0], g4_pos[:, 1], g4_pos[:, 2],
                c='orangered', s=3, alpha=0.5)
    ax2.set_xlabel('X (mm)')
    ax2.set_ylabel('Y (mm)')
    ax2.set_zlabel('Z (mm)')
    ax2.set_title(f'G4 hit positions ({len(g4)})')

    plt.suptitle(f'3D Hit Positions\n{header}')
    plt.tight_layout()
    plt.savefig(f'{outdir}/positions_3d.png', dpi=150)
    plt.close()

    # Print summary
    print(f"\nSummary: {header}")
    print(f"  Wavelength: GPU mean={gpu_wl.mean():.1f}nm  G4 mean={g4_wl.mean():.1f}nm")
    print(f"  Time:       GPU mean={gpu_t.mean():.2f}ns  G4 mean={g4_t.mean():.2f}ns")
    print(f"  WLS shifted: GPU {100*(gpu_wl>380).mean():.1f}%  G4 {100*(g4_wl>380).mean():.1f}%")
    print(f"\nPlots saved to {outdir}/:")
    for f in ['hits.png', 'wavelength_shifted.png', 'wavelength_full.png',
              'time_bulk.png', 'time_full.png', 'positions_3d.png']:
        print(f"  {f}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-g", "--gdml", default="det.gdml", help="GDML geometry file")
    parser.add_argument("-c", "--config", default=None, help="Config name (e.g. det_debug)")
    parser.add_argument("-m", "--macro", default="tests/run_genstep.mac", help="G4 macro file")
    parser.add_argument("-s", "--seed", type=int, default=42, help="Random seed")
    parser.add_argument("--outdir", default="plots", help="Output directory for plots")
    parser.add_argument("--title", default="", help="Extra title text")
    parser.add_argument("--gpu-hits", default=None, help="Skip sim, use existing GPU hits .npy")
    parser.add_argument("--g4-hits", default=None, help="Skip sim, use existing G4 hits .npy")
    args = parser.parse_args()

    if args.gpu_hits and args.g4_hits:
        print(f"Using existing files: {args.gpu_hits}, {args.g4_hits}")
    else:
        run_simulation(args.gdml, args.config, args.macro, args.seed)
        args.gpu_hits = "gpu_hits.npy"
        args.g4_hits = "g4_hits.npy"

    gpu = load_hits(args.gpu_hits)
    g4 = load_hits(args.g4_hits)

    # Normalize to (N, 4, 4) — take first 4 rows if more
    if gpu.shape[1] > 4:
        gpu = gpu[:, :4, :]
    if g4.shape[1] > 4:
        g4 = g4[:, :4, :]

    make_plots(gpu, g4, args.outdir, title_extra=args.title)


if __name__ == "__main__":
    main()
