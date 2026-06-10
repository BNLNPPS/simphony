# Physics and simulation inputs

This guide collects the Geant4 optical-surface notes, Simphony source
configuration, and the input requirements that were previously documented in
the top-level README.

## Optical surface models in Geant4

In Geant4, optical surface properties such as `finish`, `model`, and `type`
are defined using enums in the `G4OpticalSurface` and `G4SurfaceProperty`
header files:

- [`G4OpticalSurface.hh`](https://github.com/Geant4/geant4/blob/geant4-11.3-release/source/materials/include/G4OpticalSurface.hh#L52-L113)
- [`G4SurfaceProperty.hh`](https://github.com/Geant4/geant4/blob/geant4-11.3-release/source/materials/include/G4SurfaceProperty.hh#L58-L68)

These enums allow users to configure how optical photons interact with
surfaces, controlling behaviors like reflection, transmission, and absorption
based on physical models and surface qualities. The string values
corresponding to these enums, such as `ground`, `glisur`, and
`dielectric_dielectric`, can also be used directly in GDML files when defining
`<opticalsurface>` elements for geometry. Geant4 parses these attributes and
applies the corresponding surface behavior.

For a physics-motivated explanation of how Geant4 handles optical photon
boundary interactions, refer to the [Geant4 Application Developer Guide -
Boundary
Process](https://geant4-userdoc.web.cern.ch/UsersGuides/ForApplicationDeveloper/html/TrackingAndPhysics/physicsProcess.html#boundary-process).

```gdml
<gdml>
  ...
  <solids>
    <opticalsurface finish="ground" model="glisur" name="medium_surface" type="dielectric_dielectric" value="1">
      <property name="EFFICIENCY" ref="EFFICIENCY_DEF"/>
      <property name="REFLECTIVITY" ref="REFLECTIVITY_DEF"/>
    </opticalsurface>
  </solids>
  ...
</gdml>
```

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

## Code differences

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

## GDML scintillation properties for Geant4 11.x and Simphony

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

## User and developer defined inputs

### Defining primary particles

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

### Loading geometry into Simphony

Simphony can import geometries in GDML format automatically. About ten
primitives are supported now, for example `G4Box`. `G4Trd` and `G4Trap` are
not supported yet. `GPUCerenkov` takes GDML files through command line
arguments, for example `GPUCerenkov -g mygdml.gdml`.

The GDML must define all optical properties of surfaces and materials,
including:

- Efficiency, used by Simphony to specify detection efficiency and assign
  sensitive surfaces
- Refractive index
- Group velocity
- Reflectivity
- Additional material and surface optical properties as required by the model
