# Geometry Requirements for Optical Photon Studies

This note turns the current `simphony` geometry capabilities into practical
requirements for users authoring detector geometry in GDML/Geant4.

The goal is not only to make a geometry load successfully, but to make it usable
for optical photon studies where boundary normals, material interfaces, surface
definitions, and acceleration structure layout all matter.

## Scope

The current codebase supports:

- Analytic CSG geometry built from supported primitives and booleans.
- A mixed analytic + triangle OptiX 7 scene.
- Selective triangulation of chosen solids in the global, non-instanced part of
  the geometry.

The current codebase does not yet support:

- Arbitrary per-placement triangulation.
- Triangulated instanced/factor solids (`Q` in the `stree` comments).
- Mixing analytic and triangle content inside the same imported compound solid.

Those constraints come directly from the active geometry path in
`sysrap/stree.h`, `sysrap/SScene.h`, `CSG/CSGImport.cc`, and
`CSGOptiX/SBT.cc`.

## Design Goal

If your experiment depends on optical photons, create geometry that is:

- Physically correct at optical boundaries.
- Stable under Geant4 and OptiX import.
- Structured so important detector parts can remain analytic when possible.
- Structured so unsupported or awkward solids can be isolated for selective
  triangulation when needed.

## Hard Requirements

### 1. Give solids stable, unique, meaningful names

Triangulation selection is currently tied to solid identity, not to an
individual placement in the geometry tree. In practice that means:

- Every solid that might need special handling should have a stable `<solid
  name="...">`.
- Avoid anonymous or auto-generated names that change between exports.
- Use names that reflect function, for example
  `PMTWindow`, `LightGuideCone`, `SupportRingTorus`.

This matters because the current workflow matches triangulation candidates
using solid names/LVIDs derived from the imported Geant4 solids.

### 2. Keep repeated detector modules as clean repeated logical-volume subtrees

The current acceleration structure logic distinguishes between:

- Global non-instanced remainder geometry.
- Repeated factor geometry.

If a detector element is naturally repeated, model it as one logical volume
subtree placed many times, rather than copying many near-identical solids with
small naming differences.

This is important because the current code handles repeated structure through
factorization. Clean repetition is the path that scales best for analytic CSG.

### 3. Do not assume you can triangulate one copy of a repeated volume

Today, if a solid lives inside repeated/instanced geometry, the triangulated
path is not generally available for it. The current selective triangulation
flow is limited to global, non-instanced solids.

So if you know in advance that a complex shape will require triangulation:

- Prefer placing it in the non-instanced/global remainder when that is
  reasonable.
- Do not rely on "triangulate only one placement" as a design strategy.

If repeated triangulated geometry becomes necessary, the missing `Q`
triangulated-factor path will need to be implemented in the code.

### 4. Prefer supported analytic primitives for optical-critical volumes

For volumes that dominate optical transport, prefer shapes that the code can
represent analytically:

- boxes
- cylinders/tubes
- spheres/zspheres
- cones
- convex polyhedra
- supported boolean combinations of those primitives

Analytic geometry keeps exact surfaces and analytic normals, which is usually
the safer starting point for optical studies.

### 5. Isolate unsupported or awkward shapes into separate solids

Some Geant4 shapes are currently clear triangulation candidates, including:

- torus
- cut tubs / cut cylinders
- shapes imported as `notsupported`
- `G4MultiUnion` content that does not reduce to supported analytic primitives

If your apparatus needs these, isolate them so they can be triangulated
selectively instead of forcing a larger detector region away from the analytic
path.

### 6. Keep optical boundaries simple, explicit, and watertight

Optical behavior is driven by interfaces, so geometry authors should:

- Make each optical medium a distinct volume.
- Avoid overlaps and coincident-but-ambiguous surfaces.
- Avoid tiny sliver volumes unless they are physically required.
- Ensure windows, grease layers, coatings, photocathodes, and scintillators
  are modeled as explicit, separable volumes when they matter physically.

If a boundary is important in physics, it should be obvious in the geometry
tree and easy to identify by volume name.

### 7. Define materials and optical surfaces explicitly in GDML/Geant4

Geometry alone is not enough for optical work. The input model should include:

- Materials with the required optical properties over wavelength/energy.
- Border or skin surfaces where detector response depends on finish/model/type.
- A clear convention for detector surfaces versus passive interfaces.

The geometry hierarchy should support those definitions cleanly. Avoid designs
where the only way to identify a critical surface is through fragile name
matching after import.

### 8. Avoid deep, fragile boolean trees when a simpler decomposition works

Analytic CSG can represent booleans, but very deep or awkward boolean trees are
harder to debug, harder to validate, and more likely to push the geometry
toward triangulation.

Prefer:

- Simple constructive trees.
- Smaller solids assembled hierarchically.
- Reuse of the same logical volumes where possible.

Do not encode mechanical detail that is irrelevant to optical transport if it
only adds boolean complexity.

## Requirements Specific to Triangulated Solids

### 9. Treat triangle meshes as approximations, not exact replacements

Triangle meshes in this code are derived from Geant4 polyhedra. That means:

- Curved surfaces are approximated by facets.
- Surface normals come from triangle geometry.
- Optical results can depend on tessellation quality.

For curved optical interfaces, coarse tessellation can change reflection,
refraction, and detection behavior enough to matter.

### 10. Document and control mesh resolution for curved solids

If you triangulate toroidal or other curved geometry, record the mesh
generation settings used for that study. The code already supports per-shape
rotation-step tuning through `U4Mesh` environment variables such as
`U4Mesh__NumberOfRotationSteps_*`.

Two runs that use different tessellation settings should be treated as
different geometry definitions for validation purposes.

### 11. Triangulate only where the physics or implementation requires it

Do not switch to triangles for the entire detector unless there is a strong
reason. The current mixed-mode design is better used selectively:

- Keep repeated, primitive-friendly optical detectors analytic.
- Use triangles for unsupported or geometrically awkward support structures,
  envelopes, or one-off solids.

This minimizes approximation error and preserves the scalability benefits of
analytic factorized geometry.

## Hierarchy Recommendations for GDML Authors

### 12. Separate apparatus into three conceptual classes

When authoring GDML, think in these classes:

- Analytic repeated detector modules.
- Analytic global remainder geometry.
- One-off triangulation candidates.

That conceptual split aligns well with the current `R`, `F`, and `T` handling
in the importer.

### 13. Make triangulation candidates topologically isolated

A good triangulation candidate is:

- A single named solid or a small local subtree.
- Not heavily reused by instancing.
- Not interwoven with many analytic booleans.

This makes future force-triangulation far easier to control and validate.

### 14. Preserve detector modularity in the logical-volume tree

For optical detector systems, use a hierarchy where each module contains clear
sub-volumes for:

- radiator or scintillator
- optical coupling layer
- window
- sensor active region
- passive support

That organization helps both physics debugging and future acceleration-structure
classification.

### 15. Keep world and support geometry simpler than the detector itself

If support frames, shells, or mechanical covers are much more complex than the
optical detector elements, keep them in separable regions of the hierarchy.

That makes it easier to leave the detector analytic while triangulating only
the mechanically complex remainder.

## Recommended Workflow for New Apparatus Designs

1. Start with an analytic-first geometry for the optical path.
2. Give every potentially special solid a stable, human-readable name.
3. Build repeated detector channels as repeated logical-volume subtrees.
4. Isolate torus, cut-tubs, and other awkward shapes into named one-off solids.
5. Validate optical surfaces and material tables before performance work.
6. Only then evaluate whether selected solids should be triangulated.

## Validation Requirements Before Trusting Results

Any geometry intended for optical performance or physics studies should be
validated in at least three ways:

- Geometry validation: no overlaps, correct hierarchy, expected surface count.
- Physics validation: compare hit statistics and boundary-state histories
  against Geant4 for representative photon sources.
- Resolution validation: for triangulated curved solids, demonstrate that the
  results are stable under finer tessellation.

If a result changes significantly when the mesh is refined, the mesh is not yet
good enough for production optical studies.

## Practical Authoring Checklist

Before handing a GDML geometry to `simphony`, verify:

- All important solids have stable, meaningful names.
- Repeated detector modules are expressed as repeated logical-volume subtrees.
- Optical-critical parts use supported analytic primitives when possible.
- Unsupported shapes are isolated and easy to target for triangulation.
- Important interfaces are explicit volume boundaries.
- Materials and optical surfaces are fully defined.
- There are no unintentional overlaps or microscopic gaps.
- Curved triangulated surfaces have documented tessellation settings.
- The study plan includes analytic-versus-triangle and Geant4 comparison tests.

## Current Best-Practice Summary

For the current state of this repo, the safest geometry strategy for optical
photon work is:

- Analytic CSG for repeated detector modules and optical-critical interfaces.
- Selective triangulation only for isolated global solids that are unsupported
  or awkward analytically.
- Clear naming and hierarchy in GDML so future mixed analytic/triangle support
  can be extended without redesigning the apparatus model.
