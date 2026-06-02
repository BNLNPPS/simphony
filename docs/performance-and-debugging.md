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
bouncing forever, and whether the physics, such as wavelength shifting and
energy conservation, worked correctly. Without it, the simulation is a black
box that only reports a hit count.

### Prerequisites

The simulation must be run with `OPTICKS_EVENT_MODE` set so that output arrays
are saved to disk. The default mode, `Minimal`, only gathers hits into memory
and does not write `.npy` files.

```bash
export OPTICKS_EVENT_MODE=HitPhoton   # saves photon, hit, inphoton, record, seq
```

Valid modes for this script: `HitPhoton`, `DebugLite`, `DebugHeavy`.

### Running a simulation with output saving

```bash
OPTICKS_EVENT_MODE=HitPhoton GPUPhotonSourceMinimal -g tests/geom/wls_test.gdml -c wls_test -m tests/run.mac -s 42
```

### Output file location

Opticks writes `.npy` arrays to:

```text
$TMP/GEOM/$GEOM/<ExecutableName>/ALL0_no_opticks_event_name/A000/
```

| Variable | Default | Override |
|----------|---------|----------|
| `$TMP` | `/tmp/$USER/opticks` | `export TMP=/path/to/tmp` |
| `$GEOM` | `GEOM` | `export GEOM=mygeom` |

For example, with defaults:

```text
/tmp/$USER/opticks/GEOM/GEOM/GPUPhotonSourceMinimal/ALL0_no_opticks_event_name/A000/
|-- photon.npy      # (N, 4, 4) float32 - final state of all photons
|-- hit.npy         # (H, 4, 4) float32 - detected photons only
|-- inphoton.npy    # (N, 4, 4) float32 - input photons before simulation
|-- record.npy      # (N, 32, 4, 4) float32 - step-by-step history (up to 32 steps)
|-- seq.npy         # (N, 2, 2) uint64 - compressed step sequence per photon
|-- genstep.npy     # generation step parameters
|-- domain.npy      # domain compression parameters
```

### Running the analysis

```bash
# Basic summary tables:
python optiphy/ana/photon_history_summary.py <event_folder>

# Auto-resolves A000 subfolder:
python optiphy/ana/photon_history_summary.py /tmp/$USER/opticks/GEOM/GEOM/GPUPhotonSourceMinimal/ALL0_no_opticks_event_name

# Show step-by-step trace for specific photons:
python optiphy/ana/photon_history_summary.py <path> --trace 0,227,235

# Show all non-detected (lost) photons with full traces:
python optiphy/ana/photon_history_summary.py <path> --lost

# Filter by terminal flag:
python optiphy/ana/photon_history_summary.py <path> --flag BULK_ABSORB

# Show per-photon detail for first 20 photons:
python optiphy/ana/photon_history_summary.py <path> --detail 20
```

### Output tables

The script prints six summary tables by default: photon outcomes by terminal
flag with hit rate and MaxBounce truncation count, cumulative flagmask history
distribution, wavelength statistics with energy conservation check,
position/time stats, ranked step sequence histories decoded from `seq.npy`,
and step count distribution flagging truncated photons. The `--lost`,
`--trace`, and `--flag` options drill into specific photons with full
step-by-step position, wavelength, and flag traces for debugging exactly where
and why a photon died.

| Table | Description |
|-------|-------------|
| **Photon Outcomes** | Counts by terminal flag, such as `SURFACE_DETECT` and `BULK_ABSORB`, with boundary indices, hitmask, and MaxBounce truncation count |
| **Photon Histories** | Unique cumulative flagmask combinations, such as `TO|RE|BT|SD` |
| **Wavelength Analysis** | Input vs output wavelengths, shift count, energy conservation check |
| **Position / Time** | Radius and time statistics |
| **Sequence Histories** | Top N step-by-step sequences from `seq.npy`, such as `TO RE BT SD` |
| **Step Counts** | Distribution of steps per photon, flagging truncated ones at the maximum |

### Photon data layout (`sphoton.h`)

Each photon is a `(4, 4)` float32 matrix: four quads of four floats.
`photon.npy` holds the final state, `hit.npy` the detected subset,
`inphoton.npy` the initial state, and `record.npy` stores the full
step-by-step history, up to 32 bounces per photon using the same quad layout
per step.

| Quad | .x | .y | .z | .w |
|------|----|----|----|----|
| q0 | pos.x | pos.y | pos.z | time |
| q1 | mom.x | mom.y | mom.z | iindex |
| q2 | pol.x | pol.y | pol.z | wavelength (nm) |
| q3 | orient_boundary_flag | identity | index | flagmask |

### q3 bit packing

`q3` is stored as float32 but interpreted as uint32 via `.view(np.uint32)`.
This is where the physics outcome lives.

`q3.x` (`orient_boundary_flag`) packs three fields into 32 bits:

```text
bit 31          : orient sign (1 = inward-facing surface normal)
bits 30-16      : boundary index (which pair of materials the photon is at)
bits 15-0       : terminal flag (what happened last)
```

Python extraction:

```python
q3    = photon[:, 3, :].view(np.uint32)
flag  = q3[:, 0] & 0xFFFF             # terminal flag
bnd   = (q3[:, 0] >> 16) & 0x7FFF     # boundary index
```

`q3.w` (`flagmask`) is the cumulative OR of every flag set during the
photon's lifetime. Each call to `set_flag(f)` does both `flag = f` and
`flagmask |= f`. A single integer therefore captures the full history. For
example, `0x0854` = `TO|RE|SD|BT` means torch generation, WLS re-emission,
boundary transmit, and surface detect.

### Sequence history encoding (`sseq.h`)

`seq.npy` shape is `(N, 2, 2)` uint64. Each photon gets two pairs of uint64:
`seqhis[2]` for flag history and `seqbnd[2]` for boundary history.

Each uint64 holds 16 slots of 4-bit nibbles, for a total of 32 slots across
the pair. Flag nibbles store the find-first-set value of the flag bit.
`TORCH` (bit 2) stores as nibble 3, and `SURFACE_DETECT` (bit 6) stores as 7.

```python
seqhis = seq[:, 0, :]   # two uint64 per photon
nibble = (seqhis[0] >> (4 * slot)) & 0xF   # for slot 0-15
flag   = 1 << (nibble - 1)                  # reconstruct flag
```

This gives a compact chronological per-step history in 16 bytes per photon.

### Hit determination and MaxBounce

A photon is a hit when `(flagmask & hitmask) == hitmask`. The default hitmask
is `SD` (`SURFACE_DETECT = 0x40`), but for PMT efficiency tagging it may be
`EC` (`EFFICIENCY_COLLECT = 0x2000`). The script reads the actual hitmask from
`NPFold_meta.txt` in the event folder.

Photons that exhaust the bounce limit, `MaxBounce` by default 31 and therefore
32 steps, receive no special flag. The propagation loop simply exits and the
photon keeps whatever terminal flag it had at its last step. These are
typically photons trapped by total internal reflection. The script detects
them by checking `step_count == max_steps` and reports the count in the
outcome table.

### Flag definitions (`OpticksPhoton.h`)

Flags are a power-of-two enum where each GPU physics process gets one bit:

| Flag | Hex | Abbrev | Description |
|------|-----|--------|-------------|
| CERENKOV | 0x0001 | CK | Cerenkov generation |
| SCINTILLATION | 0x0002 | SI | Scintillation generation |
| TORCH | 0x0004 | TO | Torch source |
| BULK_ABSORB | 0x0008 | AB | Absorbed in bulk material |
| BULK_REEMIT | 0x0010 | RE | Re-emitted, scintillation or WLS |
| BULK_SCATTER | 0x0020 | SC | Rayleigh scattered |
| SURFACE_DETECT | 0x0040 | SD | Detected at surface |
| SURFACE_ABSORB | 0x0080 | SA | Absorbed at surface |
| SURFACE_DREFLECT | 0x0100 | DR | Diffuse reflection at surface |
| SURFACE_SREFLECT | 0x0200 | SR | Specular reflection at surface |
| BOUNDARY_REFLECT | 0x0400 | BR | Fresnel reflection at boundary |
| BOUNDARY_TRANSMIT | 0x0800 | BT | Transmitted through boundary |
| NAN_ABORT | 0x1000 | NA | Aborted due to NaN, usually geometry error |
| EFFICIENCY_COLLECT | 0x2000 | EC | Collected by PMT efficiency |
| EFFICIENCY_CULL | 0x4000 | EL | Culled by PMT efficiency |
| MISS | 0x8000 | MI | Missed all geometry |
