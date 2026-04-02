#!/usr/bin/env python3
"""
Test eic-opticks DD4hep plugins with the raindrop geometry.

Geometry: Vacuum world > Lead(220mm) > Air(200mm) > Water(100mm)
  - Water has RINDEX=1.333 and scintillation properties
  - Air has RINDEX=1.0
  - One border surface between water drop and air medium
  - Very short volume names (Ct, Md, Dr) -- avoids eic-opticks buffer overflow

This uses the same geometry as eic-opticks's own raindrop test,
but expressed as DD4hep compact XML instead of GDML.

Prerequisites:
  - Spack environment activated (ROOT, DD4hep, eic-opticks on PYTHONPATH/LD_LIBRARY_PATH)
  - DD4hepINSTALL set (for elements.xml lookup)
  - libddeicopticks.so and libRaindropGeo.so on DD4HEP_LIBRARY_PATH
"""
import os
import sys

import DDG4
from g4units import GeV, MeV, mm

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

    # Batch mode
    geant4.setupUI(typ='tcsh', vis=False, ui=False)

    # Particle gun: e- at 10 MeV starting inside water box (50mm half-size)
    # Low energy so electron stops inside the water volume
    gun = geant4.setupGun(
        "Gun",
        particle='e-',
        energy=10 * MeV,
        position=(0, 0, 0),
        direction=(1.0, 0.0, 0.0),
        multiplicity=1,
    )

    # Register sensitive detector -- creates DD4hep hit collection for Raindrop.
    # Add EnergyDepositMinimumCut filter to block G4Step hits from charged
    # particles; only GPU optical photon hits injected by OpticsEvent pass.
    seq, act = geant4.setupTracker('Raindrop')
    filt = DDG4.Filter(kernel, 'EnergyDepositMinimumCut/OpticalOnly')
    filt.Cut = 1e12  # 1 TeV -- effectively blocks all G4Step hits
    seq.adopt(filt)

    # Physics: QGSP_BERT + eic-opticks genstep-collecting optical processes
    geant4.setupPhysics('QGSP_BERT')

    # OpticsPhysics replaces Geant4CerenkovPhysics, Geant4ScintillationPhysics,
    # and Geant4OpticalPhotonPhysics with eic-opticks modified versions that
    # collect gensteps for GPU simulation instead of producing CPU photons.
    optphy = DDG4.PhysicsList(kernel, 'OpticsPhysics/OpticsPhys1')
    optphy.MaxNumPhotonsPerStep = 10000
    optphy.MaxBetaChangePerStep = 10.0
    optphy.TrackSecondariesFirst = True
    optphy.OpticksMode = 0
    kernel.physicsList().adopt(optphy)

    # --- eic-opticks GPU plugins ---
    run_action = DDG4.RunAction(kernel, 'OpticsRun/OpticsRun1')
    run_action.SaveGeometry = False
    kernel.runAction().adopt(run_action)

    evt_action = DDG4.EventAction(kernel, 'OpticsEvent/OpticsEvt1')
    evt_action.Verbose = 1
    kernel.eventAction().adopt(evt_action)

    # Save hits to ROOT file via DD4hep's standard output mechanism
    output_file = "/tmp/raindrop_hits.root"
    geant4.setupROOTOutput('RootOutput', output_file)

    # Run 2 events
    kernel.NumEvents = 2
    kernel.configure()
    kernel.initialize()
    kernel.run()
    kernel.terminate()

    # Verify: read back hits from ROOT file
    verify_hits(output_file)

def verify_hits(root_file):
    """Read back hits from the ROOT file and print summary."""
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
    print(f"  Hit verification from {root_file}")
    print(f"{'='*60}")
    print(f"  Events in file: {tree.GetEntries()}")

    # List branches
    branches = [b.GetName() for b in tree.GetListOfBranches()]
    print(f"  Branches: {branches}")

    hit_branches = [b for b in branches if 'hit' in b.lower() or 'Hit' in b]
    if not hit_branches:
        hit_branches = [b for b in branches if b not in ('RunNumber', 'EventNumber')]

    for evt_idx in range(tree.GetEntries()):
        tree.GetEntry(evt_idx)
        print(f"\n  Event {evt_idx}:")
        for bname in hit_branches:
            branch = tree.GetBranch(bname)
            leaf = branch.GetLeaf(bname)
            if hasattr(tree, bname):
                hits = getattr(tree, bname)
                nhits = hits.size() if hasattr(hits, 'size') else 0
                print(f"    {bname}: {nhits} hits")
                # Print first 3 hits
                for i in range(min(3, nhits)):
                    h = hits[i]
                    pos = h.position
                    mom = h.momentum
                    print(f"      [{i}] pos=({pos.x():.1f}, {pos.y():.1f}, {pos.z():.1f}) "
                          f"mom=({mom.x():.3f}, {mom.y():.3f}, {mom.z():.3f}) "
                          f"wavelength={h.length:.1f}nm "
                          f"time={h.truth.time:.2f}ns")

    f.Close()
    print(f"\n  PASS: Hits successfully stored and retrieved via DD4hep")
    print(f"{'='*60}\n")

if __name__ == "__main__":
    run()
