#!/usr/bin/env python3
"""Compare G4 vs Opticks hit files: hit count + per-axis chi2."""
import re, sys, numpy as np

PAT = re.compile(r'^\s*([\-\d.eE+]+)\s+([\-\d.eE+]+)\s+\(([^)]+)\)\s+\(([^)]+)\)')

def load(p):
    rows = []
    for line in open(p):
        m = PAT.match(line)
        if not m: continue
        rows.append((float(m.group(1)), float(m.group(2)),
                     *[float(x) for x in m.group(3).split(',')],
                     *[float(x) for x in m.group(4).split(',')]))
    return np.array(rows, float) if rows else np.empty((0,8))

def chi2_1d(a, b, bins):
    ha, _ = np.histogram(a, bins=bins)
    hb, _ = np.histogram(b, bins=bins)
    m = (ha + hb) > 0
    return float(np.sum((ha[m] - hb[m])**2 / (ha[m] + hb[m]))), int(m.sum())

g_path, o_path, label = sys.argv[1], sys.argv[2], sys.argv[3]
tolerance_count = float(sys.argv[4]) if len(sys.argv) > 4 else 5.0
tolerance_chi2  = float(sys.argv[5]) if len(sys.argv) > 5 else 5.0

g = load(g_path); o = load(o_path)
n_g, n_o = len(g), len(o)
rel = abs(n_o - n_g) / ((n_o + n_g) / 2) * 100 if n_o + n_g > 0 else 0

print(f"=== {label} ===")
print(f"  G4 hits:      {n_g}")
print(f"  Opticks hits: {n_o}")
print(f"  rel diff:     {rel:.3f}%   (tol={tolerance_count}%)")

fail = []
if rel > tolerance_count:
    fail.append(f"count rel-diff {rel:.2f}% > {tolerance_count}%")
if n_g == 0 or n_o == 0:
    print("  no hits, skip distributions")
else:
    for col, name, bins in [
        (2, 'x',  np.linspace(min(g[:,2].min(), o[:,2].min()), max(g[:,2].max(), o[:,2].max()), 31)),
        (3, 'y',  np.linspace(min(g[:,3].min(), o[:,3].min()), max(g[:,3].max(), o[:,3].max()), 31)),
        (5, 'dx', np.linspace(-1, 1, 21)),
        (6, 'dy', np.linspace(-1, 1, 21)),
        (7, 'dz', np.linspace(-1, 1, 21)),
    ]:
        chi2, ndf = chi2_1d(g[:, col], o[:, col], bins)
        ratio = chi2 / max(ndf, 1)
        marker = "FAIL" if ratio > tolerance_chi2 else "ok"
        print(f"  {name:>2}: chi2/ndf = {chi2:7.2f}/{ndf} = {ratio:5.2f}  {marker}")
        if ratio > tolerance_chi2:
            fail.append(f"{name} chi2/ndf {ratio:.2f}")

if fail:
    print(f"  FAILED: {', '.join(fail)}")
    sys.exit(1)
print("  PASS")
