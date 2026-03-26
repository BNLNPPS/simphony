#!/usr/bin/env python
"""
photon_table.py : Debug analysis of opticks GPU simulation output
=================================================================

Reads photon.npy, hit.npy, inphoton.npy, record.npy, seq.npy from
an opticks event folder and prints summary tables for debugging.

Requires OPTICKS_EVENT_MODE=HitPhoton (or DebugLite/DebugHeavy) so
that photon and record arrays are actually saved to disk.

Usage::

    python ana/photon_table.py /tmp/MISSING_USER/opticks/GEOM/GEOM/GPUPhotonSourceMinimal/ALL0_no_opticks_event_name/A000

    # or just the parent (auto-selects A000):
    python ana/photon_table.py /tmp/MISSING_USER/opticks/GEOM/GEOM/GPUPhotonSourceMinimal/ALL0_no_opticks_event_name

    # show per-photon detail for first 20 photons:
    python ana/photon_table.py <path> --detail 20

    # show step-by-step record for specific photons:
    python ana/photon_table.py <path> --trace 0,227,235

    # filter by terminal flag:
    python ana/photon_table.py <path> --flag BOUNDARY_REFLECT

    # show all non-hit (absorbed/lost) photons:
    python ana/photon_table.py <path> --lost
"""
import sys
import os
import argparse
import numpy as np

# --- Flag definitions from OpticksPhoton.h ---
FLAG_ENUM = {
    0x00001: "CERENKOV",
    0x00002: "SCINTILLATION",
    0x00004: "TORCH",
    0x00008: "BULK_ABSORB",
    0x00010: "BULK_REEMIT",
    0x00020: "BULK_SCATTER",
    0x00040: "SURFACE_DETECT",
    0x00080: "SURFACE_ABSORB",
    0x00100: "SURFACE_DREFLECT",
    0x00200: "SURFACE_SREFLECT",
    0x00400: "BOUNDARY_REFLECT",
    0x00800: "BOUNDARY_TRANSMIT",
    0x01000: "NAN_ABORT",
    0x02000: "EFFICIENCY_COLLECT",
    0x04000: "EFFICIENCY_CULL",
    0x08000: "MISS",
    0x10000: "__NATURAL",
    0x20000: "__MACHINERY",
    0x40000: "__EMITSOURCE",
    0x80000: "PRIMARYSOURCE",
    0x100000: "GENSTEPSOURCE",
    0x200000: "DEFER_FSTRACKINFO",
}

FLAG_ABBREV = {
    0x00001: "CK",
    0x00002: "SI",
    0x00004: "TO",
    0x00008: "AB",
    0x00010: "RE",
    0x00020: "SC",
    0x00040: "SD",
    0x00080: "SA",
    0x00100: "DR",
    0x00200: "SR",
    0x00400: "BR",
    0x00800: "BT",
    0x01000: "NA",
    0x02000: "EC",
    0x04000: "EL",
    0x08000: "MI",
    0x10000: "Nat",
    0x20000: "Mac",
    0x40000: "Emi",
    0x80000: "PS",
    0x100000: "GS",
    0x200000: "DF",
}

# Terminal flags that indicate a detected photon.
# Default hitmask is SD, but CustomART uses EC instead.
# The script reads the actual hitmask from event metadata when available.
HIT_FLAGS = {0x0040, 0x2000}  # SURFACE_DETECT, EFFICIENCY_COLLECT

# FFS (find-first-set) value to flag bit, used by seq nibble decoding
FFS_TO_FLAG = {i + 1: 1 << i for i in range(16)}


def flag_name(f):
    return FLAG_ENUM.get(f, f"0x{f:04x}")


def flag_abbrev(f):
    return FLAG_ABBREV.get(f, f"?{f:x}")


def flagmask_str(fm):
    """Decode cumulative flagmask into abbreviated flag names."""
    parts = []
    for bit in range(22):
        f = 1 << bit
        if fm & f:
            parts.append(flag_abbrev(f))
    return "|".join(parts)


def decode_seq_history(seqhis):
    """Decode seq nibbles into list of (flag_value, abbrev) tuples."""
    steps = []
    for slot in range(32):
        iseq = slot // 16
        shift = 4 * (slot - iseq * 16)
        if iseq == 0:
            nibble = (seqhis[0] >> shift) & 0xF
        else:
            nibble = (seqhis[1] >> shift) & 0xF
        if nibble == 0:
            break
        f = FFS_TO_FLAG.get(nibble, 0)
        steps.append((f, flag_abbrev(f)))
    return steps


def extract_photon_fields(photon):
    """Extract decoded fields from photon array (N, 4, 4) float32."""
    q3 = photon[:, 3, :].view(np.uint32)
    obf = q3[:, 0]  # orient_boundary_flag
    flag = obf & 0xFFFF
    boundary = (obf >> 16) & 0x7FFF
    orient = (obf >> 31).astype(np.int8)
    orient = np.where(orient, -1, 1)
    flagmask = q3[:, 3]
    identity = q3[:, 1]
    index = q3[:, 2] & 0x7FFFFFFF

    pos = photon[:, 0, :3]
    time = photon[:, 0, 3]
    mom = photon[:, 1, :3]
    pol = photon[:, 2, :3]
    wavelength = photon[:, 2, 3]

    return dict(
        pos=pos, time=time, mom=mom, pol=pol, wavelength=wavelength,
        flag=flag, boundary=boundary, orient=orient,
        flagmask=flagmask, identity=identity, index=index,
    )


def count_record_steps(record):
    """Count non-zero steps per photon in record array (N, maxstep, 4, 4)."""
    n, maxstep = record.shape[:2]
    # A step is unused if all 16 floats are zero
    step_nonzero = np.any(record.reshape(n, maxstep, -1) != 0, axis=2)
    # Assuming used steps are contiguous from the start, the number of
    # steps per photon is just the sum of non-zero-step indicators.
    nsteps = step_nonzero.sum(axis=1).astype(int)
    return nsteps


def load_event(path):
    """Load arrays from an opticks event folder. Returns dict of arrays."""
    arrays = {}
    names = ["photon", "hit", "inphoton", "record", "seq", "genstep", "domain"]
    for name in names:
        fpath = os.path.join(path, f"{name}.npy")
        if os.path.exists(fpath):
            arrays[name] = np.load(fpath)
    return arrays


def read_hitmask(path):
    """Read hitmask from event metadata. Returns int or None."""
    meta_path = os.path.join(path, "NPFold_meta.txt")
    if not os.path.exists(meta_path):
        return None
    with open(meta_path) as f:
        for line in f:
            if line.startswith("hitmask:"):
                parts = line.split(":", 1)
                if len(parts) < 2:
                    return None
                value = parts[1].strip()
                if not value:
                    return None
                try:
                    # Use base=0 to allow decimal ("64") and hex ("0x40") encodings.
                    return int(value, 0)
                except ValueError:
                    return None
    return None


def resolve_event_path(path):
    """Resolve event folder path, auto-selecting A000 if needed."""
    if os.path.exists(os.path.join(path, "photon.npy")):
        return path
    a000 = os.path.join(path, "A000")
    if os.path.exists(os.path.join(a000, "photon.npy")):
        return a000
    # Try to find any subfolder with photon.npy
    if os.path.isdir(path):
        for d in sorted(os.listdir(path)):
            dp = os.path.join(path, d)
            if os.path.isdir(dp) and os.path.exists(os.path.join(dp, "photon.npy")):
                return dp
    return path


def print_overview(arrays):
    """Print overview table of loaded arrays."""
    print("=" * 70)
    print("LOADED ARRAYS")
    print("=" * 70)
    for name, arr in sorted(arrays.items()):
        print(f"  {name:12s}  shape={str(arr.shape):24s}  dtype={arr.dtype}")
    print()


def print_outcome_table(pf, n_photons, n_hits, hitmask, arrays):
    """Print table of photon outcomes by terminal flag."""
    print("=" * 70)
    print("PHOTON OUTCOMES (by terminal flag)")
    print("=" * 70)

    hm_str = flagmask_str(hitmask) if hitmask else "unknown"
    print(f"  Hitmask: {hm_str} (0x{hitmask:x})" if hitmask else "  Hitmask: not found in metadata")

    flag_vals, flag_counts = np.unique(pf["flag"], return_counts=True)
    order = np.argsort(-flag_counts)

    print(f"\n  {'Flag':<22s} {'Count':>7s} {'%':>7s}  {'Boundary indices'}")
    print(f"  {'-'*22} {'-'*7} {'-'*7}  {'-'*30}")
    for idx in order:
        f = flag_vals[idx]
        c = flag_counts[idx]
        mask = pf["flag"] == f
        bnd_vals = np.unique(pf["boundary"][mask])
        bnd_str = ", ".join(str(b) for b in bnd_vals)
        pct = 100.0 * c / n_photons
        print(f"  {flag_name(f):<22s} {c:7d} {pct:6.1f}%  bnd=[{bnd_str}]")

    hit_rate = 100.0 * n_hits / n_photons if n_photons > 0 else 0.0
    print(f"\n  Total photons: {n_photons}   Hits: {n_hits}   "
          f"Lost: {n_photons - n_hits}   Hit rate: {hit_rate:.1f}%")

    # Report MaxBounce-truncated photons
    if "record" in arrays:
        record = arrays["record"]
        maxstep = record.shape[1]
        nsteps = count_record_steps(record)
        n_truncated = int(np.sum(nsteps == maxstep))
        if n_truncated > 0:
            print(f"  Truncated at MaxBounce ({maxstep} steps): {n_truncated}")
    print()


def print_flagmask_table(pf, n_photons):
    """Print table of unique cumulative flagmask histories."""
    print("=" * 70)
    print("PHOTON HISTORIES (by cumulative flagmask)")
    print("=" * 70)

    fm_vals, fm_counts = np.unique(pf["flagmask"], return_counts=True)
    order = np.argsort(-fm_counts)

    print(f"  {'Flagmask':<40s} {'Count':>7s} {'%':>7s}")
    print(f"  {'-'*40} {'-'*7} {'-'*7}")
    for idx in order:
        fm = fm_vals[idx]
        c = fm_counts[idx]
        pct = 100.0 * c / n_photons
        desc = flagmask_str(fm)
        print(f"  {desc:<40s} {c:7d} {pct:6.1f}%")
    print()


def print_wavelength_table(pf, arrays):
    """Print wavelength shift analysis."""
    print("=" * 70)
    print("WAVELENGTH ANALYSIS")
    print("=" * 70)

    wl = pf["wavelength"]
    print(f"  Final photons:  min={wl.min():.1f}  max={wl.max():.1f}  "
          f"mean={wl.mean():.1f}  std={wl.std():.1f} nm")

    if "inphoton" in arrays:
        inp_wl = arrays["inphoton"][:, 2, 3]
        print(f"  Input photons:  min={inp_wl.min():.1f}  max={inp_wl.max():.1f}  "
              f"mean={inp_wl.mean():.1f}  std={inp_wl.std():.1f} nm")

        shifted = np.sum(np.abs(wl - inp_wl) > 0.1)
        print(f"  Wavelength shifted: {shifted}/{len(wl)}")

    if "hit" in arrays and len(arrays["hit"]) > 0:
        hit_wl = arrays["hit"][:, 2, 3]
        print(f"  Hit wavelength: min={hit_wl.min():.1f}  max={hit_wl.max():.1f}  "
              f"mean={hit_wl.mean():.1f}  std={hit_wl.std():.1f} nm")
    elif "hit" in arrays:
        print(f"  Hit wavelength: no hits")

    # Energy conservation
    hc = 1239.84193  # eV·nm
    if "inphoton" in arrays:
        inp_wl = arrays["inphoton"][:, 2, 3]
        inp_E = hc / inp_wl
        out_E = hc / wl
        violations = np.sum(out_E > inp_E + 0.001)
        print(f"  Energy conservation violations: {violations}")
    print()


def print_position_table(pf):
    """Print position statistics."""
    print("=" * 70)
    print("POSITION / TIME")
    print("=" * 70)

    pos = pf["pos"]
    r = np.sqrt(np.sum(pos ** 2, axis=1))
    t = pf["time"]

    print(f"  Radius:  min={r.min():.2f}  max={r.max():.2f}  mean={r.mean():.2f} mm")
    print(f"  Time:    min={t.min():.3f}  max={t.max():.3f}  mean={t.mean():.3f} ns")
    print()


def print_seq_table(arrays, n_show=15):
    """Print sequence history table from seq.npy."""
    if "seq" not in arrays:
        return

    seq = arrays["seq"]
    n = seq.shape[0]

    print("=" * 70)
    print(f"STEP SEQUENCE HISTORIES (top {n_show})")
    print("=" * 70)

    # seqhis is seq[:, 0, :] as uint64
    seqhis = seq[:, 0, :]

    # Build string representation for each photon
    labels = []
    for i in range(n):
        steps = decode_seq_history(seqhis[i])
        label = " ".join(abbr for _, abbr in steps)
        labels.append(label)

    # Count unique sequences
    unique_labels, inverse, counts = np.unique(
        labels, return_inverse=True, return_counts=True
    )
    order = np.argsort(-counts)

    print(f"  {'#':>4s}  {'Count':>7s} {'%':>7s}  {'Sequence'}")
    print(f"  {'-'*4}  {'-'*7} {'-'*7}  {'-'*40}")
    for rank, idx in enumerate(order[:n_show]):
        c = counts[idx]
        pct = 100.0 * c / n
        print(f"  {rank:4d}  {c:7d} {pct:6.1f}%  {unique_labels[idx]}")

    if len(order) > n_show:
        print(f"  ... ({len(order)} unique sequences total)")
    print()


def print_record_steps_table(arrays):
    """Print step count distribution from record.npy."""
    if "record" not in arrays:
        return

    record = arrays["record"]
    nsteps = count_record_steps(record)

    print("=" * 70)
    print("RECORD STEP COUNTS")
    print("=" * 70)

    step_vals, step_counts = np.unique(nsteps, return_counts=True)
    maxstep = record.shape[1]

    print(f"  {'Steps':>6s}  {'Count':>7s} {'%':>7s}  {'Note'}")
    print(f"  {'-'*6}  {'-'*7} {'-'*7}  {'-'*20}")
    for sv, sc in zip(step_vals, step_counts):
        pct = 100.0 * sc / len(nsteps)
        note = " <-- max (truncated)" if sv == maxstep else ""
        print(f"  {sv:6d}  {sc:7d} {pct:6.1f}%{note}")

    print(f"\n  Mean steps: {nsteps.mean():.1f}  Max allowed: {maxstep}")
    print()


def print_photon_detail(arrays, pf, indices):
    """Print detailed per-photon info for given indices."""
    print("=" * 70)
    print("PHOTON DETAIL")
    print("=" * 70)

    record = arrays.get("record")
    seq = arrays.get("seq")
    n_photons = len(pf["flag"])

    for i in indices:
        if i >= n_photons:
            print(f"\n  photon[{i}]: index out of range (max {n_photons - 1})")
            continue

        pos = pf["pos"][i]
        wl = pf["wavelength"][i]
        t = pf["time"][i]
        f = pf["flag"][i]
        bnd = pf["boundary"][i]
        fm = pf["flagmask"][i]

        r = np.sqrt(np.sum(pos ** 2))

        print(f"\n  photon[{i}]")
        print(f"    Final: pos=({pos[0]:.2f}, {pos[1]:.2f}, {pos[2]:.2f}) r={r:.2f}mm"
              f"  t={t:.3f}ns  wl={wl:.1f}nm")
        print(f"    Flag:  {flag_name(f)}  bnd={bnd}  flagmask={flagmask_str(fm)}")

        if seq is not None:
            steps = decode_seq_history(seq[i, 0, :])
            seq_str = " ".join(abbr for _, abbr in steps)
            print(f"    Seq:   {seq_str}")

        if record is not None:
            rec = record[i]
            maxstep = rec.shape[0]
            print(f"    {'Step':>4s}  {'Flag':<18s}  {'Bnd':>3s}  {'Position (x,y,z)':^30s}"
                  f"  {'r':>8s}  {'Time':>8s}  {'WL':>7s}")
            print(f"    {'-'*4}  {'-'*18}  {'-'*3}  {'-'*30}  {'-'*8}  {'-'*8}  {'-'*7}")

            for s in range(maxstep):
                if np.all(rec[s] == 0):
                    break
                sq3 = rec[s, 3, :].view(np.uint32)
                sf = sq3[0] & 0xFFFF
                sb = (sq3[0] >> 16) & 0x7FFF
                spos = rec[s, 0, :3]
                st = rec[s, 0, 3]
                swl = rec[s, 2, 3]
                sr = np.sqrt(np.sum(spos ** 2))
                print(f"    {s:4d}  {flag_name(sf):<18s}  {sb:3d}"
                      f"  ({spos[0]:9.2f},{spos[1]:9.2f},{spos[2]:9.2f})"
                      f"  {sr:8.2f}  {st:8.3f}  {swl:7.1f}")
    print()


def print_lost_photons(arrays, pf, hitmask):
    """Print detail for all non-detected photons.

    A photon is "lost" if its terminal flag is not in the hit set.
    The hitmask is read from event metadata (default: SURFACE_DETECT).
    With CustomART PMTs the hitmask may be EFFICIENCY_COLLECT instead.
    """
    # A photon is a hit if (flagmask & hitmask) == hitmask.
    # For --lost we check the terminal flag against known hit flags,
    # AND also include photons whose flagmask doesn't satisfy the hitmask.
    if hitmask is not None:
        lost_mask = (pf["flagmask"] & hitmask) != hitmask
    else:
        # Fallback: terminal flag not in any known hit flag
        lost_mask = np.ones(len(pf["flag"]), dtype=bool)
        for hf in HIT_FLAGS:
            lost_mask &= pf["flag"] != hf

    lost_idx = np.where(lost_mask)[0]

    if len(lost_idx) == 0:
        print("No lost photons — all photons were detected.\n")
        return

    print("=" * 70)
    hm_str = flagmask_str(hitmask) if hitmask else "SD|EC"
    print(f"LOST PHOTONS ({len(lost_idx)} non-detected, hitmask={hm_str})")
    print("=" * 70)

    # Summary by terminal flag
    lost_flags = pf["flag"][lost_mask]
    fvals, fcounts = np.unique(lost_flags, return_counts=True)
    for fv, fc in zip(fvals, fcounts):
        print(f"  {flag_name(fv):<22s}: {fc}")

    # Flag photons stuck at MaxBounce
    if "record" in arrays:
        record = arrays["record"]
        maxstep = record.shape[1]
        nsteps = count_record_steps(record)
        lost_at_max = np.sum(nsteps[lost_idx] == maxstep)
        if lost_at_max > 0:
            print(f"\n  Note: {lost_at_max} lost photon(s) hit the {maxstep}-step"
                  f" bounce limit (trapped by total internal reflection?)")
    print()

    print_photon_detail(arrays, pf, lost_idx)


def expected_output_path(executable="GPUPhotonSourceMinimal"):
    """Compute the expected opticks output path from environment variables.

    Opticks saves output to:
        $TMP/GEOM/$GEOM/$ExecutableName/ALL0_no_opticks_event_name/A000/

    Where:
        $TMP   defaults to /tmp/$USER/opticks
        $GEOM  defaults to GEOM
    """
    tmp = os.environ.get("TMP")
    if tmp is None:
        user = os.environ.get("USER", "MISSING_USER")
        tmp = f"/tmp/{user}/opticks"
    geom = os.environ.get("GEOM", "GEOM")
    return os.path.join(tmp, "GEOM", geom, executable,
                        "ALL0_no_opticks_event_name", "A000")


def print_output_path_help():
    """Print where opticks writes output files."""
    print("OUTPUT FILE LOCATION")
    print("=" * 70)
    print()
    print("Opticks saves .npy arrays to:")
    print()
    print("    $TMP/GEOM/$GEOM/<ExecutableName>/ALL0_no_opticks_event_name/A000/")
    print()
    tmp = os.environ.get("TMP")
    if tmp is None:
        user = os.environ.get("USER", "MISSING_USER")
        tmp = f"/tmp/{user}/opticks"
        print(f"    $TMP  is not set, defaults to: {tmp}")
    else:
        print(f"    $TMP  = {tmp}")
    geom = os.environ.get("GEOM")
    if geom is None:
        print(f"    $GEOM is not set, defaults to: GEOM")
        geom = "GEOM"
    else:
        print(f"    $GEOM = {geom}")
    print()
    print("For common executables, the expected paths are:")
    print()
    for exe in ("GPUPhotonSourceMinimal", "GPUPhotonSource", "GPUPhotonFileSource"):
        p = expected_output_path(exe)
        print(f"    {exe}:")
        print(f"        {p}")
    print()


def check_event_mode():
    """Check that OPTICKS_EVENT_MODE is set to a mode that saves output arrays."""
    valid_modes = ("HitPhoton", "DebugLite", "DebugHeavy")
    mode = os.environ.get("OPTICKS_EVENT_MODE")
    if mode is None:
        print("ERROR: OPTICKS_EVENT_MODE environment variable is not set.")
        print()
        print("This script requires photon/record arrays that are only saved")
        print("when OPTICKS_EVENT_MODE is set to one of:")
        print()
        for m in valid_modes:
            print(f"    export OPTICKS_EVENT_MODE={m}")
        print()
        print("The default mode (Minimal) gathers hits into memory but does")
        print("NOT save photon.npy, record.npy, or other arrays to disk.")
        print()
        print("Set the variable before running the simulation, e.g.:")
        print()
        print("    OPTICKS_EVENT_MODE=HitPhoton GPUPhotonSourceMinimal -g geo.gdml -c cfg -m run.mac")
        print()
        print_output_path_help()
        sys.exit(1)
    if mode not in valid_modes:
        print(f"ERROR: OPTICKS_EVENT_MODE={mode} does not save full output arrays.")
        print()
        print("This script requires OPTICKS_EVENT_MODE set to one of:")
        print()
        for m in valid_modes:
            print(f"    export OPTICKS_EVENT_MODE={m}")
        print()
        print(f"Current value '{mode}' may not save photon.npy or record.npy.")
        print()
        print_output_path_help()
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Opticks GPU simulation output analysis",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("path", help="Path to opticks event folder (containing photon.npy)")
    parser.add_argument("--detail", type=int, metavar="N", default=0,
                        help="Show per-photon detail for first N photons")
    parser.add_argument("--trace", type=str, metavar="I,J,...", default=None,
                        help="Show step-by-step record for specific photon indices")
    parser.add_argument("--flag", type=str, metavar="FLAG", default=None,
                        help="Filter: only show photons with this terminal flag (e.g. BULK_ABSORB)")
    parser.add_argument("--lost", action="store_true",
                        help="Show all non-detected (lost) photons with step traces")
    parser.add_argument("--seq-top", type=int, metavar="N", default=15,
                        help="Number of top sequence histories to show (default: 15)")

    args = parser.parse_args()

    check_event_mode()

    path = resolve_event_path(args.path)
    if not os.path.exists(os.path.join(path, "photon.npy")):
        print(f"Error: photon.npy not found in {path}")
        print()
        print("Make sure the simulation was run with OPTICKS_EVENT_MODE=HitPhoton")
        print("(or DebugLite/DebugHeavy) so that output arrays are saved to disk.")
        print()
        print_output_path_help()
        sys.exit(1)

    arrays = load_event(path)
    hitmask = read_hitmask(path)
    print(f"\nEvent folder: {path}\n")
    print_overview(arrays)

    photon = arrays["photon"]
    pf = extract_photon_fields(photon)
    n_photons = len(pf["flag"])
    n_hits = len(arrays["hit"]) if "hit" in arrays else 0

    # Apply flag filter if requested
    if args.flag:
        # Resolve flag name to value
        flag_val = None
        for v, name in FLAG_ENUM.items():
            if name == args.flag.upper():
                flag_val = v
                break
        if flag_val is None:
            print(f"Error: unknown flag '{args.flag}'. Available flags:")
            for v, name in sorted(FLAG_ENUM.items()):
                print(f"  {name}")
            sys.exit(1)

        mask = pf["flag"] == flag_val
        indices = np.where(mask)[0]
        print(f"Filter: terminal flag = {args.flag.upper()} ({len(indices)} photons)\n")
        print_photon_detail(arrays, pf, indices)
        return

    # Standard tables
    print_outcome_table(pf, n_photons, n_hits, hitmask, arrays)
    print_flagmask_table(pf, n_photons)
    print_wavelength_table(pf, arrays)
    print_position_table(pf)
    print_seq_table(arrays, n_show=args.seq_top)
    print_record_steps_table(arrays)

    # Optional detail views
    if args.detail > 0:
        indices = list(range(min(args.detail, n_photons)))
        print_photon_detail(arrays, pf, indices)

    if args.trace:
        indices = [int(x.strip()) for x in args.trace.split(",")]
        print_photon_detail(arrays, pf, indices)

    if args.lost:
        print_lost_photons(arrays, pf, hitmask)


if __name__ == "__main__":
    main()
