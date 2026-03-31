#!/usr/bin/env python3
"""Plot GPU photon paths colored by wavelength from record.npy.

Usage:
    python optiphy/ana/plot_photon_paths.py <event_dir> [photon_indices] [--output path.png]

Examples:
    # Plot first 10 hit photons
    python optiphy/ana/plot_photon_paths.py /tmp/$USER/opticks/GEOM/GEOM/GPUPhotonSourceMinimal/ALL0_no_opticks_event_name/A000

    # Plot specific photons by index
    python optiphy/ana/plot_photon_paths.py /tmp/$USER/opticks/.../A000 2,19,6

    # Custom output path
    python optiphy/ana/plot_photon_paths.py /tmp/$USER/opticks/.../A000 2,19,6 --output my_plot.png
"""
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import matplotlib.colors as mcolors
from matplotlib.cm import ScalarMappable


def wl_to_rgb(wl):
    """Convert wavelength (nm) to RGB tuple. Covers 300-780nm."""
    r = g = b = 0.0
    if 300 <= wl < 380:
        t = (wl - 300) / (380 - 300)
        r = 0.4 * (1 - t) + 0.5 * t
        g = 0
        b = 0.4 * (1 - t) + 1.0 * t
    elif 380 <= wl < 440:
        r = -(wl - 440) / (440 - 380); g = 0; b = 1
    elif 440 <= wl < 490:
        r = 0; g = (wl - 440) / (490 - 440); b = 1
    elif 490 <= wl < 510:
        r = 0; g = 1; b = -(wl - 510) / (510 - 490)
    elif 510 <= wl < 580:
        r = (wl - 510) / (580 - 510); g = 1; b = 0
    elif 580 <= wl < 645:
        r = 1; g = -(wl - 645) / (645 - 580); b = 0
    elif 645 <= wl <= 780:
        r = 1; g = 0; b = 0
    else:
        r = g = b = 0.3
    return (max(0, min(1, r)), max(0, min(1, g)), max(0, min(1, b)))


def get_steps(record, pidx):
    """Return number of valid steps for photon pidx."""
    rec_p = record[pidx]
    rf = rec_p.reshape(rec_p.shape[0], -1)
    return int(np.sum(np.any(rf != 0, axis=1)))


def plot_photon_paths(event_dir, photon_indices=None, output="photon_paths.png",
                      sphere_radii=None, title=None, lim=None):
    record = np.load(f"{event_dir}/record.npy")
    photon = np.load(f"{event_dir}/photon.npy")

    q3 = photon[:, 3, :].copy().view(np.uint32)
    flags = q3[:, 0] & 0xFFFF
    hit_idx = np.where(flags == 0x40)[0]

    if photon_indices is None:
        photon_indices = hit_idx[:10]

    fig = plt.figure(figsize=(12, 10))
    ax = fig.add_subplot(111, projection='3d')

    wl_min, wl_max = 800, 300
    for pidx in photon_indices:
        ns = get_steps(record, pidx)
        if ns < 2:
            continue
        rec_p = record[pidx]
        x = rec_p[:ns, 0, 0]
        y = rec_p[:ns, 0, 1]
        z = rec_p[:ns, 0, 2]
        wl = rec_p[:ns, 2, 3]

        wl_min = min(wl_min, wl.min())
        wl_max = max(wl_max, wl.max())

        for s in range(ns - 1):
            color = wl_to_rgb(float(wl[s]))
            ax.plot([x[s], x[s + 1]], [y[s], y[s + 1]], [z[s], z[s + 1]],
                    color=color, alpha=0.9, linewidth=2.5)

        ax.scatter(x[0], y[0], z[0], c=[wl_to_rgb(float(wl[0]))], s=60,
                   marker='o', edgecolors='black', linewidths=0.8, zorder=5)
        ax.scatter(x[-1], y[-1], z[-1], c='red', s=100, marker='*', zorder=5)

    # Draw spheres if requested
    if sphere_radii:
        u = np.linspace(0, 2 * np.pi, 60)
        v = np.linspace(0, np.pi, 30)
        sphere_colors = ['mediumpurple', 'lightgreen', 'lightyellow', 'lightcoral']
        sphere_alphas = [0.1, 0.05, 0.05, 0.05]
        for i, r in enumerate(sphere_radii):
            xs = r * np.outer(np.cos(u), np.sin(v))
            ys = r * np.outer(np.sin(u), np.sin(v))
            zs = r * np.outer(np.ones_like(u), np.cos(v))
            ci = min(i, len(sphere_colors) - 1)
            ax.plot_surface(xs, ys, zs, alpha=sphere_alphas[ci], color=sphere_colors[ci])

    # Wavelength colorbar
    wl_range = np.linspace(wl_min, wl_max, 256)
    colors = [wl_to_rgb(w) for w in wl_range]
    cmap = mcolors.ListedColormap(colors)
    norm = mcolors.Normalize(vmin=wl_min, vmax=wl_max)
    sm = ScalarMappable(cmap=cmap, norm=norm)
    sm.set_array([])
    plt.colorbar(sm, ax=ax, shrink=0.5, pad=0.08, label='Wavelength (nm)')

    ax.set_xlabel('X (mm)')
    ax.set_ylabel('Y (mm)')
    ax.set_zlabel('Z (mm)')
    if title:
        ax.set_title(title)
    if lim:
        ax.set_xlim(-lim, lim)
        ax.set_ylim(-lim, lim)
        ax.set_zlim(-lim, lim)
    ax.view_init(elev=20, azim=135)
    plt.tight_layout()
    plt.savefig(output, dpi=180)
    print(f"Saved {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("event_dir", help="Path to opticks event folder containing record.npy")
    parser.add_argument("indices", nargs='?', default=None,
                        help="Comma-separated photon indices (default: first 10 hits)")
    parser.add_argument("--output", "-o", default="photon_paths.png", help="Output image path")
    parser.add_argument("--spheres", default=None,
                        help="Comma-separated sphere radii to draw (e.g. 10,30)")
    parser.add_argument("--title", "-t", default=None, help="Plot title")
    parser.add_argument("--lim", type=float, default=None,
                        help="Axis limit in mm (symmetric)")
    args = parser.parse_args()

    indices = None
    if args.indices:
        indices = [int(x) for x in args.indices.split(',')]

    spheres = None
    if args.spheres:
        spheres = [float(x) for x in args.spheres.split(',')]

    plot_photon_paths(args.event_dir, indices, args.output,
                      sphere_radii=spheres, title=args.title, lim=args.lim)


if __name__ == "__main__":
    main()
