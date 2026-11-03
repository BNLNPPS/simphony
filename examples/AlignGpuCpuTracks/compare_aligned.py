#!/usr/bin/env python3
"""
compare_aligned.py - Photon-by-photon comparison of GPU vs G4 aligned simulations.

Usage:
    python compare_aligned.py <gpu_photon.npy> <g4_photon.npy>

Performs:
  1. Per-photon flag comparison (exact match rate)
  2. Position comparison at multiple thresholds
  3. Chi-squared test on flag distributions (gold-standard validation metric)
  4. Glancing-angle photon identification (normal sign ambiguity)
  5. Divergent photon listing
"""
import sys
import numpy as np

FLAG_NAMES = {
    0x0004: "TORCH", 0x0008: "BULK_ABSORB", 0x0010: "BULK_REEMIT",
    0x0020: "BULK_SCATTER", 0x0040: "SURFACE_DETECT", 0x0080: "SURFACE_ABSORB",
    0x0100: "SURFACE_DREFLECT", 0x0200: "SURFACE_SREFLECT",
    0x0400: "BOUNDARY_REFLECT", 0x0800: "BOUNDARY_TRANSMIT", 0x8000: "MISS",
}

def flag_name(f):
    return FLAG_NAMES.get(f, f"0x{f:04x}")

def extract_flag(photon):
    """Extract flag from q3.x (orient_boundary_flag) - lower 16 bits."""
    q3 = photon.view(np.uint32).reshape(-1, 4, 4)
    return q3[:, 3, 0] & 0xFFFF

def chi2_flag_distribution(gpu_flags, g4_flags):
    """
    Chi-squared comparison of flag distributions.

    Compares the frequency of each flag value between GPU and G4.
    This is the opticks gold-standard validation metric.

    Returns (chi2, ndof, flags_used, gpu_counts, g4_counts).
    """
    all_flags = sorted(set(gpu_flags) | set(g4_flags))
    gpu_counts = np.array([(gpu_flags == f).sum() for f in all_flags], dtype=float)
    g4_counts  = np.array([(g4_flags == f).sum() for f in all_flags], dtype=float)

    total = gpu_counts + g4_counts
    mask = total > 0
    gpu_c = gpu_counts[mask]
    g4_c  = g4_counts[mask]
    tot   = total[mask]
    flags_used = [f for f, m in zip(all_flags, mask) if m]

    n_gpu = gpu_c.sum()
    n_g4  = g4_c.sum()
    expected_gpu = tot * n_gpu / (n_gpu + n_g4)
    expected_g4  = tot * n_g4  / (n_gpu + n_g4)

    chi2 = 0.0
    for i in range(len(flags_used)):
        if expected_gpu[i] > 0:
            chi2 += (gpu_c[i] - expected_gpu[i])**2 / expected_gpu[i]
        if expected_g4[i] > 0:
            chi2 += (g4_c[i] - expected_g4[i])**2 / expected_g4[i]

    ndof = max(len(flags_used) - 1, 1)
    return chi2, ndof, flags_used, gpu_c, g4_c

def identify_glancing(gpu, g4):
    """
    Identify glancing-angle photons where the normal sign ambiguity
    causes momentum negation between GPU and G4.

    At glancing incidence cos(theta) ~ 0, float32 vs float64 can produce
    opposite normal signs, reflecting the photon in the opposite direction.
    These photons have matching flags but very different positions.

    Returns boolean mask of glancing photons.
    """
    gpu_mom = gpu[:, 1, :3]
    g4_mom  = g4[:, 1, :3]

    # Normalize momenta (should already be unit vectors, but be safe)
    gpu_norm = np.linalg.norm(gpu_mom, axis=1, keepdims=True)
    g4_norm  = np.linalg.norm(g4_mom, axis=1, keepdims=True)
    gpu_norm[gpu_norm == 0] = 1
    g4_norm[g4_norm == 0] = 1

    gpu_hat = gpu_mom / gpu_norm
    g4_hat  = g4_mom / g4_norm

    # Dot product of momentum directions: -1 = fully negated (normal flip)
    mom_dot = np.sum(gpu_hat * g4_hat, axis=1)

    # Glancing: momentum vectors are nearly anti-parallel (dot ~ -1)
    glancing = mom_dot < -0.5
    return glancing, mom_dot

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <gpu_photon.npy> <g4_photon.npy>")
        sys.exit(1)

    gpu = np.load(sys.argv[1])
    g4  = np.load(sys.argv[2])

    print(f"GPU shape: {gpu.shape}")
    print(f"G4  shape: {g4.shape}")

    n = min(len(gpu), len(g4))
    gpu = gpu[:n]
    g4  = g4[:n]

    gpu_flags = extract_flag(gpu)
    g4_flags  = extract_flag(g4)

    # ---- 1. Per-photon flag comparison ----
    match = gpu_flags == g4_flags
    n_match = match.sum()
    n_diff = n - n_match
    print(f"\n{'='*60}")
    print(f"FLAG COMPARISON ({n} photons)")
    print(f"{'='*60}")
    print(f"  Matching: {n_match} ({100*n_match/n:.1f}%)")
    print(f"  Differ:   {n_diff} ({100*n_diff/n:.1f}%)")

    # ---- 2. Position comparison ----
    gpu_pos = gpu[:, 0, :3]
    g4_pos  = g4[:, 0, :3]
    pos_diff = np.linalg.norm(gpu_pos - g4_pos, axis=1)
    zero_g4 = np.all(g4_pos == 0, axis=1)

    valid = ~zero_g4
    n_valid = valid.sum()
    print(f"\n{'='*60}")
    print(f"POSITION COMPARISON ({n_valid} valid, {zero_g4.sum()} unrecorded)")
    print(f"{'='*60}")
    if n_valid > 0:
        vdiff = pos_diff[valid]
        print(f"  Mean dist:   {vdiff.mean():.4f} mm")
        print(f"  Max dist:    {vdiff.max():.4f} mm")
        print(f"  < 0.01 mm:   {(vdiff < 0.01).sum()} ({100*(vdiff < 0.01).sum()/n_valid:.1f}%)")
        print(f"  < 0.1 mm:    {(vdiff < 0.1).sum()} ({100*(vdiff < 0.1).sum()/n_valid:.1f}%)")
        print(f"  < 1.0 mm:    {(vdiff < 1.0).sum()} ({100*(vdiff < 1.0).sum()/n_valid:.1f}%)")

    # ---- 3. Chi-squared test on flag distributions ----
    print(f"\n{'='*60}")
    print(f"CHI-SQUARED TEST (flag distribution)")
    print(f"{'='*60}")

    chi2_val, ndof, flags_used, gpu_c, g4_c = chi2_flag_distribution(gpu_flags, g4_flags)

    print(f"  {'Flag':<20s} {'GPU':>8s} {'G4':>8s} {'Diff':>8s}")
    print(f"  {'-'*20} {'-'*8} {'-'*8} {'-'*8}")
    for i, f in enumerate(flags_used):
        diff = int(gpu_c[i] - g4_c[i])
        sign = "+" if diff > 0 else ""
        print(f"  {flag_name(f):<20s} {int(gpu_c[i]):>8d} {int(g4_c[i]):>8d} {sign}{diff:>7d}")

    deviant_frac = 100 * n_diff / n if n > 0 else 0
    print(f"\n  chi2/ndof = {chi2_val:.2f}/{ndof} = {chi2_val/ndof:.2f}")
    print(f"  deviant fraction: {deviant_frac:.2f}% ({n_diff}/{n})")

    # ---- 4. Glancing-angle analysis ----
    print(f"\n{'='*60}")
    print(f"GLANCING-ANGLE ANALYSIS (normal sign ambiguity)")
    print(f"{'='*60}")

    glancing, mom_dot = identify_glancing(gpu, g4)
    n_glancing = glancing.sum()

    # Among matching-flag photons, how many are glancing with large pos diff?
    match_glancing = match & glancing
    match_large_pos = match & (pos_diff > 1.0)
    match_glancing_large = match & glancing & (pos_diff > 1.0)

    print(f"  Glancing photons (mom dot < -0.5):  {n_glancing}")
    print(f"  Matching flag + pos diff > 1mm:      {match_large_pos.sum()}")
    print(f"  Of those, glancing:                  {match_glancing_large.sum()}")
    if match_large_pos.sum() > 0:
        frac = 100 * match_glancing_large.sum() / match_large_pos.sum()
        print(f"  Fraction explained by glancing:      {frac:.0f}%")

    # Position stats excluding glancing photons
    non_glancing_match = match & ~glancing & valid
    if non_glancing_match.sum() > 0:
        ng_diff = pos_diff[non_glancing_match]
        print(f"\n  Position (matching, non-glancing, {non_glancing_match.sum()} photons):")
        print(f"    Max dist:  {ng_diff.max():.6f} mm")
        print(f"    Mean dist: {ng_diff.mean():.6f} mm")
        print(f"    < 0.01 mm: {(ng_diff < 0.01).sum()} ({100*(ng_diff < 0.01).sum()/non_glancing_match.sum():.1f}%)")

    # ---- 5. Divergent photon listing ----
    if n_diff > 0:
        div_idx = np.where(~match)[0]
        print(f"\n{'='*60}")
        print(f"DIVERGENT PHOTONS (first 10 of {n_diff})")
        print(f"{'='*60}")
        for i in div_idx[:10]:
            gf = flag_name(gpu_flags[i])
            cf = flag_name(g4_flags[i])
            gp = gpu_pos[i]
            cp = g4_pos[i]
            print(f"  [{i:5d}] GPU: {gf:20s} pos=({gp[0]:8.2f},{gp[1]:8.2f},{gp[2]:8.2f})")
            print(f"          G4:  {cf:20s} pos=({cp[0]:8.2f},{cp[1]:8.2f},{cp[2]:8.2f})")

if __name__ == "__main__":
    main()
