#!/usr/bin/env python3
"""
wls_diagnostic.py : Detailed WLS wavelength distribution comparison GPU vs G4
==============================================================================

Compares wavelength and time distributions from GPU (opticks) and G4 hits,
performs KS test, and checks per-pass WLS conversion probability.

Usage::

    python ana/wls_diagnostic.py <gpu_event_folder> <g4_hits.npy> [--input-wavelength 350]
"""
import sys
import os
import argparse
import numpy as np


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


FLAG_ENUM = {
    0x0004: "TORCH", 0x0008: "BULK_ABSORB", 0x0010: "BULK_REEMIT",
    0x0020: "BULK_SCATTER", 0x0040: "SURFACE_DETECT", 0x0080: "SURFACE_ABSORB",
    0x0100: "SURFACE_DREFLECT", 0x0200: "SURFACE_SREFLECT",
    0x0400: "BOUNDARY_REFLECT", 0x0800: "BOUNDARY_TRANSMIT",
    0x1000: "NAN_ABORT", 0x2000: "EFFICIENCY_COLLECT", 0x8000: "MISS",
}


def ks_test_2sample(a, b):
    """Two-sample Kolmogorov-Smirnov test (no scipy dependency)."""
    na, nb = len(a), len(b)
    a_sorted = np.sort(a)
    b_sorted = np.sort(b)
    all_vals = np.sort(np.concatenate([a_sorted, b_sorted]))

    cdf_a = np.searchsorted(a_sorted, all_vals, side='right') / na
    cdf_b = np.searchsorted(b_sorted, all_vals, side='right') / nb
    d_stat = np.max(np.abs(cdf_a - cdf_b))

    # Approximate p-value (asymptotic)
    n_eff = np.sqrt(na * nb / (na + nb))
    lam = (n_eff + 0.12 + 0.11 / n_eff) * d_stat
    # Kolmogorov distribution approximation
    if lam < 0.001:
        p_val = 1.0
    else:
        p_val = 2.0 * np.exp(-2.0 * lam * lam)
        p_val = max(0.0, min(1.0, p_val))
    return d_stat, p_val


def print_header(title):
    print()
    print("=" * 74)
    print(f"  {title}")
    print("=" * 74)


def print_hit_summary(gpu_hits, g4_hits, n_photons, input_wl):
    print_header("HIT COUNT SUMMARY")
    ng, nc = len(gpu_hits), len(g4_hits)
    print(f"  Input photons:     {n_photons:>10d}   (wavelength = {input_wl:.0f} nm)")
    print(f"  GPU hits:          {ng:>10d}   ({100*ng/n_photons:.2f}%)")
    print(f"  G4  hits:          {nc:>10d}   ({100*nc/n_photons:.2f}%)")
    if ng > 0 and nc > 0:
        ratio = nc / ng
        # Significance
        p_pool = (ng + nc) / (2 * n_photons)
        se = np.sqrt(2 * p_pool * (1 - p_pool) / n_photons)
        z = abs(ng/n_photons - nc/n_photons) / se if se > 0 else 0
        print(f"  Ratio G4/GPU:      {ratio:>10.4f}")
        print(f"  Z-score:           {z:>10.1f}   {'** SIGNIFICANT **' if z > 3 else '(within noise)'}")
    print()


def print_wavelength_comparison(gpu_wl, g4_wl):
    print_header("WAVELENGTH DISTRIBUTION COMPARISON")

    print(f"\n  {'Statistic':<25s} {'GPU':>12s} {'G4':>12s} {'Diff':>12s}")
    print(f"  {'-'*25} {'-'*12} {'-'*12} {'-'*12}")

    for name, fn in [("Mean (nm)", np.mean), ("Std (nm)", np.std),
                     ("Median (nm)", np.median), ("Min (nm)", np.min),
                     ("Max (nm)", np.max)]:
        gv, cv = fn(gpu_wl), fn(g4_wl)
        print(f"  {name:<25s} {gv:12.2f} {cv:12.2f} {cv-gv:12.2f}")

    # Percentiles
    print()
    for pct in [5, 25, 75, 95]:
        gv = np.percentile(gpu_wl, pct)
        cv = np.percentile(g4_wl, pct)
        print(f"  {'P%d (nm)' % pct:<25s} {gv:12.2f} {cv:12.2f} {cv-gv:12.2f}")

    # KS test
    d_stat, p_val = ks_test_2sample(gpu_wl, g4_wl)
    print(f"\n  KS statistic:      {d_stat:.6f}")
    print(f"  KS p-value:        {p_val:.2e}")
    if p_val < 0.01:
        print("  ** Wavelength distributions are SIGNIFICANTLY DIFFERENT **")
    else:
        print("  Wavelength distributions are statistically compatible")
    print()


def print_fine_histogram(gpu_wl, g4_wl, bin_width=10):
    print_header(f"WAVELENGTH HISTOGRAM (bin={bin_width}nm)")

    lo = min(gpu_wl.min(), g4_wl.min())
    hi = max(gpu_wl.max(), g4_wl.max())
    bins = np.arange(np.floor(lo / bin_width) * bin_width,
                     np.ceil(hi / bin_width) * bin_width + bin_width, bin_width)

    gc, _ = np.histogram(gpu_wl, bins=bins)
    cc, _ = np.histogram(g4_wl, bins=bins)

    # Normalize to density (per nm per photon)
    gpu_dens = gc / (len(gpu_wl) * bin_width)
    g4_dens = cc / (len(g4_wl) * bin_width)

    max_dens = max(gpu_dens.max(), g4_dens.max())
    bar_w = 25

    print(f"\n  {'Bin (nm)':<14s} {'GPU':>8s} {'G4':>8s} {'GPU/G4':>7s}  GPU|G4")
    print(f"  {'-'*14} {'-'*8} {'-'*8} {'-'*7}  {'-'*51}")

    for i in range(len(bins) - 1):
        if gc[i] == 0 and cc[i] == 0:
            continue
        ratio_str = f"{gc[i]/cc[i]:.2f}" if cc[i] > 0 else "  inf"
        gb = "#" * int(gpu_dens[i] / max_dens * bar_w) if max_dens > 0 else ""
        cb = "=" * int(g4_dens[i] / max_dens * bar_w) if max_dens > 0 else ""
        print(f"  {bins[i]:5.0f}-{bins[i+1]:5.0f}   {gc[i]:8d} {cc[i]:8d} {ratio_str:>7s}  {gb:<25s}|{cb:<25s}")
    print()


def print_time_comparison(gpu_t, g4_t):
    print_header("TIME DISTRIBUTION COMPARISON")

    print(f"\n  {'Statistic':<25s} {'GPU':>12s} {'G4':>12s} {'Diff':>12s}")
    print(f"  {'-'*25} {'-'*12} {'-'*12} {'-'*12}")

    for name, fn in [("Mean (ns)", np.mean), ("Std (ns)", np.std),
                     ("Median (ns)", np.median), ("Min (ns)", np.min),
                     ("Max (ns)", np.max)]:
        gv, cv = fn(gpu_t), fn(g4_t)
        print(f"  {name:<25s} {gv:12.4f} {cv:12.4f} {cv-gv:12.4f}")

    d_stat, p_val = ks_test_2sample(gpu_t, g4_t)
    print(f"\n  KS statistic:      {d_stat:.6f}")
    print(f"  KS p-value:        {p_val:.2e}")
    if p_val < 0.01:
        print("  ** Time distributions are SIGNIFICANTLY DIFFERENT **")
    else:
        print("  Time distributions are statistically compatible")
    print()


def print_gpu_outcomes(photon):
    print_header("GPU PHOTON OUTCOMES (all photons)")
    q3 = photon[:, 3, :].view(np.uint32)
    flag = q3[:, 0] & 0xFFFF
    n = len(flag)
    vals, counts = np.unique(flag, return_counts=True)
    order = np.argsort(-counts)

    print(f"\n  {'Flag':<22s} {'Count':>8s} {'%':>7s}")
    print(f"  {'-'*22} {'-'*8} {'-'*7}")
    for idx in order:
        f = vals[idx]
        c = counts[idx]
        name = FLAG_ENUM.get(f, f"0x{f:04x}")
        print(f"  {name:<22s} {c:8d} {100*c/n:6.2f}%")
    print()


def print_position_comparison(gpu_hits, g4_hits):
    print_header("SPATIAL DISTRIBUTION")

    gpu_pos = gpu_hits[:, 0, :3]
    g4_pos = g4_hits[:, 0, :3]
    gpu_r = np.sqrt(np.sum(gpu_pos**2, axis=1))
    g4_r = np.sqrt(np.sum(g4_pos**2, axis=1))

    print(f"\n  {'Statistic':<25s} {'GPU':>12s} {'G4':>12s} {'Diff':>12s}")
    print(f"  {'-'*25} {'-'*12} {'-'*12} {'-'*12}")

    for name, gv, cv in [
        ("Mean radius (mm)", gpu_r.mean(), g4_r.mean()),
        ("Mean X (mm)", gpu_pos[:, 0].mean(), g4_pos[:, 0].mean()),
        ("Mean Y (mm)", gpu_pos[:, 1].mean(), g4_pos[:, 1].mean()),
        ("Mean Z (mm)", gpu_pos[:, 2].mean(), g4_pos[:, 2].mean()),
    ]:
        print(f"  {name:<25s} {gv:12.3f} {cv:12.3f} {cv-gv:12.3f}")
    print()


def print_energy_conservation(gpu_wl, g4_wl, input_wl):
    print_header("ENERGY CONSERVATION CHECK")
    gpu_viol = np.sum(gpu_wl < input_wl)
    g4_viol = np.sum(g4_wl < input_wl)
    print(f"  Input wavelength:          {input_wl:.0f} nm")
    print(f"  GPU hits with wl < input:  {gpu_viol} / {len(gpu_wl)}")
    print(f"  G4  hits with wl < input:  {g4_viol} / {len(g4_wl)}")
    if gpu_viol == 0 and g4_viol == 0:
        print("  ALL PASS: energy conservation satisfied")
    else:
        if gpu_viol > 0:
            bad = gpu_wl[gpu_wl < input_wl]
            print(f"  GPU violations: min={bad.min():.1f}nm, mean={bad.mean():.1f}nm")
        if g4_viol > 0:
            bad = g4_wl[g4_wl < input_wl]
            print(f"  G4  violations: min={bad.min():.1f}nm, mean={bad.mean():.1f}nm")
    print()


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("gpu_path", help="GPU opticks event folder")
    parser.add_argument("g4_hits", help="G4 hits file (g4_hits.npy)")
    parser.add_argument("--input-wavelength", type=float, default=350.0,
                        help="Input photon wavelength in nm (default: 350)")
    parser.add_argument("--bin-width", type=float, default=5.0,
                        help="Histogram bin width in nm (default: 5)")
    args = parser.parse_args()

    gpu_path = resolve_event_path(args.gpu_path)
    hit_path = os.path.join(gpu_path, "hit.npy")
    photon_path = os.path.join(gpu_path, "photon.npy")

    if not os.path.exists(photon_path):
        print(f"Error: photon.npy not found in {gpu_path}")
        sys.exit(1)
    if not os.path.exists(args.g4_hits):
        print(f"Error: {args.g4_hits} not found")
        sys.exit(1)

    gpu_photon = np.load(photon_path)
    gpu_hits = np.load(hit_path) if os.path.exists(hit_path) else np.zeros((0, 4, 4), dtype=np.float32)
    g4_hits = np.load(args.g4_hits)
    n_photons = len(gpu_photon)

    print(f"\n  GPU event path: {gpu_path}")
    print(f"  G4 hits file:   {args.g4_hits}")

    # Hit summary
    print_hit_summary(gpu_hits, g4_hits, n_photons, args.input_wavelength)

    if len(gpu_hits) == 0 or len(g4_hits) == 0:
        print("  Cannot compare — one side has zero hits.")
        return

    gpu_wl = gpu_hits[:, 2, 3]
    g4_wl = g4_hits[:, 2, 3]
    gpu_t = gpu_hits[:, 0, 3]
    g4_t = g4_hits[:, 0, 3]

    # GPU outcomes
    print_gpu_outcomes(gpu_photon)

    # Energy conservation
    print_energy_conservation(gpu_wl, g4_wl, args.input_wavelength)

    # Wavelength comparison
    print_wavelength_comparison(gpu_wl, g4_wl)
    print_fine_histogram(gpu_wl, g4_wl, bin_width=args.bin_width)

    # Time comparison
    print_time_comparison(gpu_t, g4_t)

    # Spatial
    print_position_comparison(gpu_hits, g4_hits)


if __name__ == "__main__":
    main()
