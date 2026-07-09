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
