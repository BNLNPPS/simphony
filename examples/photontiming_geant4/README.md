# GPU vs G4 Optical Photon Benchmark

Measures the speedup of GPU (OptiX 7) optical photon propagation compared to
Geant4 CPU propagation on the APEX liquid argon scintillator tile detector.

## Quick Start

```bash
cd /workspaces/eic-opticks   # or wherever the repo is
./examples/benchmark_apex.sh  # single event (~250k photons)
./examples/benchmark_apex.sh 10  # 10 events (~2.5M photons)
```

Requires `apex.gdml` in the current directory and a working eic-opticks installation.

## What It Measures

The benchmark runs GPURaytrace three times to isolate each timing component:

| Run | G4 photons | GPU | What it measures |
|-----|-----------|-----|------------------|
| 1. `--skip-gpu` | Yes | No | G4 electron tracking + G4 photon propagation |
| 2. `setStackPhotons false` | No | No | G4 electron tracking only (baseline) |
| 3. Normal | Yes | Yes | GPU photon propagation time |

**Speedup = (Run 1 - Run 2) / GPU time from Run 3**

- Run 1 uses `--skip-gpu` to prevent the GPU `simulate()` call, so the G4 wall
  time reflects only CPU-side work.
- Run 2 uses G4's `setStackPhotons false` to prevent optical photon track creation
  entirely — this is cheaper than killing photons in the SteppingAction because no
  G4Track objects are allocated.
- GPU time is the wall-clock duration of `gx->simulate() + cudaDeviceSynchronize()`,
  which is pure photon propagation on the GPU.

Runs 1 and 2 use single-threaded G4 (`/run/numberOfThreads 1`) to avoid
multithreading overhead inflating the per-photon CPU time.

## Per-Process Timing

Run 1 also prints per-process step timing and per-photon statistics:

```
Geant4: StepTime  Transportation:  count= 114186428  avg=    0.77 us  total=  87.840 s
Geant4: StepTime         OpWLS:    count=     42418  avg=    0.85 us  total=   0.036 s
Geant4: StepTime    OpRayleigh:    count=     96947  avg=    0.98 us  total=   0.095 s
Geant4: StepTime  OpAbsorption:    count=      8621  avg=    0.82 us  total=   0.007 s

Geant4: PhotonTiming: 288738 photons, avg=306.0 us, median=2.1 us, ...
Geant4: PhotonPercentiles: p10=1.1 us, p50=2.1 us, p90=9.7 us, p99=7906.9 us
Geant4: PhotonSteps: 288738 photons, avg=397.0, median=2, min=1, max=10001
Geant4: StepPercentiles: p10=1, p50=2, p90=10, p99=10001
```

### How step timing works

Each optical photon step is timed using `std::chrono::steady_clock` between
consecutive `UserSteppingAction` calls. The interval captures the full step cost:
geometry navigation (`G4Navigator::ComputeStep`), all process interaction length
proposals, and physics execution. Steps are classified by the post-step defining
process name.

### How per-photon timing works

`PreUserTrackingAction` starts a timer when an optical photon track begins.
`PostUserTrackingAction` stops it and records the duration and step count.
Median and percentiles are computed from the collected per-photon times at end
of run.

Both approaches are thread-safe (thread-local timers, atomic accumulators).

## Typical Results

### APEX detector, 10 MeV electron, single event (~250k photons)

```
G4 photon-only:    88.4 s   (306 us/photon avg, 2.1 us median)
GPU:               0.064 s  (82 ns/photon)
Speedup:           ~1,400x
```

### APEX detector, 10 events (~2.5M photons)

```
G4 photon-only:   ~880 s
GPU:              0.20 s   (82 ns/photon)
Speedup:          ~4,400x
```

GPU rate improves with batch size (better RTX occupancy).

## Configuration

Environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `OPTICKS_PROPAGATE_EPSILON` | 0.00001 | Ray tmin after boundary events (mm) |
| `OPTICKS_PROPAGATE_EPSILON0` | 0.0006 | Ray tmin after bulk events (mm) |
| `OPTICKS_MAX_BOUNCE` | 1000 | Maximum bounces per photon |
| `BENCHMARK_CONFIG` | det_debug | Config JSON name |

## Implementation Details

The timing instrumentation is in `src/GPURaytrace.h`:

- **SteppingAction**: Atomic counters `fTimeTransport`, `fTimeOpWLS`, etc.
  Thread-local `fLastStepTime` for inter-step interval measurement.
- **TrackingAction**: Atomic counters `fTotalPhotonTime`, `fPhotonCount`, etc.
  Thread-local `fTrackStart` for per-photon lifetime measurement.
  Mutex-protected vectors `fAllTimes`, `fAllSteps` for median/percentile computation.
- **RunAction**: `fSkipGPU` flag prevents `gx->simulate()`. `PrintTimingReport()`
  outputs all collected timing data at end of run.
- **GPURaytrace.cpp**: `--skip-gpu` CLI flag sets `RunAction::fSkipGPU`.
