#!/usr/bin/env python3
"""
synrad_test.py : pass/fail comparison of GPU vs G4 synrad hit distributions
=============================================================================

Loads the two wall-absorbed hit arrays (shape Nx4x4 sphoton rows) written by
examples/synrad/synrad (GPU mode) and examples/synrad/synrad_g4 (Geant4
reference mode, same input photons, same Cu table, independent RNG)
and runs six checks:

  1. Wall-absorbed count agreement (both modes must absorb ~all N photons)
  2. Reflected-at-least-once fraction, two-proportion Z < 5
     (catches reflectivity-model / normal-convention mismatches)
  3-5. Binned Pearson chi2/ndf < 3 on the z, x and y marginals of the
     absorption points (catches transport / geometry / angle-sampling
     mismatches; x and y resolve the transverse reflection chains that the
     z marginal integrates over)
  6. Binned Pearson chi2/ndf < 3 on the energy spectrum of the
     reflected-at-least-once subset (both modes absorb the identical input
     photons, so the FULL absorbed spectrum is trivially equal; membership
     of the reflected subset is where the reflectivity physics acts)

Designed to be invoked from tests/test_synrad_example.sh.

Usage::

    python3 optiphy/ana/synrad_test.py <gpu_hits.npy> <g4_hits.npy>

Exits 0 on PASS, 1 on FAIL.
"""
import argparse
import math
import sys

import numpy as np

BOUNDARY_REFLECT = 0x1 << 10   # sysrap/OpticksPhoton.h
CHI2_NDF_MAX = 3.0
PROP_Z_MAX = 5.0
COUNT_LOSS_MAX = 1e-3          # fraction of photons a mode may fail to absorb


def load_hits(path):
    a = np.load(path).reshape(-1, 4, 4)
    x = a[:, 0, 0]
    y = a[:, 0, 1]
    z = a[:, 0, 2]
    e = a[:, 2, 3]
    refl = (np.ascontiguousarray(a[:, 3, 3]).view(np.uint32) & BOUNDARY_REFLECT) != 0
    return x, y, z, e, refl


def chi2_ndf(a, b, edges):
    ha, _ = np.histogram(a, bins=edges)
    hb, _ = np.histogram(b, bins=edges)
    m = (ha + hb) > 0
    chi2 = np.sum((ha[m] - hb[m]) ** 2 / (ha[m] + hb[m]).astype(float))
    ndf = max(int(m.sum()) - 1, 1)
    return chi2 / ndf, ndf


def banner(title):
    print("=" * 55)
    print(f"  {title}")
    print("=" * 55)


def marginal_test(name, a, b, nbin=101, ok=True):
    banner(f"TEST: {name} marginal of the absorption points")
    lo = min(a.min(), b.min())
    hi = max(a.max(), b.max())
    c2, ndf = chi2_ndf(a, b, np.linspace(lo, hi, nbin))
    print(f"  chi2/ndf = {c2:.2f}  (ndf {ndf}, max {CHI2_NDF_MAX})")
    passed = c2 < CHI2_NDF_MAX
    print(f"  {'PASS' if passed else 'FAIL'}")
    return ok and passed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gpu")
    ap.add_argument("g4")
    ap.add_argument("--nphoton", type=int, default=0,
                    help="number of input photons (0: use max of the two hit counts)")
    args = ap.parse_args()

    x_gpu, y_gpu, z_gpu, e_gpu, r_gpu = load_hits(args.gpu)
    x_g4, y_g4, z_g4, e_g4, r_g4 = load_hits(args.g4)
    n_gpu, n_g4 = len(z_gpu), len(z_g4)
    n_in = args.nphoton if args.nphoton > 0 else max(n_gpu, n_g4)

    ok = True

    banner("TEST: Wall-absorbed count")
    print(f"  GPU: {n_gpu}   G4: {n_g4}   input: {n_in}")
    loss_gpu = 1.0 - n_gpu / n_in
    loss_g4 = 1.0 - n_g4 / n_in
    print(f"  unabsorbed fraction  GPU {loss_gpu:.2e}  G4 {loss_g4:.2e}  (max {COUNT_LOSS_MAX:.0e})")
    passed = loss_gpu < COUNT_LOSS_MAX and loss_g4 < COUNT_LOSS_MAX
    print(f"  {'PASS' if passed else 'FAIL'}")
    ok &= passed

    banner("TEST: Reflected-at-least-once fraction")
    f_gpu, f_g4 = r_gpu.mean(), r_g4.mean()
    p = (r_gpu.sum() + r_g4.sum()) / (n_gpu + n_g4)
    sigma = math.sqrt(p * (1 - p) * (1 / n_gpu + 1 / n_g4))
    zscore = abs(f_gpu - f_g4) / sigma if sigma > 0 else 0.0
    print(f"  GPU: {f_gpu:.4f}   G4: {f_g4:.4f}   |Z| = {zscore:.1f} sigma  (max {PROP_Z_MAX})")
    passed = zscore < PROP_Z_MAX
    print(f"  {'PASS' if passed else 'FAIL'}")
    ok &= passed

    ok = marginal_test("z", z_gpu, z_g4, ok=ok)
    ok = marginal_test("x", x_gpu, x_g4, ok=ok)
    ok = marginal_test("y", y_gpu, y_g4, ok=ok)

    banner("TEST: energy spectrum of the reflected subset")
    er_gpu, er_g4 = e_gpu[r_gpu], e_g4[r_g4]
    elo = min(er_gpu.min(), er_g4.min())
    ehi = max(er_gpu.max(), er_g4.max())
    edges = np.geomspace(max(elo, 1e-3), ehi, 41)
    c2, ndf = chi2_ndf(er_gpu, er_g4, edges)
    print(f"  chi2/ndf = {c2:.2f}  (ndf {ndf}, max {CHI2_NDF_MAX})")
    passed = c2 < CHI2_NDF_MAX
    print(f"  {'PASS' if passed else 'FAIL'}")
    ok &= passed

    print()
    print("OVERALL: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
