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

To benchmark the experimental CSGOptiX RNG-state rebuild variant, see the
[`SIMPHONY_RNG_REBUILD` CMake option](getting-started.md#cmake-build-options).
It is a compile-time choice, so reconfigure and rebuild the PTX target before
measuring each variant.

### Shader execution reordering (SER)

`SIMPHONY_ENABLE_SER` defaults to `ON`. With OptiX 8 or newer, this compiles a
reorder call into the photon propagation loop. Older SDKs exclude that code.
The JSON configuration controls its execution:

```json
{ "ser": { "interval": 0, "hint": "none" } }
```

`interval` defaults to zero. A positive value executes reordering every N
bounces, including bounce zero; zero disables execution. `hint` defaults to
`none` and accepts `none` or `bounce`. The `none` mode supplies no application
hint bits, leaving ordering to OptiX. The `bounce` mode supplies the low four
bits of the bounce index divided by the interval. Other values are rejected.

Retaining a dormant reorder intrinsic can change OptiX dispatch even when
`interval` is zero. To remove SER from the compiled program, rebuild with:

```shell
cmake -S . -B build -DSIMPHONY_ENABLE_SER=OFF
cmake --build build --target csgoptix_ptx
```

This rebuilds the build-tree PTX; reinstall when using an installed copy.
With SER compiled out, the JSON SER settings have no effect.

Performance depends on the workload. Tests of an entry-call control on an
RTX 4090 with OptiX 9 showed substantial gains for large DUNE showers, but
regressions in some pfRICH workloads and rendering. Compare compiled-out,
compiled-in with interval zero, and active reordering on representative
jobs with identical seeds, slot limits, and output settings. Check output
agreement and whole-job throughput before choosing a production setting.

### Interpreting `simg4ox` MT timings

With `--threads N` and N greater than one, Geant4 workers collect torch photons
or charged-particle gensteps into a growable buffer per event. At the event
boundary a worker submits its buffer and can start another event. The queue
holds one waiting event per visible GPU. A full queue blocks producers until
a slot opens. A GPU service thread owns each device and transports one complete
event at a time. The run action waits for the GPU work and merges hits in
event-ID order.

Set `CUDA_VISIBLE_DEVICES=0` or `CUDA_VISIBLE_DEVICES=0,1` to compare one
and two GPUs with the same CPU worker count. For a serial torch control,
`--threads 1 --no-cpu-recorder` retains Geant4 tracking and hit collection
with the same CPU recording settings as MT.

For example, run 20 GeV/c electrons in pfRICH with four CPU workers:

```bash
simg4ox -g tests/geom/pfrich_min_FINAL.gdml -c pfrich \
    -m tests/run_mt.mac --particle e- --momentum-gev-c 20 \
    --multiplicity 10 --threads 4 --seed 42
```

To compare CPU worker counts, run the same geometry, source configuration, and
event macro with `--threads 1`, `2`, `4`, or `8`. Use a separate working directory
or `event.output_dir` for each run. Enable `event_timing.output` to record the
per-event CPU/GPU timelines described below. Keep the source settings, event
count, random seeds, and GPU configuration fixed when comparing runs.

Torch input generation uses seed zero for each event. The timing manifest
records `torch_seed: 0` and labels these as fixed-input timing repeats. The
`--seed` option controls Geant4 transport; GPU random streams are configured
separately. Changing worker counts does not imply identical photon histories.

The supplied `tests/run_mt.mac` contains only five low-statistics events and is
an integration check, not a benchmark. The integration script
`tests/test_simg4ox_multithread.sh` also accepts `THREADS=1` for the matched
serial control and validates the per-event photon counts and timing.

## Per-event CPU/GPU timelines

`simg4ox` can write event-level timing data suitable for CPU/GPU scheduling
studies. Set `event_timing.output` in the selected JSON configuration to enable
a CSV with one row per event and a JSON run manifest beside it; omit it or set
it to an empty string to disable the recorder. The recorder uses one monotonic clock origin for the run. Each CSV row
identifies the event, Geant4 worker, photon and genstep counts, CPU and GPU
hits, and elapsed and CPU times. The run manifest records the configuration
and clock origin. The manifest's `run_id` identifies the Geant4 run when successive
`/run/beamOn` calls reuse event IDs. Event IDs in the output follow Geant4 event order even when
GPU completion order differs.

Serial runs report `dispatch_mode=blocking`. MT runs report
`dispatch_mode=asynchronous` and include `num_gpus` in the manifest.
Use these CSV columns to reconstruct the MT timeline:

| Interval | Start column | End column |
| --- | --- | --- |
| Geant4 event production | `start_offset_s` | `gpu_submit_time_offset_s` |
| Producer waiting for queue space | `gpu_submit_time_offset_s` | `cpu_end_time_offset_s` |
| GPU service on `gpu_device_id` / `gpu_uuid` | `gpu_service_start_offset_s` | `gpu_service_end_offset_s` |
| GPU hit collection and reset | `gpu_service_end_offset_s` | `gpu_cleanup_end_offset_s` |

GPU service is host-observed and includes staging, transport and synchronization.
Use a CUDA device trace to measure kernel execution. An empty event records
device ID `-1` and empty GPU service columns. Thread CPU measurements cover
Geant4 work before submission and GPU-worker cleanup after service. Process
CPU measurements include all concurrently running threads.

All `*_time`, `*_offset_s`, `*_runtime_s`, `*_delay_s`, and CPU values are
seconds derived from monotonic or CPU clocks. Momentum is GeV/c; the remaining
workload columns are counts.

Set `event_timing.profile.enabled` to `true` when lifecycle-level timestamps
and memory samples are also needed. Each detailed mark captures wall time in
microseconds, monotonic time in nanoseconds, and virtual/resident memory in KiB. This path
queries `/proc`, appends to synchronized in-memory storage, and periodically
rewrites the crash-safe profile snapshot, so it has more overhead than the
five-boundary event recorder. Its exact CSV header is:

```text
name,wall_time_us,steady_time_ns,vm_kb,rss_kb,metadata
```

The default filename is `event_timing_profile.csv`. Relative timing paths are
resolved beneath `event.output_dir`. Configure a literal path or an indexed
path with one safe integer conversion:

```json
"event_timing": {
  "output": "events.csv",
  "profile": {
    "enabled": true,
    "output": "event_timing_profile_%05d.csv",
    "path_index": 7
  }
}
```

`sreport` reads `event_timing_profile.csv` from a selected run directory by
default. For a configured custom or indexed filename, pass the actual profile
path while creating the directory report:

```bash
build/sysrap/tests/sreport /tmp/pfrich --event-timing-profile /tmp/pfrich/event_timing_profile_00007.csv
```

It can also analyze only a profile, without loading an event directory:

```bash
build/sysrap/tests/sreport --event-timing-profile /tmp/pfrich/event_timing_profile.csv
```

The bundled pfrich baseline runs three single-threaded Geant4 events with one
negative muon at 5 GeV/c per event. Its position and forward direction come
from [`config/pfrich.json`](../config/pfrich.json). Optical secondaries are not
placed on the Geant4 stack; their Cerenkov/scintillation gensteps are captured
and transported by Simphony on the GPU.

```bash
cmake --build build --target simg4ox
SIMG4OX_BIN="$PWD/build/src/simg4ox" scripts/run_pfrich_timing.sh

# Add the optional lifecycle/memory profile by selecting its JSON config.
SIMG4OX_BIN="$PWD/build/src/simg4ox" \
scripts/run_pfrich_timing.sh /tmp/simphony-pfrich-profiled pfrich_profiled
```

The runner creates `pfrich_timing/events.csv`,
`pfrich_timing/events.manifest.json`, and `pfrich_timing/simg4ox.stdout.log`.
The CSV records `scenario`, `dispatch_mode`, event and CPU phase boundaries, GPU
submit/start/end and wait boundaries, run-local offsets, and measured durations.
Producer-specific particle, genstep, photon, and hit columns are retained.

For an MT timeline, group CPU generation and post-processing intervals by
`worker_id` and plot GPU service intervals on a separate lane. Queue and GPU
wait intervals describe workers waiting for results; they are not CPU busy
time. Keep each run's monotonic origin separate when comparing independent
processes.

Initial events include CUDA, OptiX, and worker warm-up effects. Preserve the raw
data and report any exclusions explicitly when comparing steady-state timings.
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

The selected JSON configuration must set `event.event_mode` to a mode that
saves output arrays. The default mode, `Minimal`, gathers hits into memory but
does not write `.npy` event arrays.

Use `HitPhoton` for final photon/hit summaries:

```json
{
  "event": {
    "event_mode": "HitPhoton"
  }
}
```

Use `DebugLite` when you need `record.npy` for step-by-step traces:

```json
{
  "event": {
    "event_mode": "DebugLite"
  }
}
```

`HitPhotonSeq` saves `seq.npy` without `record.npy`; `DebugHeavy` saves the
same primary debug arrays as `DebugLite` plus additional low-level diagnostic
arrays. See the event-mode table in the output protocol for the full save
matrix.

### Running a simulation with output saving

```bash
GPUPhotonSourceMinimal -g tests/geom/wls_test.gdml -c wls_test -m tests/run.mac -s 42
```

Here `config/wls_test.json` supplies the event mode. Copy or edit a named
configuration when a different persistence mode is needed. See
[Runtime configuration](configuration.md) for configuration discovery,
defaults and the event schema. Legacy `OPTICKS_*` event settings are ignored.

### Output file location

GPU event arrays are written under JSON `event.output_dir` and
`event.event_reldir`:

    <output_base>/ALL0/A000/

The script also accepts the parent folder and auto-selects `A000` when it
contains the GPU event. The exact path construction and configurable segments
are defined in the [output protocol](inputs-outputs.md#event-directory).

### Running the analysis

```bash
# Basic summary tables:
python optiphy/ana/photon_history_summary.py <event_folder>

# Auto-resolves A000 subfolder:
python optiphy/ana/photon_history_summary.py /tmp/$USER/opticks/GEOM/GEOM/GPUPhotonSourceMinimal/ALL0

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
