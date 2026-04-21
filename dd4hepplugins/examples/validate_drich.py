#!/usr/bin/env python3
"""Run GPU and CPU with simplified dRICH geometry (no non-optical PDU components)."""
import math, os, sys, numpy as np

_SCRIPT_DIR = "/tmp/simphony/dd4hepplugins/examples"
_GEOM_DIR = os.path.join(_SCRIPT_DIR, "geometry")
sys.path.insert(0, _SCRIPT_DIR)

NUM_EVENTS = int(os.environ.get("NUM_EVENTS", "3"))
MULTIPLICITY = int(os.environ.get("MULTIPLICITY", "20"))


def setup_kernel(mode):
    import cppyy, DDG4
    from g4units import GeV

    cppyy.include("G4OpticalParameters.hh")
    from cppyy.gbl import G4OpticalParameters

    compact = os.path.join(_GEOM_DIR, "epic_drich_simple.xml")
    if "DRICH_SIMPLE_XML" not in os.environ:
        os.environ["DRICH_SIMPLE_XML"] = os.path.join(_GEOM_DIR, "drich_1sector_simple.xml")

    if mode == "gpu":
        os.environ["OPTICKS_EVENT_MODE"] = "Minimal"
        os.environ["OPTICKS_INTEGRATION_MODE"] = "1"
        # Ensure DDG4 can find the plugin
        import ctypes
        ctypes.CDLL("libddeicopticks.so", ctypes.RTLD_GLOBAL)
    else:
        os.environ.pop("OPTICKS_EVENT_MODE", None)
        os.environ.pop("OPTICKS_INTEGRATION_MODE", None)

    kernel = DDG4.Kernel()
    kernel.loadGeometry(str("file:" + compact))
    geant4 = DDG4.Geant4(kernel)
    geant4.setupUI(typ="tcsh", vis=False, ui=False)

    eta = 2.0
    theta = 2.0 * math.atan(math.exp(-eta))
    phi = math.radians(-167.0)
    geant4.setupGun("Gun", particle="pi+", energy=10*GeV,
                    position=(0, 0, 0), isotrop=False,
                    direction=(math.sin(theta)*math.cos(phi),
                               math.sin(theta)*math.sin(phi),
                               math.cos(theta)),
                    multiplicity=MULTIPLICITY)

    geant4.setupPhysics("QGSP_BERT")
    ph = DDG4.PhysicsList(kernel, "Geant4PhysicsList/OpticalPhys")
    ph.addPhysicsConstructor(str("G4OpticalPhysics"))
    kernel.physicsList().adopt(ph)

    # Detector setup needed for both modes (GPU injects hits into DD4hep)
    seq, act = geant4.setupDetector("DRICH", "Geant4OpticalTrackerAction")
    filt = DDG4.Filter(kernel, "ParticleSelectFilter/OpticalPhotonSelector")
    filt.particle = "opticalphoton"
    seq.adopt(filt)

    if mode == "gpu":
        step_action = DDG4.SteppingAction(kernel, "OpticsSteppingAction/OpticsStep1")
        kernel.steppingAction().adopt(step_action)
        run_action = DDG4.RunAction(kernel, "OpticsRun/OpticsRun1")
        kernel.runAction().adopt(run_action)
        # OpticsEvent must be registered BEFORE ROOT output so hits are
        # injected into the collection before serialization.
        evt_action = DDG4.EventAction(kernel, "OpticsEvent/OpticsEvt1")
        kernel.eventAction().adopt(evt_action)

    if mode == "gpu":
        geant4.setupROOTOutput("RootOutput", "/tmp/drich_simple_gpu")
    else:
        geant4.setupROOTOutput("RootOutput", "/tmp/drich_simple_cpu")

    kernel.NumEvents = NUM_EVENTS
    kernel.configure()

    # In GPU mode, tell G4Cerenkov/G4Scintillation to NOT push optical
    # photon secondaries onto the Geant4 stack.  The genstep is still
    # computed (numPhotons, BetaInverse, etc.) and collected by
    # OpticsSteppingAction, but no CPU photon tracks are created.
    if mode == "gpu":
        G4OpticalParameters.Instance().SetCerenkovStackPhotons(False)
        G4OpticalParameters.Instance().SetScintStackPhotons(False)

    kernel.initialize()
    G4OpticalParameters.Instance().SetBoundaryInvokeSD(True)
    return kernel


def run_gpu():
    kernel = setup_kernel("gpu")
    kernel.run()
    kernel.terminate()

    # Read hits from npy files
    base = os.path.join(os.environ.get("TMP", "/tmp/opticks_out"),
                        "GEOM", os.environ.get("GEOM", "simple"),
                        "python3.13", "ALL0_no_opticks_event_name")
    total = 0
    if os.path.isdir(base):
        for d in sorted(os.listdir(base)):
            hp = os.path.join(base, d, "hit.npy")
            if os.path.exists(hp):
                n = len(np.load(hp))
                total += n
                print(f"  {d}: {n} hits")
    print(f"GPU total: {total} hits")
    return total


def run_cpu():
    kernel = setup_kernel("cpu")
    kernel.run()
    kernel.terminate()

    # Extract hits from ROOT
    import ROOT
    ROOT.gSystem.Load("libDDG4Plugins")
    ROOT.gSystem.Load("libDDG4")
    f = ROOT.TFile.Open("/tmp/drich_simple_cpu.root")
    tree = f.Get("EVENT")
    total = 0
    for i in range(tree.GetEntries()):
        tree.GetEntry(i)
        nhits = tree.DRICHHits.size()
        total += nhits
        print(f"  event {i}: {nhits} hits")
    f.Close()
    print(f"CPU total: {total} hits")
    return total


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "gpu"
    if mode == "gpu":
        run_gpu()
    elif mode == "cpu":
        run_cpu()
    else:
        print(f"Usage: {sys.argv[0]} [gpu|cpu]")
