#!/usr/bin/env python3
"""
CPU-only raindrop optical photon test via DD4hep.

Uses standard G4OpticalPhysics (no eic-opticks GPU plugins).
Optical photons are tracked on CPU by Geant4 and collected as
Geant4Tracker::Hit via the sensitive detector.

Compare hit counts with test_raindrop_dd4hep.py (GPU) to validate.

Prerequisites:
  - Spack environment activated (ROOT, DD4hep on PYTHONPATH/LD_LIBRARY_PATH)
  - DD4hepINSTALL set (for elements.xml lookup)
  - libRaindropGeo.so on DD4HEP_LIBRARY_PATH
"""
import os
import sys

import DDG4
from g4units import MeV

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def run():
    kernel = DDG4.Kernel()

    compact_file = os.path.join(_SCRIPT_DIR, "geometry", "raindrop_dd4hep.xml")
    if not os.path.exists(compact_file):
        print(f"ERROR: Compact file not found: {compact_file}")
        sys.exit(1)

    if "DD4hepINSTALL" not in os.environ:
        print("ERROR: DD4hepINSTALL not set. Activate the spack environment first.")
        sys.exit(1)

    print(f"Loading geometry: {compact_file}")
    kernel.loadGeometry(str("file:" + compact_file))

    geant4 = DDG4.Geant4(kernel)
    geant4.printDetectors()
    geant4.setupUI(typ='tcsh', vis=False, ui=False)

    # Same particle gun as the GPU test: e- at 10 MeV inside water box
    geant4.setupGun(
        "Gun",
        particle='e-',
        energy=10 * MeV,
        position=(0, 0, 0),
        direction=(1.0, 0.0, 0.0),
        multiplicity=1,
    )

    # Register sensitive detector with Geant4OpticalTrackerAction:
    # - Accepts only optical photons (ParticleSelectFilter)
    # - Records one hit per photon and kills it (stop-and-kill on detection)
    seq, act = geant4.setupDetector('Raindrop', 'Geant4OpticalTrackerAction')
    filt = DDG4.Filter(kernel, 'ParticleSelectFilter/OpticalPhotonSelector')
    filt.particle = 'opticalphoton'
    seq.adopt(filt)

    # Physics: QGSP_BERT + standard Geant4 optical physics (CPU tracking)
    geant4.setupPhysics('QGSP_BERT')
    ph = DDG4.PhysicsList(kernel, 'Geant4PhysicsList/OpticalPhys')
    ph.addPhysicsConstructor(str('G4OpticalPhysics'))
    kernel.physicsList().adopt(ph)

    # Save hits to ROOT file
    output_file = "/tmp/raindrop_hits_cpu.root"
    geant4.setupROOTOutput('RootOutput', output_file)

    # Run 2 events (same as GPU test)
    kernel.NumEvents = 2
    kernel.configure()
    kernel.initialize()
    kernel.run()
    kernel.terminate()

    # Verify hits
    verify_hits(output_file)


def verify_hits(root_file):
    """Read back hits, count unique optical photon tracks (boundary crossings)."""
    import ROOT
    f = ROOT.TFile.Open(root_file)
    if not f or f.IsZombie():
        print(f"ERROR: Cannot open {root_file}")
        return

    tree = f.Get("EVENT")
    if not tree:
        print("ERROR: No EVENT tree in ROOT file")
        f.Close()
        return

    print(f"\n{'='*60}")
    print(f"  CPU hit verification from {root_file}")
    print(f"{'='*60}")
    print(f"  Events in file: {tree.GetEntries()}")

    for evt_idx in range(tree.GetEntries()):
        tree.GetEntry(evt_idx)
        hits = tree.RaindropHits
        nhits = hits.size()

        # Count unique photon tracks (each track = one photon entering the volume)
        track_ids = set()
        # Keep only the first hit per track (boundary crossing)
        first_hits = {}
        for i in range(nhits):
            h = hits[i]
            tid = h.truth.trackID
            if tid not in first_hits:
                first_hits[tid] = h
            track_ids.add(tid)

        n_unique = len(track_ids)
        print(f"\n  Event {evt_idx}:")
        print(f"    Total G4Steps in sensitive volume: {nhits}")
        print(f"    Unique optical photon tracks:      {n_unique}")
        print(f"    Avg steps per photon:              {nhits/n_unique:.1f}")
        print(f"    First 3 photon hits (boundary crossing):")
        for i, (tid, h) in enumerate(list(first_hits.items())[:3]):
            pos = h.position
            mom = h.momentum
            print(f"      [trackID={tid}] pos=({pos.x():.1f}, {pos.y():.1f}, {pos.z():.1f}) "
                  f"mom=({mom.x():.3f}, {mom.y():.3f}, {mom.z():.3f}) "
                  f"time={h.truth.time:.2f}ns")

    f.Close()
    print(f"\n  CPU test complete")
    print(f"{'='*60}\n")


if __name__ == "__main__":
    run()
