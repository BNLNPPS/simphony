# synrad — GPU synchrotron-radiation soft X-ray transport

Transports soft X-rays (30 eV – 30 keV scale) through the vacuum of a
synchrotron-radiation beam chamber on the GPU. Photons fly at c through the
vacuum and interact only at the wall: at every vacuum→wall contact the photon
either reflects with a tabulated grazing-incidence Cu reflectivity or is
killed at the surface — the reflect-or-absorb model of the SynradG4
benchmark `GammaReflectionProcess`
([github.com/eicorg/SynradBenchmark](https://github.com/eicorg/SynradBenchmark)).
A reflected photon is specular with probability given by the Debye-Waller
factor of the surface roughness, otherwise diffusely perturbed. Photon energy
is carried in the `sphoton` wavelength slot, in keV. Physics in
`qudarap/qgxs.h`, dispatch in `qsim::propagate_gamma`.

## Geometry

Both GDMLs describe the same SynradBenchmark tunnel — a 50 m Cu chamber of
50×50 mm cross section: straight drift, 10 mrad horizontal arc, straight
drift — fused into ONE closed `G4TessellatedSolid`:

| file | facets | |
|---|---|---|
| `synrad_bench.gdml` | 1252 | arc as 150 chords (sagitta 0.28 µm), equivalent to the analytic benchmark chamber |
| `synrad_bench_tess.gdml` | ~25k | fine CAD-like tessellation of the same chamber |

`G4TessellatedSolid` is routed to the triangulated geometry path
(`stree__force_triangulate_solid`, set by the app for the bundled solid
names): the GPU intersects the exact facets, while `u4/U4Solid.h` only
provides an AABB placeholder on the analytic side.

## Run

```
SIMPHONY_PREFIX=/path/to/simphony/install ./run.sh
```

or directly:

```
synrad -g synrad_bench.gdml -n 1000000
```

The default gun is a pencil beam entering the first drift along +z with
0.5 mrad Gaussian smearing and log-uniform energy in 0.3–19.4 keV: the chamber
bends away underneath, so photons graze the outer arc wall at mrad angles and
skate downstream through reflection chains until absorbed — the
characteristic SR photon transport pattern. Expect the majority absorbed on
the arc/downstream wall, a reflected fraction that grows with decreasing
energy, and a small on-cap count at the absorber end plates (`-U`).

Options: `-r SIGMA_NM` roughness, `-T T_UM` autocorrelation length,
`-U ZLO,ZHI` absorber end planes,
`-I x,y,z,dx,dy,dz,emin,emax` gun, `-f FAN_MRAD`, `-i input_photons.npy` to
feed externally generated photons ((N,4,4) float32 sphoton, e.g. births
recorded by a Geant4 SR application), `-o OUTDIR`.

## Geant4 reference mode

`synrad_g4` transports the SAME photons (same seed, same gun in
`synrad_gun.h`) with a self-contained Geant4 application: the
SynradBenchmark geometry built from its native analytic CSG solids (drift
boxes and trapezoids, `G4Tubs` arc, dipole field, absorber end plates) and a
verbatim port of the benchmark's reflect-or-absorb `GammaReflectionProcess` —
the same compiled-in Cu table in double precision, native CLHEP
`setTheta`/`setPhi` diffuse smearing, and the reflected track continued
in place with `ProposeMomentumDirection`, exactly as the reference. There is
no custom transport machinery — the reference app has nothing special and
no extra overhead. Wall and absorber kills go to `synrad_g4_hits.npy` in the
same sphoton layout, and the summary prints `LOST`, the number of photons
that entered transport but never ended in a surface record — 0 in a valid
comparison.

`-g tess` swaps the two straight drifts for closed `G4TessellatedSolid`
meshes generated in code (rectangular rings every ~15 mm: 2668 and 21332
facets, CAD-like density) while the arc stays analytic — the standard
CPU-side way to run a meshed chamber. Caveat, measured on Geant4 11.4:
continuing a reflected track in place across a `G4TessellatedSolid` boundary
silently loses the track above a grazing-angle threshold between 10 and
20 mrad (the relocated on-facet point fails to re-enter the solid and the
photon free-flies to the world boundary with no warning; analytic solids are
robust at all angles, and ultra-grazing fans below ~2 mrad have a small
geometric escape tail on any solid). Synchrotron-radiation illumination
stays below the bend angle (10 mrad here) by construction, so the tess mode
is exercised with its native workload, the `-e` electron mode: stock
`G4SynchrotronRadiation` in the dipole field, a 30 eV stacking cut, and
`--killphotons` (SR gammas stack-killed at birth) for the Delta timing
method — `LOST` stays 0 there. Pencil beams steered above the threshold
onto tessellated walls are outside the in-place idiom's validity domain.

## Output

`synrad_hits.npy` / `synrad_g4_hits.npy` — (N_hit,4,4) float32 sphoton rows
of the wall-absorbed photons: position+time, direction, polarization, energy
(keV) — plus a one-line summary with the wall-absorbed /
reflected-at-least-once / on-cap counts and the transport time.
