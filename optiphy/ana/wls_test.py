#!/usr/bin/env python3
"""
wls_test.py : pass/fail comparison of GPU vs G4 WLS hit distributions
======================================================================

Loads two hit.npy arrays (shape Nx4x4) and runs four checks:

  1. Hit count agreement (Z-score < 5)
  2. WLS conversion fraction within 3% (photons with wl > 380 nm)
  3. Two-sample KS on the shifted-wavelength spectrum (p > ALPHA)
  4. Two-sample KS on shifted-photon arrival times (p > ALPHA)

Designed to be invoked from tests/test_wavelength_shifting.sh.

Usage::

    python3 optiphy/ana/wls_test.py <gpu_hit.npy> <g4_hit.npy> [--alpha 0.001]

Exits 0 on PASS, 1 on FAIL.
"""
import argparse
import math
import os
import sys

import numpy as np

# Reuse ks_test_2sample from the diagnostic script in the same directory.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wls_diagnostic import ks_test_2sample  # noqa: E402


WLS_THRESHOLD_NM = 380.0


def load_hits(path):
    return np.load(path).reshape(-1, 4, 4)


def hit_count_test(gpu, g4):
    n_gpu, n_g4 = len(gpu), len(g4)
    sigma = math.sqrt(n_gpu + n_g4)
    z = abs(n_gpu - n_g4) / sigma if sigma > 0 else 0.0
    print("=" * 55)
    print("  TEST 1: Hit Count")
    print("=" * 55)
    print(f"  GPU: {n_gpu}")
    print(f"  G4:  {n_g4}")
    print(f"  |Z| = {z:.1f} sigma")
    passed = z < 5
    print(f"  Result: {'PASS' if passed else 'FAIL'} (threshold: 5 sigma)")
    return passed


def wls_fraction_test(gpu_wl, g4_wl, tolerance=0.03):
    gpu_frac = float(np.mean(gpu_wl > WLS_THRESHOLD_NM))
    g4_frac = float(np.mean(g4_wl > WLS_THRESHOLD_NM))
    diff = abs(gpu_frac - g4_frac)
    print()
    print("=" * 55)
    print("  TEST 2: WLS Conversion Fraction")
    print("=" * 55)
    print(f"  GPU shifted: {100 * gpu_frac:.1f}%")
    print(f"  G4  shifted: {100 * g4_frac:.1f}%")
    print(f"  |Difference|: {100 * diff:.2f}%")
    passed = diff < tolerance
    print(f"  Result: {'PASS' if passed else 'FAIL'} (threshold: {100 * tolerance:.0f}%)")
    return passed


def shifted_wavelength_ks(gpu_wl, g4_wl, alpha):
    gpu_shifted = gpu_wl[gpu_wl > WLS_THRESHOLD_NM]
    g4_shifted = g4_wl[g4_wl > WLS_THRESHOLD_NM]
    print()
    print("=" * 55)
    print("  TEST 3: Shifted Wavelength Spectrum (KS Test)")
    print("=" * 55)
    if len(gpu_shifted) <= 10 or len(g4_shifted) <= 10:
        print("  Too few shifted photons for KS test")
        print("  Result: PASS (insufficient stats, skipped)")
        return True
    d, p = ks_test_2sample(gpu_shifted, g4_shifted)
    print(f"  GPU shifted: N={len(gpu_shifted)}, mean={gpu_shifted.mean():.1f}nm")
    print(f"  G4  shifted: N={len(g4_shifted)}, mean={g4_shifted.mean():.1f}nm")
    print(f"  KS D={d:.6f}  p={p:.4f}")
    passed = p >= alpha
    print(f"  Result: {'PASS' if passed else 'FAIL'} (threshold: p > {alpha})")
    return passed


def shifted_time_ks(gpu_wl, g4_wl, gpu_time, g4_time, alpha):
    gpu_t = gpu_time[gpu_wl > WLS_THRESHOLD_NM]
    g4_t = g4_time[g4_wl > WLS_THRESHOLD_NM]
    print()
    print("=" * 55)
    print("  TEST 4: Shifted Photon Arrival Time (KS Test)")
    print("=" * 55)
    if len(gpu_t) > 0 and len(g4_t) > 0:
        print(f"  GPU shifted: N={len(gpu_t)}, "
              f"mean={gpu_t.mean():.3f}ns, std={gpu_t.std():.3f}ns")
        print(f"  G4  shifted: N={len(g4_t)}, "
              f"mean={g4_t.mean():.3f}ns, std={g4_t.std():.3f}ns")
        if g4_t.std() > 0:
            print(f"  Std ratio: {gpu_t.std() / g4_t.std():.3f} (expect ~1.0)")
    if len(gpu_t) <= 10 or len(g4_t) <= 10:
        print("  Too few shifted photons for KS test")
        print("  Result: PASS (insufficient stats, skipped)")
        return True
    d, p = ks_test_2sample(gpu_t, g4_t)
    print(f"  KS D={d:.6f}  p={p:.4f}")
    gpu_unshifted = gpu_time[gpu_wl <= WLS_THRESHOLD_NM]
    g4_unshifted = g4_time[g4_wl <= WLS_THRESHOLD_NM]
    if len(gpu_unshifted) > 0 and len(g4_unshifted) > 0:
        print(f"  Unshifted time: GPU mean={gpu_unshifted.mean():.3f}ns  "
              f"G4 mean={g4_unshifted.mean():.3f}ns")
    passed = p >= alpha
    print(f"  Result: {'PASS' if passed else 'FAIL'} (KS p > {alpha})")
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("gpu_hit", help="GPU hit.npy")
    parser.add_argument("g4_hit", help="G4 hits npy file")
    parser.add_argument("--alpha", type=float, default=0.001,
                        help="KS p-value significance threshold (default: 0.001)")
    args = parser.parse_args()

    gpu = load_hits(args.gpu_hit)
    g4 = load_hits(args.g4_hit)

    gpu_wl, g4_wl = gpu[:, 2, 3], g4[:, 2, 3]
    gpu_time, g4_time = gpu[:, 0, 3], g4[:, 0, 3]

    results = [
        ("Hit count",             hit_count_test(gpu, g4)),
        ("WLS fraction",          wls_fraction_test(gpu_wl, g4_wl)),
        ("Shifted wavelength KS", shifted_wavelength_ks(gpu_wl, g4_wl, args.alpha)),
        ("Shifted time KS",       shifted_time_ks(gpu_wl, g4_wl, gpu_time, g4_time, args.alpha)),
    ]

    print()
    print("=" * 55)
    print("  SUMMARY")
    print("=" * 55)
    for name, passed in results:
        print(f"  {name:>25s}: {'PASS' if passed else 'FAIL'}")

    print()
    if all(p for _, p in results):
        print("  *** ALL TESTS PASSED ***")
        sys.exit(0)
    else:
        print("  *** SOME TESTS FAILED ***")
        sys.exit(1)


if __name__ == "__main__":
    main()
