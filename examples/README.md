## Examples

Simphony provides several examples demonstrating GPU-accelerated optical photon simulation:

| Example | Physics | Geometry | Use Case |
|---------|---------|----------|----------|
| `simphox` | Optical photons (torch) | None | External project build and CPU/GPU photon generation smoke test |
| `GPUCerenkov` | Cerenkov only | Simple nested boxes (raindrop) | Basic Cerenkov testing |
| `GPURaytrace` | Cerenkov + Scintillation | 8x8 CsI crystal + SiPM array | Realistic detector simulation |
| `GPUPhotonSource` | Optical photons (torch) | Any GDML | G4 + GPU side-by-side validation |
| `GPUPhotonSourceMinimal` | Optical photons (torch) | Any GDML | GPU-only test |
| `GPUPhotonFileSource` | Optical photons (text file) | Any GDML | GPU-only, user-defined photons from file |
| WLS test | Wavelength shifting | WLS sphere + detector shell | Validate GPU WLS physics |

### Application capabilities

The example applications differ mainly in where photons originate and whether
Geant4 optical-photon tracking is also run for validation.

| Feature | GPUCerenkov | GPURaytrace | GPUPhotonSource | GPUPhotonSourceMinimal | GPUPhotonFileSource |
|---------|-------------|-------------|-----------------|------------------------|---------------------|
| Cerenkov genstep collection | Yes | Yes | No | No | No |
| Scintillation genstep collection | No | Yes | No | No | No |
| Wavelength shifting (WLS) | Yes | Yes | Yes | Yes | Yes |
| Torch photon generation | No | No | Yes | Yes | No |
| Photon input from text file | No | No | No | No | Yes |
| G4 optical photon tracking | Yes | Yes | Yes | No | No |
| GPU simulation (Simphony) | Yes | Yes | Yes | Yes | Yes |
| Multi-threaded | Yes | Yes | No | No | No |

`GPUCerenkov` and `GPURaytrace` collect gensteps from charged-particle
interactions and pass them to Simphony for GPU photon generation and tracing.
`GPUPhotonSource` and `GPUPhotonSourceMinimal` generate photons directly from
a torch configuration. `GPUPhotonSource` runs both G4 and GPU tracking for
validation, while `GPUPhotonSourceMinimal` keeps only the GPU path.
`GPUPhotonFileSource` reads user-defined photons from a text file.

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

### Example 4: GPUPhotonSource (G4 + GPU Validation)

`GPUPhotonSource` generates optical photons from a configurable torch source and runs
both Geant4 and Simphony GPU simulation in parallel on the same input photons. This
enables direct comparison of hit counts and positions between the two engines.

Both engines detect photons using the same mechanism: border surface physics. On the G4
side the `SteppingAction` records a hit when `G4OpBoundaryProcess` reports Detection at
the optical surface, matching how Simphony detects photons on the GPU.

| Argument | Description | Default |
|----------|-------------|---------|
| `-g, --gdml` | Path to GDML file | `geom.gdml` |
| `-c, --config` | Config file name (without `.json`) | `dev` |
| `-m, --macro` | Path to G4 macro | `run.mac` |
| `-i, --interactive` | Open interactive viewer | off |
| `-s, --seed` | Fixed random seed | time-based |

```bash
GPUPhotonSource -g tests/geom/opticks_raindrop.gdml -c dev -m run.mac -s 42
```

**Output:**
- `opticks_hits_output.txt` — Simphony GPU hits, one line per hit
- `g4_hits_output.txt` — Geant4 hits in the same format

Hit format (both files): `time wavelength (pos_x, pos_y, pos_z) (mom_x, mom_y, mom_z) (pol_x, pol_y, pol_z)`

**Source files:** `src/GPUPhotonSource.cpp`, `src/GPUPhotonSource.h`

### Example 5: GPUPhotonSourceMinimal (GPU-Only)

`GPUPhotonSourceMinimal` is a stripped-down version of `GPUPhotonSource` that runs
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
