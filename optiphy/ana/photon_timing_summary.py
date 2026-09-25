"""
photon_timing_summary.py
========================
Format and print the photon-only and wall-time speedup summary used by
examples/photontiming_geant4/photontimingandsteps.sh after the three
benchmark runs (G4 photon-only, EM baseline, GPU-accelerated) complete.

Usage:
    python3 photon_timing_summary.py <gpu_sim> <g4_cpu> <g4_base> <g4_wall> \
                                     <gpu_wall> <nphotons> <gpu_hits> <g4_hits>
"""

import sys


def main(argv):
    gpu_sim = float(argv[1])
    g4_cpu = float(argv[2])
    g4_base = float(argv[3])
    g4_wall = float(argv[4])
    gpu_wall = float(argv[5])
    nphotons = int(argv[6])
    gpu_hits = int(argv[7])
    g4_hits = int(argv[8])

    g4_photon_cpu = g4_cpu - g4_base
    hit_diff = (gpu_hits - g4_hits) / g4_hits * 100 if g4_hits > 0 else 0

    print()
    print(f'Photons:                    {nphotons:>12,}')
    print()
    print(f'--- Photon-only speedup ---')
    print(f'G4 photon CPU time:         {g4_photon_cpu:>12.2f} s')
    print(f'GPU sim time:               {gpu_sim:>12.4f} s')
    if gpu_sim > 0 and g4_photon_cpu > 0:
        print(f'Photon speedup:             {g4_photon_cpu/gpu_sim:>12.0f}x')
        print(f'GPU time/photon:            {gpu_sim/nphotons*1e9:>12.1f} ns')
        print(f'G4 time/photon (avg):       {g4_photon_cpu/nphotons*1e6:>12.1f} us')
    print()
    print(f'--- Wall time speedup ---')
    print(f'G4 wall (EM + photons):     {g4_wall:>12.2f} s')
    print(f'GPU wall (EM + GPU):        {gpu_wall:>12.4f} s')
    if gpu_wall > 0 and g4_wall > 0:
        print(f'Wall speedup:               {g4_wall/gpu_wall:>12.0f}x')
    print()
    print(f'GPU hits:                   {gpu_hits:>12}')
    print(f'G4 hits:                    {g4_hits:>12}')
    print(f'Hit diff:                   {hit_diff:>+11.1f}%')


if __name__ == "__main__":
    main(sys.argv)
