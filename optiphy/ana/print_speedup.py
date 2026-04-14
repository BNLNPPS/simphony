"""
print_speedup.py
================
Format and print the GPU vs G4 speedup summary used by
benchmarks/benchmark_apex.sh after a single benchmark run.

Usage:
    python3 print_speedup.py <gpu_time> <g4_cpu> <g4_wall> <nphotons> <gpu_hits> <g4_hits>
"""

import sys


def main(argv):
    gpu = float(argv[1])
    g4_cpu = float(argv[2])
    g4_wall = float(argv[3])
    nphotons = int(argv[4])
    gpu_hits = int(argv[5])
    g4_hits = int(argv[6])
    hit_diff = (gpu_hits - g4_hits) / g4_hits * 100 if g4_hits > 0 else 0

    print()
    print(f'Photons:        {nphotons:>10,}')
    print(f'GPU sim time:   {gpu:>10.4f} s')
    print(f'G4 CPU time:    {g4_cpu:>10.2f} s')
    print(f'G4 wall time:   {g4_wall:>10.2f} s')
    print()
    print(f'Speedup (CPU):  {g4_cpu/gpu:>10.0f}x')
    print(f'Speedup (wall): {g4_wall/gpu:>10.0f}x')
    print()
    print(f'GPU rate:       {nphotons/gpu/1e6:>10.1f} M photons/s')
    print(f'G4 rate:        {nphotons/g4_cpu/1e3:>10.1f} k photons/s')
    print()
    print(f'GPU hits:       {gpu_hits:>10}')
    print(f'G4 hits:        {g4_hits:>10}')
    print(f'Hit diff:       {hit_diff:>+9.1f}%')


if __name__ == "__main__":
    main(sys.argv)
