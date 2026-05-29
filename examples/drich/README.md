# Standalone dRICH example (GDML, no DD4hep)

Runs the single-sector ePIC dRICH from a geoConverter-exported **GDML** through
**Geant4 (CPU) and Opticks (GPU) side-by-side** — no DD4hep dependency — and
compares optical-photon hits between the two engines.

This exercises the dRICH physics fixes in this branch's first commit
(`Add dRICH Cherenkov GPU/CPU physics fixes`). With those fixes the GPU
reproduces Geant4 CPU to within √N — e.g. ag02 (0.2 mm airgap), 10k muons,
GPU/CPU = 1.0005 ± 0.0016.

## Files
- `drich_gdml_main.cc` — the G4 + Opticks driver (env-var configured).
- `drich_ag02.gdml`    — single-sector dRICH geometry, 0.2 mm airgap.

## Prerequisites
A built + installed library from THIS branch, **including a rebuilt
`CSGOptiX7.ptx`**. Several fixes (ULP floor, phi-cut leaf clips, sibling-pair
carry, wavelength-linear lookup) live in `__device__` headers compiled into the
PTX, so the PTX must be regenerated — not just the host libraries.

## Build
Adjust `-I/-L` to your install layout. NOTE: the project was renamed
`eic-opticks` → `simphony`, so installed headers may live under
`include/simphony/...` (the recipe below used the old `eic-opticks` prefix).

```
g++ -std=c++17 examples/drich/drich_gdml_main.cc \
  $(geant4-config --cflags) $(geant4-config --libs) \
  -I${OPTICKS_PREFIX}/include/simphony \
  -I${OPTICKS_PREFIX}/include/simphony/g4cx \
  -I${OPTICKS_PREFIX}/include/simphony/sysrap \
  -I${OPTICKS_PREFIX}/include/simphony/u4 \
  -I${OPTICKS_PREFIX}/include/simphony/qudarap \
  -I${OPTICKS_PREFIX}/include/simphony/CSG \
  -I${OPTICKS_PREFIX}/include/simphony/CSGOptiX \
  -L${OPTICKS_PREFIX}/lib -lG4CX -lU4 -lQUDARap -lSysRap -lCSG -lCSGOptiX -lxerces-c \
  -o drich_gdml_main
```

## Run
```
QBND_FILTER_POINT=1 OPTICKS_MAX_SLOT=2000000 \
GDML_FILE=examples/drich/drich_ag02.gdml \
PARTICLE=mu- MOMENTUM_GEV=5 ETA=2.5 PHI_DEG=30 MULT=100 SEED=12345 NEVENTS=10 \
KILL_OPTICAL=0 DUMP_HITS=1 CUDA_VISIBLE_DEVICES=0 \
  ./drich_gdml_main
```
- `QBND_FILTER_POINT=1` is **required** — it selects the POINT bnd-texture path
  that the wavelength-linear 2-tap lookup depends on.
- For high-stats runs wrap each batch in `timeout --signal=KILL 600s`: a Geant4
  11.04 navigator bug can hang on a single optical photon at the aerogel/airgap
  shared face (CPU-side only; not an Opticks issue).

## Expected
GPU/CPU hit ratio ≈ 1.0 within √N. A difference is only meaningful if
|Δ| > 2·√N.

## Notes (slated for PR split, not yet done here)
- The driver carries the GDML-import workarounds **inline**: GeV→MeV
  property-energy rescale, EFFICIENCY-surface sensitive-detector attach (the
  GDML import has no SD), and a sensor-surface `REFLECTIVITY=0`/`dielectric_metal`
  override. These are specific to the SD-less GDML-import path; the plan is to
  move them into a reusable library utility and switch to SD-on-entry hit
  booking (dropping the surface override) so the standalone and DD4hep flows
  share one hit definition.
- One airgap is shipped (0.2 mm — inside the numerically-clean window). Airgaps
  below ~0.19 mm need an FP32 cone-intersect fix that is out of scope here.
