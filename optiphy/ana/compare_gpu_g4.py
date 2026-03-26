#!/usr/bin/env python
"""
compare_gpu_g4.py : Compare GPU (opticks) vs G4 (standalone) simulation hits
=============================================================================

Reads GPU hit/photon arrays from an opticks event folder and G4 hits from
g4_hits.npy, then prints a side-by-side comparison table.

Usage::

    python ana/compare_gpu_g4.py <gpu_event_folder> <g4_hits.npy>

    # Auto-resolves A000 subfolder:
    python ana/compare_gpu_g4.py /tmp/$USER/opticks/GEOM/GEOM/GPUPhotonSourceMinimal/ALL0_no_opticks_event_name g4_hits.npy
"""
import sys
import os
import argparse
import numpy as np

FLAG_ENUM = {
    0x0004: "TORCH", 0x0008: "BULK_ABSORB", 0x0010: "BULK_REEMIT",
    0x0020: "BULK_SCATTER", 0x0040: "SURFACE_DETECT", 0x0080: "SURFACE_ABSORB",
    0x0100: "SURFACE_DREFLECT", 0x0200: "SURFACE_SREFLECT",
    0x0400: "BOUNDARY_REFLECT", 0x0800: "BOUNDARY_TRANSMIT",
    0x1000: "NAN_ABORT", 0x2000: "EFFICIENCY_COLLECT", 0x8000: "MISS",
}


def resolve_event_path(path):
    if os.path.exists(os.path.join(path, "photon.npy")):
        return path
    a000 = os.path.join(path, "A000")
    if os.path.exists(os.path.join(a000, "photon.npy")):
        return a000
    if os.path.isdir(path):
        for d in sorted(os.listdir(path)):
            dp = os.path.join(path, d)
            if os.path.isdir(dp) and os.path.exists(os.path.join(dp, "photon.npy")):
                return dp
    return path


def hit_stats(hits, label):
    """Compute statistics dict from a (N, 4, 4) hit array."""
    n = len(hits)
    if n == 0:
        return dict(label=label, n=0)
    wl = hits[:, 2, 3]
    t = hits[:, 0, 3]
    pos = hits[:, 0, :3]
    r = np.sqrt(np.sum(pos ** 2, axis=1))
    return dict(
        label=label, n=n,
        wl_min=wl.min(), wl_max=wl.max(), wl_mean=wl.mean(), wl_std=wl.std(),
        t_min=t.min(), t_max=t.max(), t_mean=t.mean(), t_std=t.std(),
        r_min=r.min(), r_max=r.max(), r_mean=r.mean(),
        x_mean=pos[:, 0].mean(), y_mean=pos[:, 1].mean(), z_mean=pos[:, 2].mean(),
    )


def print_comparison_table(gpu, g4, n_photons):
    """Print side-by-side comparison."""
    w = 14  # column width

    print("=" * 70)
    print("GPU vs G4 COMPARISON")
    print("=" * 70)

    print(f"\n  {'':30s} {'GPU':>{w}s} {'G4':>{w}s} {'Diff':>{w}s}")
    print(f"  {'-'*30} {'-'*w} {'-'*w} {'-'*w}")

    def row(name, gv, cv, fmt=".1f", diff_fmt=None):
        if diff_fmt is None:
            diff_fmt = fmt
        gs = f"{gv:{fmt}}" if gv is not None else "—"
        cs = f"{cv:{fmt}}" if cv is not None else "—"
        if gv is not None and cv is not None:
            d = cv - gv
            ds = f"{d:{diff_fmt}}"
        else:
            ds = "—"
        print(f"  {name:30s} {gs:>{w}s} {cs:>{w}s} {ds:>{w}s}")

    row("Hits", gpu["n"], g4["n"], "d")
    if n_photons and n_photons > 0:
        row("Hit rate (%)", 100.0 * gpu["n"] / n_photons, 100.0 * g4["n"] / n_photons, ".2f")

    if gpu["n"] > 0 and g4["n"] > 0:
        ratio = g4["n"] / gpu["n"]
        print(f"  {'Ratio G4/GPU':30s} {'':>{w}s} {'':>{w}s} {ratio:>{w}.3f}")

    if gpu["n"] == 0 or g4["n"] == 0:
        print("\n  Cannot compare distributions — one side has zero hits.")
        return

    print()
    row("Wavelength min (nm)", gpu["wl_min"], g4["wl_min"])
    row("Wavelength max (nm)", gpu["wl_max"], g4["wl_max"])
    row("Wavelength mean (nm)", gpu["wl_mean"], g4["wl_mean"])
    row("Wavelength std (nm)", gpu["wl_std"], g4["wl_std"])

    print()
    row("Time min (ns)", gpu["t_min"], g4["t_min"], ".3f")
    row("Time max (ns)", gpu["t_max"], g4["t_max"], ".3f")
    row("Time mean (ns)", gpu["t_mean"], g4["t_mean"], ".3f")
    row("Time std (ns)", gpu["t_std"], g4["t_std"], ".3f")

    print()
    row("Radius min (mm)", gpu["r_min"], g4["r_min"], ".2f")
    row("Radius max (mm)", gpu["r_max"], g4["r_max"], ".2f")
    row("Radius mean (mm)", gpu["r_mean"], g4["r_mean"], ".2f")

    print()
    row("Mean X (mm)", gpu["x_mean"], g4["x_mean"], ".2f")
    row("Mean Y (mm)", gpu["y_mean"], g4["y_mean"], ".2f")
    row("Mean Z (mm)", gpu["z_mean"], g4["z_mean"], ".2f")

    # Statistical significance
    print()
    if n_photons and n_photons > 0:
        p_pool = (gpu["n"] + g4["n"]) / (2 * n_photons)
        std = np.sqrt(p_pool * (1 - p_pool) / n_photons)
        if std > 0:
            z = abs(gpu["n"] / n_photons - g4["n"] / n_photons) / (std * np.sqrt(2))
            expected_fluct = std * np.sqrt(2) * n_photons
            print(f"  {'Z-score (hit count)':30s} {z:>{w}.1f}")
            print(f"  {'Expected 1σ fluctuation':30s} {expected_fluct:>{w}.0f} hits")
            if z > 3:
                print(f"  ** Statistically significant difference (>{3}σ) **")
            else:
                print(f"  Within statistical expectations (<3σ)")
    print()


def print_gpu_outcomes(photon):
    """Print GPU photon outcome summary."""
    q3 = photon[:, 3, :].view(np.uint32)
    flag = q3[:, 0] & 0xFFFF

    print("=" * 70)
    print("GPU PHOTON OUTCOMES")
    print("=" * 70)

    n = len(flag)
    vals, counts = np.unique(flag, return_counts=True)
    order = np.argsort(-counts)

    print(f"\n  {'Flag':<22s} {'Count':>8s} {'%':>7s}")
    print(f"  {'-'*22} {'-'*8} {'-'*7}")
    for idx in order:
        f = vals[idx]
        c = counts[idx]
        name = FLAG_ENUM.get(f, f"0x{f:04x}")
        print(f"  {name:<22s} {c:8d} {100*c/n:6.1f}%")
    print()


def print_wavelength_histograms(gpu_hits, g4_hits):
    """Print overlaid wavelength histograms."""
    if len(gpu_hits) == 0 or len(g4_hits) == 0:
        return

    gpu_wl = gpu_hits[:, 2, 3]
    g4_wl = g4_hits[:, 2, 3]

    wl_min = min(gpu_wl.min(), g4_wl.min())
    wl_max = max(gpu_wl.max(), g4_wl.max())
    bins = np.arange(max(100, np.floor(wl_min / 25) * 25),
                     min(800, np.ceil(wl_max / 25) * 25 + 25), 25)

    gpu_counts, _ = np.histogram(gpu_wl, bins=bins)
    g4_counts, _ = np.histogram(g4_wl, bins=bins)

    # Normalize to same total for shape comparison
    gpu_norm = gpu_counts / len(gpu_hits) * 1000
    g4_norm = g4_counts / len(g4_hits) * 1000

    print("=" * 70)
    print("WAVELENGTH DISTRIBUTION (per 1000 hits)")
    print("=" * 70)
    print(f"\n  {'Bin (nm)':<14s} {'GPU':>8s} {'G4':>8s} {'GPU':^20s} {'G4':^20s}")
    print(f"  {'-'*14} {'-'*8} {'-'*8} {'-'*20} {'-'*20}")

    max_bar = 20
    scale = max(gpu_norm.max(), g4_norm.max())
    if scale == 0:
        scale = 1

    for i in range(len(bins) - 1):
        if gpu_counts[i] == 0 and g4_counts[i] == 0:
            continue
        gpu_bar = "#" * int(gpu_norm[i] / scale * max_bar)
        g4_bar = "#" * int(g4_norm[i] / scale * max_bar)
        print(f"  {bins[i]:5.0f}-{bins[i+1]:5.0f}   {gpu_norm[i]:8.1f} {g4_norm[i]:8.1f}"
              f" {gpu_bar:<20s} {g4_bar:<20s}")
    print()


def print_time_histograms(gpu_hits, g4_hits):
    """Print overlaid time histograms."""
    if len(gpu_hits) == 0 or len(g4_hits) == 0:
        return

    gpu_t = gpu_hits[:, 0, 3]
    g4_t = g4_hits[:, 0, 3]

    t_max = max(gpu_t.max(), g4_t.max())
    bin_size = max(1.0, np.ceil(t_max / 15))
    bins = np.arange(0, t_max + bin_size, bin_size)

    gpu_counts, _ = np.histogram(gpu_t, bins=bins)
    g4_counts, _ = np.histogram(g4_t, bins=bins)

    gpu_norm = gpu_counts / len(gpu_hits) * 1000
    g4_norm = g4_counts / len(g4_hits) * 1000

    print("=" * 70)
    print("TIME DISTRIBUTION (per 1000 hits)")
    print("=" * 70)
    print(f"\n  {'Bin (ns)':<14s} {'GPU':>8s} {'G4':>8s} {'GPU':^20s} {'G4':^20s}")
    print(f"  {'-'*14} {'-'*8} {'-'*8} {'-'*20} {'-'*20}")

    max_bar = 20
    scale = max(gpu_norm.max(), g4_norm.max())
    if scale == 0:
        scale = 1

    for i in range(len(bins) - 1):
        if gpu_counts[i] == 0 and g4_counts[i] == 0:
            continue
        gpu_bar = "#" * int(gpu_norm[i] / scale * max_bar)
        g4_bar = "#" * int(g4_norm[i] / scale * max_bar)
        print(f"  {bins[i]:5.1f}-{bins[i+1]:5.1f}   {gpu_norm[i]:8.1f} {g4_norm[i]:8.1f}"
              f" {gpu_bar:<20s} {g4_bar:<20s}")
    print()


def main():
    parser = argparse.ArgumentParser(
        description="Compare GPU (opticks) vs G4 (standalone) simulation hits",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("gpu_path", help="Path to GPU opticks event folder")
    parser.add_argument("g4_hits", help="Path to G4 hits file (g4_hits.npy)")
    parser.add_argument("--histograms", action="store_true",
                        help="Show wavelength and time distribution histograms")

    args = parser.parse_args()

    gpu_path = resolve_event_path(args.gpu_path)
    if not os.path.exists(os.path.join(gpu_path, "photon.npy")):
        print(f"Error: photon.npy not found in {gpu_path}")
        sys.exit(1)
    if not os.path.exists(args.g4_hits):
        print(f"Error: {args.g4_hits} not found")
        sys.exit(1)

    # Load GPU arrays
    gpu_hits = np.load(os.path.join(gpu_path, "hit.npy")) if os.path.exists(os.path.join(gpu_path, "hit.npy")) else np.zeros((0, 4, 4), dtype=np.float32)
    gpu_photon = np.load(os.path.join(gpu_path, "photon.npy"))
    n_photons = len(gpu_photon)

    # Load G4 hits
    g4_hits = np.load(args.g4_hits)

    print(f"\nGPU event: {gpu_path}")
    print(f"G4 hits:   {args.g4_hits}")
    print(f"Total photons: {n_photons}\n")

    # Compute stats
    gpu_stats = hit_stats(gpu_hits, "GPU")
    g4_stats = hit_stats(g4_hits, "G4")

    # Print tables
    print_comparison_table(gpu_stats, g4_stats, n_photons)
    print_gpu_outcomes(gpu_photon)

    if args.histograms:
        print_wavelength_histograms(gpu_hits, g4_hits)
        print_time_histograms(gpu_hits, g4_hits)


if __name__ == "__main__":
    main()
