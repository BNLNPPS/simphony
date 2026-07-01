# Simulation inputs

This guide collects the runtime configuration, application entry points, and
GDML requirements that were previously documented in the top-level README. For
the optical-surface physics implemented by Simphony, see [Physics](physics.md).

## Torch configuration

`GPUPhotonSource` and `GPUPhotonSourceMinimal` read photon source parameters
from a JSON config file, by default `config/dev.json`.

| Field | Description |
|-------|-------------|
| `type` | Source shape: `disc`, `sphere`, `point` |
| `radius` | Size of the source area (mm) |
| `pos` | Center position `[x, y, z]` (mm) |
| `mom` | Emission direction `[x, y, z]` (normalized automatically) |
| `numphoton` | Number of photons to generate |
| `wavelength` | Photon wavelength (nm) |

## Application input modes

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

`GPUCerenkov` and `GPURaytrace` collect gensteps from charged particle
interactions and pass them to Simphony for GPU photon generation and tracing.
`GPUPhotonSource` and `GPUPhotonSourceMinimal` instead generate photons
directly from a torch configuration. `GPUPhotonSource` runs both G4 and GPU
tracking for validation, while `GPUPhotonSourceMinimal` skips G4 tracking
entirely to show the minimal GPU-only path. `GPUPhotonFileSource` reads
photons from a user-provided text file, enabling custom photon distributions
without code changes.

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

## Defining primary particles

There are several inputs that the user or developer has to define. In
`src/GPUCerenkov`, which imports `src/GPUCerenkov.h`, the code provides a
working example with a simple geometry. The user or developer has to change
the following details: the number of primary particles to simulate in a macro
file and the number of G4 threads. For example:

```text
/run/numberOfThreads {threads}
/run/verbose 1
/process/optical/cerenkov/setStackPhotons {flag}
/run/initialize
/run/beamOn 500
```

Here `setStackPhotons` defines whether G4 will propagate optical photons or
not. In production, Simphony on the GPU takes care of optical photon
propagation. Additionally, the user has to define the starting position,
momentum, and related settings of the primary particles in the
`GeneratePrimaries` function in `src/GPUCerenkov.h`. The hits of the optical
photons are returned in the `EndOfRunAction` function. If more photons are
simulated than can fit in GPU RAM, the execution of a GPU call should be moved
to `EndOfEventAction` together with retrieving the hits.

## Loading geometry into Simphony

Simphony can import geometries in GDML format automatically. About ten
primitives are supported now, for example `G4Box`. `G4Trd` and `G4Trap` are
not supported yet. `GPUCerenkov` takes GDML files through command line
arguments, for example `GPUCerenkov -g mygdml.gdml`.

The GDML must define all optical properties of surfaces and materials,
including the properties used by the standard GPU path:

- Efficiency, used by Simphony to specify detection efficiency and assign
  sensitive surfaces
- Material refractive index, used for optical boundary transmission and
  reflection
- Material group velocity
- Reflectivity, used for non-sensor specular or diffuse explicit surfaces
- Additional Geant4 material and surface optical properties needed for CPU-side
  Geant4 validation, noting that not all of them are used by Simphony's
  standard GPU propagation

See [Physics](physics.md) for the current standard-GPU interpretation of GDML
optical-surface properties such as `model`, `finish`, `type`, `EFFICIENCY`,
and `REFLECTIVITY`.
