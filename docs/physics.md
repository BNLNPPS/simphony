# Physics

This guide describes the optical-photon physics behavior relevant to
Simphony's GPU propagation. For runtime configuration, GDML syntax
requirements, and application input modes, see [Simulation inputs](inputs.md).

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
boundary interactions, refer to the [Geant4 Application Developer Guide
boundary process](https://geant4-userdoc.web.cern.ch/UsersGuides/ForApplicationDeveloper/html/TrackingAndPhysics/physicsProcess.html#boundary-process).
The [UNIFIED model
section](https://geant4-userdoc.web.cern.ch/UsersGuides/ForApplicationDeveloper/html/TrackingAndPhysics/physicsProcess.html#the-unified-model)
is especially useful when comparing Geant4 behavior with Simphony's GPU
implementation.

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

## Optical surface support in Simphony

Simphony reads GDML through Geant4, so `<opticalsurface>` attributes such as
`model="glisur"`, `model="unified"`, `finish="ground"`, and
`type="dielectric_dielectric"` can be present in input files. During geometry
translation, Simphony also preserves the Geant4 surface metadata and material
property table arrays in the serialized surface fold. The standard GPU
propagation path, however, implements a smaller optical-surface model than
Geant4.

For ordinary explicit surfaces, Simphony builds a four-probability surface
payload:

```text
(detect, absorb, reflect_specular, reflect_diffuse)
```

This is done in `u4/U4SurfaceArray.h` and consumed by
`qsim::propagate_at_surface` in `qudarap/qsim.h`. The supported ordinary
surface behavior is:

| GDML input | Standard GPU behavior |
|------------|-----------------------|
| `EFFICIENCY` with any value greater than zero | Treats the surface as sensor-like: detect with `EFFICIENCY`, absorb with `1 - EFFICIENCY`, and do not reflect. |
| No nonzero `EFFICIENCY`, with `REFLECTIVITY` and a polished finish | Specular reflection with probability `REFLECTIVITY`; otherwise absorption. |
| No nonzero `EFFICIENCY`, with `REFLECTIVITY` and a ground finish | Diffuse/Lambertian reflection with probability `REFLECTIVITY`; otherwise absorption. |
| Boundary from a material with `RINDEX` to a material without `RINDEX` | Adds an implicit perfect absorber, matching Geant4's stop-and-kill behavior for non-transparent boundaries. |

When no explicit optical surface is present and both materials have `RINDEX`,
Simphony uses the material refractive indices in `qsim::propagate_at_boundary`
to perform Fresnel reflection or transmission with polarization handling.
This corresponds to Geant4's smooth dielectric-dielectric boundary case.

Several Geant4 optical-surface features are currently parsed or preserved as
metadata, but are not used by the standard GPU surface propagation:

- `model="glisur"` and `model="unified"` are recorded, but ordinary GPU
  propagation is not selected from the full Geant4 model implementation.
- The `value` attribute is stored as polish for `glisur` and as `sigma_alpha`
  for non-`glisur` models, but normal smearing from polish or `sigma_alpha` is
  not active in the ordinary surface path.
- `type`, such as `dielectric_metal` or `dielectric_dielectric`, is recorded,
  but ordinary explicit surfaces are still reduced to the probability payload
  above.
- UNIFIED-model properties such as `SPECULARLOBECONSTANT`,
  `SPECULARSPIKECONSTANT`, `BACKSCATTERCONSTANT`, and `TRANSMITTANCE` are not
  used by the ordinary GPU surface path.
- `RINDEX` or `GROUPVEL` properties attached to an optical surface are
  preserved, but the standard boundary calculation uses the corresponding
  material properties.
- LUT, DAVIS, dichroic, and coated/thin-film Geant4 models should not be
  treated as supported standard GDML surface features in Simphony.
- Only the common polished and ground finish families are recognized by the
  current helper code: `polished`, `polishedfrontpainted`,
  `polishedbackpainted`, `ground`, `groundfrontpainted`, and
  `groundbackpainted`.

For example, in `tests/geom/basic_detector.gdml`, the surface named
`medium_container_bs...` is a `ground` `glisur` surface with `REFLECTIVITY`.
In the standard GPU path it becomes a diffuse reflector. The
`medium_drop_surface` and `MirrorPyramid...` surfaces both define
`EFFICIENCY=1`, so they become fully detecting surfaces in the simplified
payload, even though they also define `REFLECTIVITY`.
