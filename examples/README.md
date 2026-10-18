## Examples

Simphony provides several examples demonstrating GPU-accelerated optical photon simulation:

| Example | Physics | Geometry | Use Case |
|---------|---------|----------|----------|
| `simphox` | Optical photons (torch) | None | External project build and CPU/GPU photon generation smoke test |
| `GPUCerenkov` | Cerenkov only | Simple nested boxes (raindrop) | Basic Cerenkov testing |
| `GPURaytrace` | Cerenkov + Scintillation | 8x8 CsI crystal + SiPM array | Realistic detector simulation |
| `simg4ox` | Optical photons (torch) | Supported GDML | Serial/MT G4 + GPU side-by-side validation |
| `GPUPhotonSourceMinimal` | Optical photons (torch) | Supported GDML | GPU-only test |
| `GPUPhotonFileSource` | Optical photons (text file) | Supported GDML | GPU-only, user-defined photons from file |
| WLS test | Wavelength shifting | WLS sphere + detector shell | Validate GPU WLS physics |

Here, "Supported GDML" means that every Geant4 solid and parameter
combination in the file is handled by Simphony. See the [Geometry
guide](../docs/geometry-requirements.md) for the current support table and a
conversion-first workflow.

### Building an external application

After installing Simphony, standalone Geant4 applications link the public
runtime target:

```cmake
find_package(simphony CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE simphony::simphony)
target_compile_features(my_application PRIVATE cxx_std_20)
```

Include `simphony.h`, then call `simphony::initialize(world)` once the Geant4
world is complete. For a custom sensor mapping, implement
`simphony::SensorIdentifier` (included by that header) and pass it through
`simphony::GeometryOptions::sensor_identifier`. Keep the world and identifier
alive for the process lifetime. Reinitialization with the same inputs reuses
the runtime; different inputs are rejected.

After preparing gensteps in `SEvt`, call `simphony::simulate(event_id, false)`,
retrieve results, and call `simphony::reset(event_id)` before the next launch.
Async applications must serialize the entire genstep / launch / result / reset
sequence because `SEvt` is process-wide. The `MC_Truth`, `async_gpu_launch`,
and `async_gpu_std` directories show standalone builds against the installed
package. Low-level event access still uses the installed SysRap headers.

The DD4hep plugins use the same runtime. Build them in-tree when DD4hep is
available or configure `dd4hepplugins/` against the installed package. The
`OpticsRun` action reuses geometry across runs, flushes remaining batches at
end-of-run, and retains the process-wide runtime. Set `SaveGeometry = True`
and `GeometryDirectory` (default `geometry`) to export `origin.gdml` and
`CSGFoundry/` to an explicit destination.

For conversion without a GPU, use `GpuRequirement::Disabled` and
`simphony::save_geometry(directory)`, or run `consgeo -g detector.gdml -o csg`.
Saved-foundry rendering and simtrace remain available through the lower-level
`CSGOptiX` tools; the public runtime initializes from a live Geant4 world.

### Application capabilities

The example applications differ mainly in where photons originate and whether
Geant4 optical-photon tracking is also run for validation.

| Feature | GPUCerenkov | GPURaytrace | simg4ox | GPUPhotonSourceMinimal | GPUPhotonFileSource |
|---------|-------------|-------------|-----------------|------------------------|---------------------|
| Cerenkov genstep collection | Yes | Yes | Yes | No | No |
| Scintillation genstep collection | No | Yes | Yes | No | No |
| Wavelength shifting (WLS) | Yes | Yes | Yes | Yes | Yes |
| Torch photon generation | No | No | Yes | Yes | No |
| Photon input from text file | No | No | No | No | Yes |
| G4 optical photon tracking | Yes | Yes | Yes | No | No |
| GPU simulation (Simphony) | Yes | Yes | Yes | Yes | Yes |
| Geant4 CPU multithreading | Yes | Yes | Yes | No | No |

`GPUCerenkov` and `GPURaytrace` collect gensteps from charged-particle
interactions and pass them to Simphony for GPU photon generation and tracing.
`simg4ox` accepts a torch configuration or charged particles (`--particle`)
for Cerenkov and scintillation gensteps. `GPUPhotonSourceMinimal` uses a
torch configuration. `simg4ox` runs both G4 and GPU tracking for
validation, while `GPUPhotonSourceMinimal` keeps only the GPU path.
`GPUPhotonFileSource` reads user-defined photons from a text file.

For `simg4ox`, multithreading applies to Geant4 CPU tracking. Its process-wide
Opticks event context is protected by serializing GPU launches in event-ID
order.

### Example 1: simphox (External build smoke test)

Assuming Simphony is properly installed on the system, compile and run this
example from `examples/simphox`:

```bash
cmake -S . -B build
cmake --build build
./build/simphox --cpu
./build/simphox --gpu
```

It generates a configurable set of optical photons using the built-in torch
configuration, converts them into an NP array, prints the data, and saves it as
`out/photons.npy`. The default `cpu` backend generates photons on the host. The
`gpu` backend generates the same kind of torch photons in a CUDA kernel and
copies them back before writing the same output file.


### Example 2: GPUCerenkov (Cerenkov Only)

The `GPUCerenkov` example uses the **opticks_raindrop** geometry - a simple nested box configuration
designed for testing Cerenkov photon production and GPU raytracing:

```
Geometry: opticks_raindrop.gdml
├── VACUUM world (240×240×240 mm)
│   └── Lead box (220×220×220 mm)
│       └── Air box (200×200×200 mm)
│           └── Water box (100×100×100 mm) ← Cerenkov medium
```

When charged particles traverse the water volume above the Cerenkov threshold, optical photons
are generated and traced on the GPU. This is a minimal example for validating the Simphony pipeline.

```bash
# Run with raindrop geometry (Cerenkov only)
GPUCerenkov -g tests/geom/opticks_raindrop.gdml -m run.mac
```

**Source files:** `src/GPUCerenkov.cpp`, `src/GPUCerenkov.h`

### Example 3: GPURaytrace (Cerenkov + Scintillation)

The `GPURaytrace` example demonstrates a realistic detector configuration with both Cerenkov
and scintillation physics using the **8x8 SiPM array** geometry (not validated yet):

```
Geometry: 8x8SiPM_w_CSI_optial_grease.gdml
├── Air world (100×100×100 mm)
│   ├── 64 CsI crystal pixels (2×2×8 mm each) ← Scintillation + Cerenkov
│   ├── Optical grease layer (17.4×17.4×0.1 mm)
│   ├── Entrance window (17.4×17.4×0.1 mm)
│   └── 64 SiPM active areas (2×2×0.2 mm each) ← Photon detectors
```

This geometry models a pixelated scintillator calorimeter with:
- **CsI(Tl) crystals**: Produce both Cerenkov and scintillation photons
- **Optical coupling**: Grease and window layers for photon transmission
- **SiPM readout**: 8×8 array of silicon photomultipliers with dead space modeling

```bash
# Run with 8x8 SiPM array geometry (Cerenkov + Scintillation)
GPURaytrace -g tests/geom/8x8SiPM_w_CSI_optial_grease.gdml -m tests/run.mac

# Check output for Cerenkov (ID=0) and scintillation (ID=1) photons
grep -c "CreationProcessID=0" opticks_hits_output.txt  # Cerenkov
grep -c "CreationProcessID=1" opticks_hits_output.txt  # Scintillation
```

**Source files:** `src/GPURaytrace.cpp`, `src/GPURaytrace.h`

### Example 4: simg4ox (G4 + GPU Validation)

`simg4ox` generates optical photons from a configurable torch source and
tracks the same input with Geant4 on the CPU and Simphony on the GPU. This
enables direct comparison of hit counts and positions between the two engines.

Both engines detect photons using border-surface physics. On the Geant4 side,
the optical boundary process invokes the worker-local `PhotonSD` at configured
sensitive surfaces; the detector records and terminates the photon. Simphony
selects GPU hits from the corresponding optical-boundary result.

| Argument | Description | Default |
|----------|-------------|---------|
| `-g, --gdml` | Path to GDML file | `geom.gdml` |
| `-c, --config` | Config file name (without `.json`) | `dev` |
| `-m, --macro` | Path to G4 macro | `run.mac` |
| `-i, --interactive` | Open interactive viewer | off |
| `-s, --seed` | Fixed random seed | Geant4 default |
| `-t, --threads` | Geant4 CPU threads; `1` selects the serial run manager | `1` |

The same executable supports serial and multi-threaded Geant4 runs:

```bash
# Serial Geant4 (default)
simg4ox -g tests/geom/opticks_raindrop.gdml -c dev -m tests/run_5evt.mac -s 42

# Geant4 MT with four CPU workers
simg4ox -g tests/geom/opticks_raindrop.gdml -c dev -m tests/run_mt.mac -s 42 --threads 4
```

The thread count is a command-line option because the executable must select
the serial or MT run-manager type before Geant4 reads a macro. The example
`tests/run_mt.mac` contains the remaining run initialization and event-count
commands.

In MT mode, Geant4 actions and sensitive detectors are worker-local and the
run-wide results are merged in event-ID order. Opticks currently exposes one
process-wide GPU event context, so GPU launches are serialized in that same
order while Geant4 CPU tracking remains multi-threaded. Full CPU-side Opticks
photon-history recording remains available in serial mode; both modes write
the run-wide hit arrays below.

**Output:**

- `s_hits.npy` — Simphony GPU hits
- `g_hits.npy` — Geant4 hits

Both arrays use the `sphoton` layout and are written under the configured
`event.output_dir`.

**Source files:** `src/simg4ox.cpp`, `src/g4app.h`

### Example 5: GPUPhotonSourceMinimal (GPU-Only)

`GPUPhotonSourceMinimal` is a stripped-down version of `simg4ox` that runs
**only** Simphony GPU simulation. All G4 optical photon tracking infrastructure
(sensitive detectors, stepping actions, tracking actions) is removed. Geant4 is used
solely for geometry loading and hosting the event loop.

Use this when you only need GPU results and want faster execution.

| Argument | Description | Default |
|----------|-------------|---------|
| `-g, --gdml` | Path to GDML file | `geom.gdml` |
| `-c, --config` | Config file name (without `.json`) | `dev` |
| `-m, --macro` | Path to G4 macro | `run.mac` |
| `-i, --interactive` | Open interactive viewer | off |
| `-s, --seed` | Fixed random seed | time-based |

```bash
GPUPhotonSourceMinimal -g tests/geom/opticks_raindrop.gdml -c dev -m run.mac -s 42
```

**Output:** `opticks_hits_output.txt` — one hit per line

**Source files:** `src/GPUPhotonSourceMinimal.cpp`, `src/GPUPhotonSourceMinimal.h`

### Example 6: GPUPhotonFileSource (File Input, GPU-Only)

`GPUPhotonFileSource` reads optical photons from a plain text file and runs
GPU-only simulation via Simphony. Each line in the input file defines one
photon with 11 space-separated values:

```
# pos_x pos_y pos_z time mom_x mom_y mom_z pol_x pol_y pol_z wavelength
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  420.0
-10.0 -30.0 -90.0  0.0  0.0 0.287348 0.957826  1.0 0.0 0.0  450.0
```

- Positions are in mm, time in ns, wavelength in nm
- Momentum direction should be normalized
- Polarization should be transverse to momentum and normalized
- Lines starting with `#` are comments and blank lines are skipped

| Argument | Description | Default |
|----------|-------------|---------|
| `-g, --gdml` | Path to GDML file | `geom.gdml` |
| `-p, --photons` | Path to input photon text file | (required) |
| `-m, --macro` | Path to G4 macro | `run.mac` |
| `-i, --interactive` | Open interactive viewer | off |
| `-s, --seed` | Fixed random seed | time-based |

```bash
GPUPhotonFileSource -g tests/geom/opticks_raindrop.gdml -p my_photons.txt -m run.mac
```

**Output:** `opticks_hits_output.txt` — one hit per line

**Source files:** `src/GPUPhotonFileSource.cpp`, `src/GPUPhotonFileSource.h`

### Example 7: Wavelength Shifting (WLS) Test

This test validates the GPU wavelength shifting implementation using a dedicated
geometry with a WLS sphere surrounded by a detector shell:

```
Geometry: wls_test.gdml
├── Air world (r=200 mm)
│   ├── WLS sphere (r=20 mm) ← Absorbs UV, re-emits visible
│   └── Glass detector shell (r=28-30 mm) ← 100% detection efficiency
```

The WLS material absorbs UV photons (350 nm) and re-emits them isotropically at
longer wavelengths (peak ~481 nm) with a 0.5 ns exponential time delay. The test
fires 1000 monochromatic 350 nm photons from the origin into the WLS sphere.

```bash
GPUPhotonSourceMinimal -g tests/geom/wls_test.gdml -c wls_test -m tests/run.mac -s 42
```

**Expected results:**
- ~990/1000 photons detected (10 absorbed after failing energy conservation)
- All hits wavelength-shifted from 350 nm to mean ~487 nm
- Energy conservation: no hits with wavelength < 350 nm
- Isotropic re-emission: mean momentum direction near zero
- Time delay: mean ~0.6 ns (propagation + 0.5 ns exponential WLS decay)

**GDML WLS properties required** (same syntax for G4 10.x and 11.x):
```xml
<define>
    <matrix coldim="2" name="WLSABSLENGTH" values="1.77e-06 10000.0 ... 4.13e-06 0.01"/>
    <matrix coldim="2" name="WLSCOMPONENT" values="1.77e-06 0.00 ... 3.10e-06 0.00"/>
    <matrix coldim="1" name="WLSTIMECONSTANT" values="0.5"/>
</define>
<materials>
    <material name="WLSMaterial">
        <property name="WLSABSLENGTH" ref="WLSABSLENGTH"/>
        <property name="WLSCOMPONENT" ref="WLSCOMPONENT"/>
        <property name="WLSTIMECONSTANT" ref="WLSTIMECONSTANT"/>
    </material>
</materials>
```

Unlike scintillation properties, WLS property names are the same in both Geant4
10.x and 11.x — no dual-naming is needed.

**Test files:** `tests/geom/wls_test.gdml`, `config/wls_test.json`
**Implementation docs:** `docs/WLS_IMPLEMENTATION.md`
