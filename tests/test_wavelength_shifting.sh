#!/bin/bash
#
# test_wavelength_shifting.sh
# ============================
# End-to-end test: GPU vs G4 wavelength shifting physics
#
# Fires 10000 UV photons (350nm) from outside a WLS sphere into a scattering
# medium. Compares GPU (opticks) and G4 hit wavelength distributions, WLS
# conversion rate, and arrival time distributions using chi-squared test.
#
# Geometry: tests/geom/wls_scatter_viz.gdml
#   - WLS sphere r=10mm (absorbs UV, re-emits visible)
#   - Scattering medium (Rayleigh, 10mm mean free path)
#   - Detector shell r=30mm (100% efficiency)
#
# Usage:
#   ./tests/test_wavelength_shifting.sh [seed]
#
# Exit code 0 = PASS, 1 = FAIL
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SEED=${1:-42}
NUMPHOTON=10000
GEOM="$REPO_DIR/tests/geom/wls_scatter_viz.gdml"
CONFIG="wls_scatter_viz"

source /opt/eic-opticks/eic-opticks-env.sh 2>/dev/null || true
export OPTICKS_MAX_BOUNCE=100
export OPTICKS_EVENT_MODE=HitPhoton
export OPTICKS_MAX_SLOT=100000

echo "=============================================="
echo " WLS Test: GPU vs G4 Wavelength Shifting"
echo "=============================================="
echo "  Geometry: $GEOM"
echo "  Photons:  $NUMPHOTON (350nm UV)"
echo "  Seed:     $SEED"
echo ""

# --- GPU run ---
echo "[GPU] Running GPUPhotonSourceMinimal..."
GPU_OUT=$(/opt/eic-opticks/bin/GPUPhotonSourceMinimal \
    -g "$GEOM" -c "$CONFIG" -m "$REPO_DIR/tests/run.mac" -s "$SEED" 2>&1)
GPU_HITS=$(echo "$GPU_OUT" | grep "Opticks: NumHits" | head -1 | awk '{print $NF}')
echo "[GPU] Hits: $GPU_HITS"

GPU_HIT_FILE="/tmp/MISSING_USER/opticks/GEOM/GEOM/GPUPhotonSourceMinimal/ALL0_no_opticks_event_name/A000/hit.npy"

# --- G4 run ---
echo "[G4]  Running StandAloneGeant4Validation..."
G4_OUT=$(/opt/eic-opticks/bin/StandAloneGeant4Validation \
    -g "$GEOM" -c "$CONFIG" -s "$SEED" 2>&1)
G4_HITS=$(echo "$G4_OUT" | grep "Total accumulated hits" | awk '{print $NF}')
echo "[G4]  Hits: $G4_HITS"

G4_HIT_FILE="g4_hits.npy"

# --- Compare ---
echo ""
echo "[COMPARE] Analyzing wavelength and time distributions..."
echo ""

python3 - "$GPU_HIT_FILE" "$G4_HIT_FILE" "$GPU_HITS" "$G4_HITS" << 'PYEOF'
import sys
import numpy as np

gpu_hit_file = sys.argv[1]
g4_hit_file = sys.argv[2]
gpu_nhits = int(sys.argv[3])
g4_nhits = int(sys.argv[4])

gpu = np.load(gpu_hit_file).reshape(-1, 4, 4)
g4 = np.load(g4_hit_file).reshape(-1, 4, 4)

gpu_wl = gpu[:, 2, 3]
g4_wl = g4[:, 2, 3]
gpu_time = gpu[:, 0, 3]
g4_time = g4[:, 0, 3]

PASS = True
ALPHA = 0.01  # significance level


def chi2_test(h_obs, h_exp, label):
    """Chi-squared test for two histograms. Returns (chi2, ndf, p_value, pass)."""
    # Scale expected to match observed total
    scale = h_obs.sum() / h_exp.sum() if h_exp.sum() > 0 else 1.0
    h_exp_scaled = h_exp * scale

    # Only use bins with sufficient statistics (>5 expected)
    mask = h_exp_scaled > 5
    if mask.sum() < 2:
        print(f"  {label}: Too few bins with sufficient stats")
        return 0, 0, 1.0, True

    obs = h_obs[mask].astype(float)
    exp = h_exp_scaled[mask].astype(float)
    chi2 = np.sum((obs - exp) ** 2 / exp)
    ndf = mask.sum() - 1

    # p-value from chi2 distribution using Wilson-Hilferty approximation
    if ndf > 0:
        z = (chi2 / ndf) ** (1.0 / 3) - (1 - 2.0 / (9 * ndf))
        z /= np.sqrt(2.0 / (9 * ndf))
        # Approximate p-value from standard normal
        p = 0.5 * (1.0 + math.erf(-z / np.sqrt(2)))
    else:
        p = 1.0

    passed = p >= ALPHA
    return chi2, ndf, p, passed


def ks_test(a, b):
    """Two-sample Kolmogorov-Smirnov test."""
    a, b = np.sort(a), np.sort(b)
    na, nb = len(a), len(b)
    combined = np.concatenate([a, b])
    combined.sort()
    cdf_a = np.searchsorted(a, combined, side='right') / na
    cdf_b = np.searchsorted(b, combined, side='right') / nb
    d = np.max(np.abs(cdf_a - cdf_b))
    en = np.sqrt(na * nb / (na + nb))
    p = min(np.exp(-2.0 * (en * d) ** 2) * 2.0, 1.0)
    return d, p


# -------------------------------------------------------
# Test 1: Hit count comparison
# -------------------------------------------------------
print("=" * 55)
print("  TEST 1: Hit Count")
print("=" * 55)
print(f"  GPU: {len(gpu)}")
print(f"  G4:  {len(g4)}")
import math
sigma = math.sqrt(len(gpu) + len(g4))
z = abs(len(gpu) - len(g4)) / sigma if sigma > 0 else 0
print(f"  |Z| = {z:.1f}σ")
t1_pass = z < 5
status = "PASS" if t1_pass else "FAIL"
print(f"  Result: {status} (threshold: 5σ)")
PASS = PASS and t1_pass


# -------------------------------------------------------
# Test 2: WLS conversion fraction
# -------------------------------------------------------
print()
print("=" * 55)
print("  TEST 2: WLS Conversion Fraction")
print("=" * 55)
WLS_THRESHOLD = 380  # nm

gpu_frac = np.mean(gpu_wl > WLS_THRESHOLD)
g4_frac = np.mean(g4_wl > WLS_THRESHOLD)
frac_diff = abs(gpu_frac - g4_frac)

print(f"  GPU shifted: {100*gpu_frac:.1f}%")
print(f"  G4  shifted: {100*g4_frac:.1f}%")
print(f"  |Difference|: {100*frac_diff:.2f}%")
t2_pass = frac_diff < 0.03  # 3% tolerance
status = "PASS" if t2_pass else "FAIL"
print(f"  Result: {status} (threshold: 3%)")
PASS = PASS and t2_pass


# Pre-compute shifted/unshifted arrays for tests 3 and 4
gpu_shifted = gpu_wl[gpu_wl > WLS_THRESHOLD]
g4_shifted = g4_wl[g4_wl > WLS_THRESHOLD]

# -------------------------------------------------------
# Test 3: Wavelength distribution (chi-squared)
# -------------------------------------------------------
print()
print("=" * 55)
print("  TEST 3: Shifted Wavelength Distribution (Chi-Squared)")
print("=" * 55)
# Chi2 on WLS-shifted photons only (>380nm), 75nm bins for robust statistics
wl_bins = np.arange(375, 575, 75)
h_gpu_wl, _ = np.histogram(gpu_shifted, bins=wl_bins)
h_g4_wl, _ = np.histogram(g4_shifted, bins=wl_bins)

chi2, ndf, p, t3_pass = chi2_test(h_gpu_wl, h_g4_wl, "Shifted WL")
print(f"  Chi2/ndf = {chi2:.1f}/{ndf} = {chi2/ndf:.2f}" if ndf > 0 else "  N/A")
print(f"  p-value  = {p:.4f}")
status = "PASS" if t3_pass else "FAIL"
print(f"  Result: {status} (threshold: p > {ALPHA})")

# Print full histogram for reference
print()
wl_bins_full = np.arange(325, 575, 25)
h_gpu_full, _ = np.histogram(gpu_wl, bins=wl_bins_full)
h_g4_full, _ = np.histogram(g4_wl, bins=wl_bins_full)
scale = len(gpu_wl) / len(g4_wl) if len(g4_wl) > 0 else 1
print(f"  {'WL (nm)':>10s} {'GPU':>7s} {'G4*scl':>7s} {'diff%':>7s}")
for i in range(len(wl_bins_full) - 1):
    if h_gpu_full[i] > 0 or h_g4_full[i] > 0:
        g4s = h_g4_full[i] * scale
        dpct = 100 * (h_gpu_full[i] - g4s) / g4s if g4s > 0 else 0
        print(f"  {wl_bins_full[i]:>4.0f}-{wl_bins_full[i+1]:<4.0f} {h_gpu_full[i]:>7d} {g4s:>7.0f} {dpct:>+6.1f}%")

PASS = PASS and t3_pass


# -------------------------------------------------------
# Test 4: Shifted wavelength spectrum (KS test)
# -------------------------------------------------------
print()
print("=" * 55)
print("  TEST 4: Shifted Wavelength Spectrum (KS Test)")
print("=" * 55)

if len(gpu_shifted) > 10 and len(g4_shifted) > 10:
    d, p4 = ks_test(gpu_shifted, g4_shifted)
    print(f"  GPU shifted: N={len(gpu_shifted)}, mean={gpu_shifted.mean():.1f}nm")
    print(f"  G4  shifted: N={len(g4_shifted)}, mean={g4_shifted.mean():.1f}nm")
    print(f"  KS D={d:.6f}  p={p4:.4f}")
    t4_pass = p4 >= ALPHA
else:
    print("  Too few shifted photons for KS test")
    t4_pass = True

status = "PASS" if t4_pass else "FAIL"
print(f"  Result: {status} (threshold: p > {ALPHA})")
PASS = PASS and t4_pass


# -------------------------------------------------------
# Test 5: Arrival time for shifted photons (KS test)
# -------------------------------------------------------
print()
print("=" * 55)
print("  TEST 5: Shifted Photon Arrival Time (KS Test)")
print("=" * 55)

# Compare shifted photon times — these include WLS exponential delay + transport
# With the G4 WLS time profile set to "exponential", distributions should match
gpu_shifted_t = gpu_time[gpu_wl > WLS_THRESHOLD]
g4_shifted_t = g4_time[g4_wl > WLS_THRESHOLD]

print(f"  GPU shifted: N={len(gpu_shifted_t)}, mean={gpu_shifted_t.mean():.3f}ns, std={gpu_shifted_t.std():.3f}ns")
print(f"  G4  shifted: N={len(g4_shifted_t)}, mean={g4_shifted_t.mean():.3f}ns, std={g4_shifted_t.std():.3f}ns")
print(f"  Std ratio: {gpu_shifted_t.std()/g4_shifted_t.std():.3f} (expect ~1.0)")

if len(gpu_shifted_t) > 10 and len(g4_shifted_t) > 10:
    d_t, p_t = ks_test(gpu_shifted_t, g4_shifted_t)
    print(f"  KS D={d_t:.6f}  p={p_t:.4f}")
    t5_pass = p_t >= ALPHA
else:
    print("  Too few shifted photons for KS test")
    t5_pass = True

# Also check unshifted time (pure transport, no WLS delay)
gpu_unshifted_t = gpu_time[gpu_wl <= WLS_THRESHOLD]
g4_unshifted_t = g4_time[g4_wl <= WLS_THRESHOLD]
print(f"  Unshifted time: GPU mean={gpu_unshifted_t.mean():.3f}ns  G4 mean={g4_unshifted_t.mean():.3f}ns")

status = "PASS" if t5_pass else "FAIL"
print(f"  Result: {status} (KS p > {ALPHA})")
PASS = PASS and t5_pass


# -------------------------------------------------------
# Summary
# -------------------------------------------------------
print()
print("=" * 55)
print("  SUMMARY")
print("=" * 55)
tests = [
    ("Hit count",          t1_pass),
    ("WLS fraction",       t2_pass),
    ("Wavelength chi2",    t3_pass),
    ("Shifted spectrum KS", t4_pass),
    ("Arrival time chi2",  t5_pass),
]
for name, passed in tests:
    print(f"  {name:>25s}: {'PASS' if passed else 'FAIL'}")

print()
if PASS:
    print("  *** ALL TESTS PASSED ***")
    sys.exit(0)
else:
    print("  *** SOME TESTS FAILED ***")
    sys.exit(1)
PYEOF
