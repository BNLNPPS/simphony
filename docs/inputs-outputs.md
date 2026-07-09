# Simulation inputs and outputs

This guide collects the runtime configuration, application entry points, and
GDML requirements that were previously documented in the top-level README. It
also defines the persisted optical-event output protocol for Python readers.
For the optical-surface physics implemented by Simphony, see
[Physics](physics.md).

## Geometry input

Simphony imports detector geometry in GDML format. Example applications accept
GDML files through `-g`, for example:

```bash
GPUCerenkov -g tests/geom/opticks_raindrop.gdml
```

The GDML must define the optical properties needed by the GPU path:

- Efficiency, used to specify detection efficiency and assign sensitive
  surfaces
- Material refractive index, used for optical boundary transmission and
  reflection
- Material group velocity
- Reflectivity, used for non-sensor specular or diffuse explicit surfaces
- Additional Geant4 material and surface optical properties needed for
  CPU-side Geant4 validation, noting that not all of them are used by
  Simphony's standard GPU propagation

See [Physics](physics.md) for the current standard-GPU interpretation of GDML
optical-surface properties such as `model`, `finish`, `type`, `EFFICIENCY`,
and `REFLECTIVITY`.

## Photon source inputs

Simphony examples feed the GPU simulation in three common ways:

- Charged-particle events in Geant4 collect Cerenkov and, where supported,
  scintillation gensteps. `GPUCerenkov` and `GPURaytrace` use this path.
- Torch-source applications generate optical photons directly from the JSON
  configuration described below.

The [examples guide](../examples/README.md#application-capabilities) compares
the executable-level capabilities, including whether Geant4 optical-photon
tracking is also run for validation.

## Torch configuration

`simg4ox` reads photon source parameters from a JSON config file, by default `config/dev.json`.

| Field | Description |
|-------|-------------|
| `type` | Source shape: `disc`, `sphere`, `point` |
| `radius` | Size of the source area (mm) |
| `pos` | Center position `[x, y, z]` (mm) |
| `mom` | Emission direction `[x, y, z]` (normalized automatically) |
| `numphoton` | Number of photons to generate |
| `wavelength` | Photon wavelength (nm) |

## Defining primary particles

For charged-particle examples, the user or developer defines the primary
particle count and Geant4 threading in the macro file, and the primary
particle position, momentum, and related settings in the application's
`GeneratePrimaries` implementation. `src/GPUCerenkov.h` provides a compact
working example.

```text
/run/numberOfThreads {threads}
/run/verbose 1
/process/optical/cerenkov/setStackPhotons {flag}
/run/initialize
/run/beamOn 500
```

`setStackPhotons` controls whether Geant4 propagates optical photons. In
production-style GPU runs, Simphony handles optical photon propagation on the
GPU. If more photons are simulated than can fit in GPU memory, move the GPU
simulation and hit retrieval from run-end handling to per-event handling.

## GDML scintillation properties

For scintillation to work with both Geant4 11.x and Simphony GPU simulation,
the GDML must define properties using the correct syntax.

1. Const properties, such as yield and time constants, must use
   `coldim="1"` matrices:

```xml
<define>
    <matrix coldim="1" name="SCINT_YIELD" values="5000.0"/>
    <matrix coldim="1" name="FAST_TIME_CONST" values="21.5"/>
</define>
```

2. Both old and new style property names are required for Simphony
   compatibility:

```xml
<material name="Crystal">
    <!-- New Geant4 11.x names -->
    <property name="SCINTILLATIONYIELD" ref="SCINT_YIELD"/>
    <property name="SCINTILLATIONCOMPONENT1" ref="SCINT_SPECTRUM"/>
    <property name="SCINTILLATIONTIMECONSTANT1" ref="FAST_TIME_CONST"/>
    <!-- Old-style names for Opticks U4Scint -->
    <property name="FASTCOMPONENT" ref="SCINT_SPECTRUM"/>
    <property name="SLOWCOMPONENT" ref="SCINT_SPECTRUM"/>
    <property name="REEMISSIONPROB" ref="REEMISSION_PROB"/>
</material>
```

See `tests/geom/8x8SiPM_w_CSI_optial_grease.gdml` for a complete working
example.

## Output protocol

Simphony persists optical-event arrays as `.npy` files using component names
from `sysrap/SComp.h`. The canonical full step-history filename is
`record.npy`.

The schema below is source-backed by `SEvt::makePhoton`, `SEvt::makeRecord`,
`SEvt::makeSeq`, `SEvt::gatherHit`, `SEvt::gatherDomain`, `sphoton`, `sseq`,
and `OpticksPhoton.h`. Treat the file stems, dtypes, array ranks, `sphoton`
quad layout, `q3` bit packing, and `seq.npy` nibble encoding as the reader
contract. Event folder names and which files appear are runtime configuration.
Auxiliary debug arrays such as `aux.npy`, `prd.npy`, `tag.npy`, `flat.npy`,
and `sup.npy` are lower-level diagnostics.

### Event directory

For applications that construct `gphox::Config`, the event output base is
`Config::output_dir`, read from the JSON key `event.output_dir` and applied to
the lower-level event system by `Config::Apply`. With the default event
relative directory, GPU event arrays are written below:

```text
<Config.output_dir>/ALL0_no_opticks_event_name/A000/
```

The path is assembled as:

| Segment | Source | Notes |
|---------|--------|-------|
| `<Config.output_dir>` | `Config::output_dir`, JSON `event.output_dir` | The app-level base directory for event output folders. The default JSON configs currently use `./`. |
| `ALL0_no_opticks_event_name` | lower-level event relative directory default | Stable enough for readers as a default, but not a data-schema contract. |
| `A000` | event instance and event index | `A` is the GPU `SEvt` instance, `B` is the CPU/G4 comparison instance. The numeric part is zero-padded event index width 3. |

For run-level metadata, the same base and event relative directory are used
without the final `A000` or `B000` event-index subdirectory. The app-level
`Config` interface does not expose a separate save-mode field; prefer
`Config::output_dir` for controlling where event folders are written.

### File schemas

The table assumes the default full photon representation and unmerged hits:
`Config::mode_lite` is `ModeLite::Unspecified` or `ModeLite::Standard`, and
`Config::mode_merge` is `ModeMerge::Unspecified` or `ModeMerge::Separate`.
Lite and merge modes replace the photon and hit component names with
`photonlite`, `hitlite`, `hitmerged`, or `hitlitemerged` after
`Config::Apply` synchronizes the app-level policy to `SEventConfig`.

| File | Shape | Dtype | Meaning | Written by |
|------|-------|-------|---------|------------|
| `photon.npy` | `(N, 4, 4)` | `float32` | Final state of every photon as `sphoton`. | `EventMode::HitPhoton`, `EventMode::HitPhotonSeq`, `EventMode::DebugLite`, `EventMode::DebugHeavy` |
| `hit.npy` | `(H, 4, 4)` | `float32` | Subset of `photon.npy` whose `flagmask` satisfies the configured hitmask. | `EventMode::Hit`, `EventMode::HitPhoton`, `EventMode::HitPhotonSeq`, `EventMode::HitSeq`, `EventMode::DebugLite`, `EventMode::DebugHeavy` |
| `inphoton.npy` | `(N, 4, 4)` | `float32` | Input photons copied or narrowed from the configured input-photon array. Present only when input photons exist. | `EventMode::DebugLite`, `EventMode::DebugHeavy` |
| `record.npy` | `(N, R, 4, 4)` | `float32` | Full per-step `sphoton` records for each photon. `R` is `MaxRecord`; debug modes set it to `RecordLimit() == sseq::SLOTS == 32`. | `EventMode::DebugLite`, `EventMode::DebugHeavy` |
| `seq.npy` | `(N, 2, 2)` | `uint64` | Compact chronological step history: two `seqhis` words and two `seqbnd` words per photon. | `EventMode::HitPhotonSeq`, `EventMode::HitSeq`, `EventMode::DebugLite`, `EventMode::DebugHeavy` |
| `genstep.npy` | `(G, 6, 4)` | `float32` | Uploaded generation-step records. | `EventMode::Hit`, `EventMode::HitPhoton`, `EventMode::HitPhotonSeq`, `EventMode::HitSeq`, `EventMode::DebugLite`, `EventMode::DebugHeavy` |
| `domain.npy` | `(2, 4, 4)` | `float32` | Domain and config quads from `sevent::get_domain` and `sevent::get_config`. Mostly relevant to compressed records. | `EventMode::DebugLite`, `EventMode::DebugHeavy` |

`N` is the number of photons in the event, `H` is the number of selected hits,
`G` is the number of gensteps, and `R` is the configured record depth. A
configured component with no backing data may be omitted.

### Event modes

`Config::event_mode`, read from JSON `event.mode`, controls both GPU-to-CPU
gathering and CPU-to-file saving after `Config::Apply` synchronizes it to the
lower-level event system. The app-level default is `EventMode::Minimal`.

| Mode | Files saved with default lite/merge settings | Notes |
|------|----------------------------------------------|-------|
| `EventMode::Minimal` | none | Gathers hits into memory only. Highest-statistics mode. |
| `EventMode::Hit` | `hit.npy`, `genstep.npy` | Hit and genstep output only. |
| `EventMode::HitPhoton` | `hit.npy`, `photon.npy`, `genstep.npy` | Does not save `record.npy` or `seq.npy`. |
| `EventMode::HitPhotonSeq` | `hit.npy`, `photon.npy`, `seq.npy`, `genstep.npy` | Sets `MaxSeq=1`; sequence output without full records. |
| `EventMode::HitSeq` | `hit.npy`, `seq.npy`, `genstep.npy` | Sequence output without full photon output. |
| `EventMode::DebugLite` | `domain.npy`, `genstep.npy`, `inphoton.npy` when present, `photon.npy`, `hit.npy`, `record.npy`, `seq.npy` | Enables full records and sequence history. Low-statistics/debug mode. |
| `EventMode::DebugHeavy` | `DebugLite` files plus lower-level arrays such as `aux.npy`, `prd.npy`, `tag.npy`, `flat.npy`, `sup.npy` | Deep debugging only; substantially higher memory use. |

Use `EventMode::DebugLite` or `EventMode::DebugHeavy` when an external reader
requires `record.npy`. Use `EventMode::HitPhotonSeq`, `EventMode::HitSeq`,
`EventMode::DebugLite`, or `EventMode::DebugHeavy` when it requires
`seq.npy`.

### Photon and record layout

`photon.npy`, `hit.npy`, `inphoton.npy`, and each `record.npy` step store the
same `sphoton` layout: a `(4, 4)` `float32` matrix. Numeric physics fields are
stored as floats. Packed integer fields occupy `float32` slots and must be
read through a `uint32` view.

| Quad | `.x` | `.y` | `.z` | `.w` |
|------|------|------|------|------|
| `q0` | `pos.x` in mm | `pos.y` in mm | `pos.z` in mm | `time` in ns |
| `q1` | `mom.x` | `mom.y` | `mom.z` | `hitcount_iindex` as `uint32` |
| `q2` | `pol.x` | `pol.y` | `pol.z` | `wavelength` in nm |
| `q3` | `orient_boundary_flag` as `uint32` | `identity` plus index extension as `uint32` | `index` low 32 bits as `uint32` | `flagmask` as `uint32` |

`mom` and `pol` are direction/polarization vectors. `record.npy[i, s]` is the
same layout for photon `i` at step slot `s`. Unused record slots are zero-filled;
used slots are expected to be contiguous from slot 0 until the first all-zero
step or until the configured record depth is reached.

### Packed fields

Interpret the packed fields with `arr.view(np.uint32)`.

`q1.w` packs hit count and intersected-geometry instance index:

```text
bits 31-16: hitcount
bits 15-0 : iindex
```

`q3.x` packs the terminal flag, boundary, and orientation:

```text
bit 31     : orient sign, where 1 means orient = -1 and 0 means orient = +1
bits 30-16: boundary index, 15 bits
bits 15-0 : terminal flag from OpticksPhoton.h
```

Use these exact masks and shifts:

```python
obf = q3[..., 0]
flag = obf & 0x0000FFFF
boundary = (obf & 0x7FFF0000) >> 16
orient = np.where((obf & 0x80000000) != 0, -1.0, 1.0)
```

`q3.y` stores a 24-bit identity in its low bits and extends the photon index in
its high 8 bits. `q3.z` stores the low 32 bits of the photon index:

```python
identity = q3[..., 1] & 0x00FFFFFF
index = ((q3[..., 1].astype(np.uint64) >> 24) << 32) | q3[..., 2].astype(np.uint64)
flagmask = q3[..., 3]
```

`identity` is currently `sensor_identifier + 1`; zero means the photon did not
end on a sensor. `flagmask` is the cumulative OR of every flag set during the
photon lifetime. Each `sphoton::set_flag(f)` updates both the terminal `flag`
and `flagmask |= f`.

### Flag values

The low 16 `OpticksPhoton.h` flags are the values stored by the terminal
`flag` field and cumulative `flagmask`.

| Flag | Hex | Abbrev | Meaning |
|------|-----|--------|---------|
| `CERENKOV` | `0x0001` | `CK` | Cerenkov generation |
| `SCINTILLATION` | `0x0002` | `SI` | Scintillation generation |
| `TORCH` | `0x0004` | `TO` | Torch source |
| `BULK_ABSORB` | `0x0008` | `AB` | Absorbed in bulk material |
| `BULK_REEMIT` | `0x0010` | `RE` | Re-emitted, including WLS |
| `BULK_SCATTER` | `0x0020` | `SC` | Rayleigh scattered |
| `SURFACE_DETECT` | `0x0040` | `SD` | Detected at a surface |
| `SURFACE_ABSORB` | `0x0080` | `SA` | Absorbed at a surface |
| `SURFACE_DREFLECT` | `0x0100` | `DR` | Diffuse reflection at a surface |
| `SURFACE_SREFLECT` | `0x0200` | `SR` | Specular reflection at a surface |
| `BOUNDARY_REFLECT` | `0x0400` | `BR` | Fresnel reflection at a boundary |
| `BOUNDARY_TRANSMIT` | `0x0800` | `BT` | Transmitted through a boundary |
| `NAN_ABORT` | `0x1000` | `NA` | Aborted due to NaN, usually geometry-related |
| `EFFICIENCY_COLLECT` | `0x2000` | `EC` | Collected by PMT efficiency handling |
| `EFFICIENCY_CULL` | `0x4000` | `EL` | Culled by PMT efficiency handling |
| `MISS` | `0x8000` | `MI` | Missed all geometry |

### Sequence layout

`seq.npy` stores `sseq` as `(N, 2, 2)` `uint64`:

```python
seqhis = seq[:, 0, :]  # two uint64 words per photon
seqbnd = seq[:, 1, :]  # two uint64 words per photon
```

`sseq::NSEQ == 2`, `sseq::BITS == 4`, `sseq::SLOTMAX == 16`, and
`sseq::SLOTS == 32`. Each photon therefore has 32 chronological history slots.
Slot `0..15` is stored in word 0; slot `16..31` is stored in word 1. Within a
word, each slot is one 4-bit nibble:

```python
iseq = slot // 16
shift = 4 * (slot - iseq * 16)
nibble = (seqhis[i, iseq] >> shift) & 0xF
flag = 0 if nibble == 0 else 1 << (nibble - 1)
```

The stored nibble is `FFS(flag) & 0xF`, where `FFS` is find-first-set using
one-based bit positions. For example, `TORCH` (`0x0004`) stores nibble `3`,
and `SURFACE_DETECT` (`0x0040`) stores nibble `7`. A zero nibble marks no
recorded flag and is commonly used as the end of the sequence. Because the
nibble has only four bits and zero is reserved, `MISS` (`0x8000`, FFS 16) is
not representable in `seq.npy` history slots.

`seqbnd` stores `boundary & 0xF` for the same slots. Use `record.npy` or
`photon.npy` when the full 15-bit boundary index is required.

### Hit semantics

Hits are selected from the final photon array by the configured hitmask:

```text
(flagmask & hitmask) == hitmask
```

The current default hitmask is `SD`, meaning `SURFACE_DETECT` (`0x0040`).
Some PMT efficiency workflows use `EC`, meaning `EFFICIENCY_COLLECT`
(`0x2000`). The app-level `Config` interface does not currently expose the
hitmask; the event metadata records the numeric `hitmask`, and readers should
prefer that metadata over hard-coded assumptions.

`hit.npy` is a copy of the selected rows from `photon.npy`, not a different
layout. The original photon index remains available through the packed `q3.y`
and `q3.z` index fields.

Photons that exhaust the bounce limit are not given a distinct terminal flag
by the output format. With default `MaxBounce=31`, debug records have up to 32
slots. A reader can identify likely bounce-limit truncation by checking photons
whose non-zero record-step count equals `record.shape[1]`.

### Python reader pattern

Use shape and dtype checks before decoding packed fields:

```python
from pathlib import Path
import numpy as np

event_dir = Path("path/from/Config.output_dir/ALL0_no_opticks_event_name/A000")
record = np.load(event_dir / "record.npy")

assert record.dtype == np.float32
assert record.ndim == 4 and record.shape[2:] == (4, 4)

u32 = record.view(np.uint32)
q3 = u32[..., 3, :]

terminal_flag = q3[..., 0] & 0x0000FFFF
boundary = (q3[..., 0] & 0x7FFF0000) >> 16
orient = np.where((q3[..., 0] & 0x80000000) != 0, -1.0, 1.0)
identity = q3[..., 1] & 0x00FFFFFF
photon_index = ((q3[..., 1].astype(np.uint64) >> 24) << 32) | q3[..., 2].astype(np.uint64)
flagmask = q3[..., 3]

step_used = np.any(record.reshape(record.shape[0], record.shape[1], 16) != 0, axis=2)
step_count = step_used.sum(axis=1)
```

For `photon.npy`, `hit.npy`, and `inphoton.npy`, use the same decoding with
shape `(N, 4, 4)` and `q3 = arr.view(np.uint32)[:, 3, :]`.
