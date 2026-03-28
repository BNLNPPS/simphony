#!/usr/bin/env python3
"""
compare_aligned.py - Photon-by-photon comparison of GPU vs G4 aligned simulations.

Usage:
    python compare_aligned.py <gpu_photon.npy> <g4_photon.npy>
"""
import sys
import numpy as np

def flag_name(f):
    names = {
        0x0004: "TORCH", 0x0008: "BULK_ABSORB", 0x0010: "BULK_REEMIT",
        0x0020: "BULK_SCATTER", 0x0040: "SURFACE_DETECT", 0x0080: "SURFACE_ABSORB",
        0x0100: "SURFACE_DREFLECT", 0x0200: "SURFACE_SREFLECT",
        0x0400: "BOUNDARY_REFLECT", 0x0800: "BOUNDARY_TRANSMIT", 0x8000: "MISS",
    }
    return names.get(f, f"0x{f:04x}")

def extract_flag(photon):
    """Extract flag from q3.x (orient_boundary_flag) - lower 16 bits."""
    q3 = photon.view(np.uint32).reshape(-1, 4, 4)
    return q3[:, 3, 0] & 0xFFFF

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

    # Flag comparison
    match = gpu_flags == g4_flags
    n_match = match.sum()
    n_diff = n - n_match
    print(f"\nFlag comparison ({n} photons):")
    print(f"  Matching: {n_match} ({100*n_match/n:.1f}%)")
    print(f"  Differ:   {n_diff} ({100*n_diff/n:.1f}%)")

    # Position comparison
    gpu_pos = gpu[:, 0, :3]  # q0.xyz = position
    g4_pos  = g4[:, 0, :3]

    pos_diff = np.linalg.norm(gpu_pos - g4_pos, axis=1)
    zero_g4 = np.all(g4_pos == 0, axis=1)  # G4 photon not recorded (indexed mode gaps)

    valid = ~zero_g4
    n_valid = valid.sum()
    print(f"\nPosition comparison ({n_valid} valid G4 photons, {zero_g4.sum()} zero/unrecorded):")
    if n_valid > 0:
        vdiff = pos_diff[valid]
        print(f"  Mean dist:   {vdiff.mean():.4f} mm")
        print(f"  Max dist:    {vdiff.max():.4f} mm")
        print(f"  < 0.01 mm:   {(vdiff < 0.01).sum()} ({100*(vdiff < 0.01).sum()/n_valid:.1f}%)")
        print(f"  < 0.1 mm:    {(vdiff < 0.1).sum()} ({100*(vdiff < 0.1).sum()/n_valid:.1f}%)")
        print(f"  < 1.0 mm:    {(vdiff < 1.0).sum()} ({100*(vdiff < 1.0).sum()/n_valid:.1f}%)")

    # Flag distribution
    print(f"\nGPU flag distribution:")
    for f in sorted(set(gpu_flags)):
        c = (gpu_flags == f).sum()
        print(f"  {flag_name(f):20s}: {c:6d} ({100*c/n:.1f}%)")

    print(f"\nG4 flag distribution (aligned):")
    for f in sorted(set(g4_flags)):
        c = (g4_flags == f).sum()
        print(f"  {flag_name(f):20s}: {c:6d} ({100*c/n:.1f}%)")

    # Show first few divergent photons
    if n_diff > 0:
        div_idx = np.where(~match)[0]
        print(f"\nFirst 10 divergent photons:")
        for i in div_idx[:10]:
            gf = flag_name(gpu_flags[i])
            cf = flag_name(g4_flags[i])
            gp = gpu_pos[i]
            cp = g4_pos[i]
            print(f"  [{i:5d}] GPU: {gf:20s} pos=({gp[0]:8.2f},{gp[1]:8.2f},{gp[2]:8.2f})")
            print(f"          G4:  {cf:20s} pos=({cp[0]:8.2f},{cp[1]:8.2f},{cp[2]:8.2f})")

if __name__ == "__main__":
    main()
