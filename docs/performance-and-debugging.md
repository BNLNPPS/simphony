# Performance and debugging

This guide covers the existing performance-study workflow and the
`optiphy/ana/photon_history_summary.py` analysis script.

## Performance studies

To quantify the speed-up achieved by Simphony compared to Geant4, the
repository provides Python tooling that runs the same Geant4 simulation with
and without tracking optical photons in G4. The difference between those runs
approximates the time required to simulate photons in Geant4, while the same
photons are simulated on the GPU with Simphony and the GPU simulation time is
saved.

```shell
cd simphony/

mkdir -p /tmp/out/develop
docker build -t simphony:develop --target=develop .
docker run --rm -t -v /tmp/out:/tmp/out simphony:develop \
    run-performance -g tests/geom/opticks_raindrop.gdml -o /tmp/out/develop

mkdir -p /tmp/out/release
docker build -t simphony:release --target=release .
docker run --rm -t -v /tmp/out:/tmp/out simphony:release \
    run-performance -g tests/geom/opticks_raindrop.gdml -o /tmp/out/release
```

### Interpreting `simg4ox` MT timings

`simg4ox --threads N` parallelizes Geant4 CPU tracking of its configured torch
photons. It does not run multiple Opticks launches concurrently: the current
process-wide GPU event context requires launches to be serialized in event-ID
order. Consequently, an end-to-end `simg4ox` wall-clock measurement includes
parallel CPU work plus serialized GPU work and should not be interpreted as a
pure GPU speed-up measurement.

![Serial and multithreaded simg4ox event-processing timelines](assets/simg4ox-event-processing.svg)

The supplied `tests/run_mt.mac` contains only five low-statistics events and is
an integration check, not a benchmark.

## Per-event CPU/GPU timelines

`simg4ox` can write event-level timing data suitable for CPU/GPU scheduling
studies. `--timing-output` enables a CSV with one row per event and a JSON run
manifest beside it. The CSV records:

- the basket-forming Geant4 interval before GPU dispatch and the CPU
  collection/reset interval afterward;
- GPU submission, start, and completion timestamps for the blocking
  `G4CXOpticks::simulate()` call, including transfers and synchronization;
- wall-clock, process-CPU, and event-thread CPU durations; and
- generated genstep/photon counts and CPU/GPU hit counts.

All timeline offsets use a monotonic run-local clock. They are intended for
interval comparison within one run, not as timestamps shared across jobs.

The bundled pfrich baseline runs three single-threaded Geant4 events with one
negative muon at 5 GeV/c per event. Its position and forward direction come
from [`config/pfrich.json`](../config/pfrich.json). Optical secondaries are not
placed on the Geant4 stack; their Cerenkov/scintillation gensteps are captured
and transported by Opticks on the GPU.

```bash
cmake --build build --target simg4ox
SIMG4OX_BIN="$PWD/build/src/simg4ox" scripts/run_pfrich_timing.sh
```

The runner creates `pfrich_timing/events.csv`,
`pfrich_timing/events.manifest.json`, and `pfrich_timing/simg4ox.stdout.log`.
The CSV follows the timeline columns used by teerex: `scenario`,
`dispatch_mode`, event and CPU phase boundaries, GPU submit/start/end and wait
boundaries, run-local offsets, and measured durations. Producer-specific
particle, genstep, photon, and hit columns are retained.

Use teerex's marimo notebook to compare this measured run with simulated
blocking and asynchronous scheduling:

```bash
cd third_party/teerex
uv run marimo edit notebooks/simload.py
```

When teerex is checked out at that path, the notebook discovers
`pfrich_timing/events.csv` automatically. It also accepts arbitrary CSV paths
through its input control. The first event includes CUDA and OptiX warm-up, so
steady-state comparisons should report it separately or exclude it explicitly.
This small run matches the table's pfrich particle and momentum, but not its
`10^5`-muon statistics.

## Debug analysis with `optiphy/ana/photon_history_summary.py`

The script analyzes GPU optical photon simulation output to debug where
photons went and why: which were detected, absorbed, scattered, or trapped
bouncing until the configured limit, and whether wavelength shifting and
energy conservation behaved as expected.

For the authoritative event folder layout, file schemas, `record.npy`
quad layout, `q3` bit packing, `seq.npy` nibble encoding, and hitmask rules,
see [Simulation inputs and outputs](inputs-outputs.md#output-protocol).

### Prerequisites

The simulation must be run with `OPTICKS_EVENT_MODE` set so that output arrays
are saved to disk. The default mode, `Minimal`, gathers hits into memory but
does not write `.npy` event arrays.

Use `HitPhoton` for final photon/hit summaries:

```bash
export OPTICKS_EVENT_MODE=HitPhoton
```

Use `DebugLite` when you need `record.npy` for step-by-step traces:

```bash
export OPTICKS_EVENT_MODE=DebugLite
```

`HitPhotonSeq` saves `seq.npy` without `record.npy`; `DebugHeavy` saves the
same primary debug arrays as `DebugLite` plus additional low-level diagnostic
arrays. See the event-mode table in the output protocol for the full save
matrix.

### Running a simulation with output saving

```bash
OPTICKS_EVENT_MODE=DebugLite GPUPhotonSourceMinimal -g tests/geom/wls_test.gdml -c wls_test -m tests/run.mac -s 42
```

### Output file location

GPU event arrays are written under the configured output base (`event.output_dir` / `OPTICKS_OUT_FOLD`):

    <output_base>/ALL0_no_opticks_event_name/A000/

The script also accepts the parent folder and auto-selects `A000` when it
contains the GPU event. The exact path construction and configurable segments
are defined in the [output protocol](inputs-outputs.md#event-directory).

### Running the analysis

```bash
# Basic summary tables:
python optiphy/ana/photon_history_summary.py <event_folder>

# Auto-resolves A000 subfolder:
python optiphy/ana/photon_history_summary.py /tmp/$USER/opticks/GEOM/GEOM/GPUPhotonSourceMinimal/ALL0_no_opticks_event_name

# Show step-by-step trace for specific photons, when record.npy is present:
python optiphy/ana/photon_history_summary.py <path> --trace 0,227,235

# Show all non-detected photons with traces when record.npy is present:
python optiphy/ana/photon_history_summary.py <path> --lost

# Filter by terminal flag:
python optiphy/ana/photon_history_summary.py <path> --flag BULK_ABSORB

# Show per-photon detail for first 20 photons:
python optiphy/ana/photon_history_summary.py <path> --detail 20
```

### Output tables

The script prints tables for photon outcomes by terminal flag, cumulative
flagmask histories, wavelength statistics, and position/time statistics.
When `seq.npy` is present it also prints ranked step-sequence histories. When
`record.npy` is present it prints step-count distributions and the detail
commands include per-step positions, wavelengths, boundaries, and flags.
