#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

#include "FTFP_BERT.hh"
#include "G4ClassificationOfNewTrack.hh"
#include "G4Electron.hh"
#include "G4Gamma.hh"
#include "G4GeometryManager.hh"
#include "G4LossTableManager.hh"
#include "G4OpticalParameters.hh"
#include "G4OpticalPhysics.hh"
#include "G4ParticleTable.hh"
#include "G4Positron.hh"
#include "G4ProcessManager.hh"
#include "G4QuasiCerenkov.hh"
#include "G4QuasiOpticalPhoton.hh"
#include "G4QuasiScintillation.hh"
#include "G4RunManager.hh"
#include "G4RunManagerFactory.hh"
#include "G4VModularPhysicsList.hh"
#include "G4VPhysicsConstructor.hh"
#include "G4VUserActionInitialization.hh"

#include "G4UIExecutive.hh"
#include "G4UImanager.hh"
#include "G4VisExecutive.hh"

#include "sysrap/OPTICKS_LOG.hh"

#include "GPURaytraceSubEvt.h"

// QuasiOpticalPhysics — same companion physics constructor as in
// examples/GPURaytraceQuasi: replaces G4Cerenkov / G4Scintillation with their
// G4 11.4 Quasi variants. Required because G4OpticalPhysics::ConstructProcess
// in v11.4.1 hardcodes the legacy classes and ignores the offload flags.
class QuasiOpticalPhysics : public G4VPhysicsConstructor
{
  public:
    QuasiOpticalPhysics(const G4String &name = "QuasiOptical") :
        G4VPhysicsConstructor(name)
    {
    }
    ~QuasiOpticalPhysics() override = default;

    void ConstructParticle() override
    {
    }

    void ConstructProcess() override
    {
        auto *params = G4OpticalParameters::Instance();

        G4QuasiCerenkov *theCerenkov = nullptr;
        if (params->GetCerenkovOffloadPhotons())
        {
            theCerenkov = new G4QuasiCerenkov();
            theCerenkov->SetOffloadPhotons(true);
            theCerenkov->SetStackPhotons(params->GetCerenkovStackPhotons());
        }

        G4QuasiScintillation *theScint = nullptr;
        if (params->GetScintOffloadPhotons())
        {
            theScint = new G4QuasiScintillation();
            theScint->SetOffloadPhotons(true);
            theScint->SetStackPhotons(params->GetScintStackPhotons());
            G4EmSaturation *emSat = G4LossTableManager::Instance()->EmSaturation();
            theScint->AddSaturation(emSat);
        }

        auto *iter = GetParticleIterator();
        iter->reset();
        while ((*iter)())
        {
            auto *particle = iter->value();
            if (particle->IsShortLived())
                continue;
            auto *pManager = particle->GetProcessManager();
            if (!pManager)
                continue;

            if (theCerenkov && theCerenkov->IsApplicable(*particle))
            {
                pManager->AddDiscreteProcess(theCerenkov);
            }
            if (theScint && theScint->IsApplicable(*particle))
            {
                pManager->AddProcess(theScint);
                pManager->SetProcessOrderingToLast(theScint, idxAtRest);
                pManager->SetProcessOrderingToLast(theScint, idxPostStep);
            }
        }
    }
};

using namespace std;

// ActionInitialization — sub-event aware action installer.
//
// Two routing strategies, selectable at the binary level via --subevt-route:
//
//   photon  (default): G4QuasiOpticalPhoton metadata tokens (one per parent
//           Cerenkov / Scintillation step, requires stackPhotons=true) are
//           routed onto a sub-event stack. Each sub-event is then literally a
//           batch of GPU work — N photon-burst tokens. Workers pull batches.
//           This is the natural framing for the new G4 11.4 Quasi classes:
//           the QuasiOpticalPhoton track carries G4QuasiOpticalData /
//           G4CerenkovQuasiTrackInfo / G4ScintillationQuasiTrackInfo as
//           auxiliary track information; G4 does not propagate the token
//           through the geometry, it's a non-physical metadata carrier.
//
//   cascade: classic RE03-style routing of e-/e+/gamma to a sub-event stack.
//           Fragments the EM cascade across workers. Optical photons remain
//           captured at the parent step on the master via the existing
//           SteppingAction intercept.
//
// In both routes the master thread generates the primary; sub-event workers
// must not have a primary generator or G4UserRunAction
// (G4WorkerSubEvtRunManager throws RunSE0221 on those).
struct ActionInitialization : public G4VUserActionInitialization
{
  public:
    enum Route
    {
        ROUTE_PHOTON,
        ROUTE_CASCADE,
    };

  private:
    G4App *fG4App;
    int fSubEventSize;
    Route fRoute;

  public:
    ActionInitialization(G4App *app, int subevt_size, Route route) :
        G4VUserActionInitialization(),
        fG4App(app),
        fSubEventSize(subevt_size),
        fRoute(route)
    {
    }

    void BuildForMaster() const override
    {
        auto *rm = G4RunManager::GetRunManager();
        bool is_subevt_master = (rm->GetRunManagerType() == G4RunManager::subEventMasterRM);

        if (is_subevt_master)
        {
            rm->RegisterSubEventType(0, fSubEventSize);

            if (fRoute == ROUTE_PHOTON)
            {
                // Route the Quasi-optical-photon metadata tokens to sub-events.
                // Each token represents a burst of N photons (carried via
                // G4VAuxiliaryTrackInformation) and is non-physical — G4 does
                // not propagate it; the worker SteppingAction kills it after
                // (optionally) consuming the metadata. Requires stackPhotons=true
                // upstream so that the tokens are pushed at all.
                //
                // Note: in this route the EM cascade carriers (e-/e+/gamma) are
                // NOT routed, so they stay on the master's stack and the master
                // tracks them. Master therefore needs the SteppingAction to
                // capture Cerenkov / Scintillation gensteps at the parent step.
                rm->SetDefaultClassification(G4QuasiOpticalPhoton::Definition(), fSubEvent_0);
            }
            else // ROUTE_CASCADE
            {
                // RE03-style routing: fragment the EM cascade across workers.
                rm->SetDefaultClassification(G4Electron::Definition(), fSubEvent_0);
                rm->SetDefaultClassification(G4Positron::Definition(), fSubEvent_0);
                rm->SetDefaultClassification(G4Gamma::Definition(), fSubEvent_0);
            }

            // In sub-event mode the master owns the primary generator.
            SetUserAction(fG4App->prim_gen_);

            // In photon-route the master also tracks the EM cascade itself
            // (only G4QuasiOpticalPhoton tokens are routed to sub-events; the
            // e-/e+/gamma carriers stay on the master). Master therefore needs
            // the stepping/tracking/event actions: stepping captures the
            // QuasiCerenkov / QuasiScintillation parent steps and pushes the
            // QuasiOpticalPhoton token onto the stack via the natural G4 path,
            // then sub-event routing dispatches the tokens to workers where
            // genstep extraction happens (CAPTURE_TOKEN mode).
            //
            // Harmless to install in cascade-route too — master ends up idle
            // once the primary classification dispatches it to a worker.
            if (fRoute == ROUTE_PHOTON)
            {
                SetUserAction(fG4App->event_act_);
                SetUserAction(fG4App->tracking_);
                SetUserAction(fG4App->stepping_);
            }
        }

        // RunAction lives on the master in every MT mode so that EndOfRunAction
        // (which triggers the GPU launch) runs exactly once.
        SetUserAction(fG4App->run_act_);
    }

    void Build() const override
    {
        auto *rm = G4RunManager::GetRunManager();
        bool is_subevt_worker = (rm->GetRunManagerType() == G4RunManager::subEventWorkerRM);

        // Sub-event workers must not have a primary generator (master owns it)
        // and must not have a G4UserRunAction (G4WorkerSubEvtRunManager throws
        // RunSE0221 if one is installed on a worker).
        if (!is_subevt_worker)
        {
            SetUserAction(fG4App->prim_gen_);
            SetUserAction(fG4App->run_act_);
        }

        SetUserAction(fG4App->event_act_);
        SetUserAction(fG4App->tracking_);
        SetUserAction(fG4App->stepping_);
    }
};

static void usage(const char *prog)
{
    std::cerr << "Usage: " << prog
              << " [options]\n"
                 "  -g, --gdml PATH        GDML file (default: geom.gdml)\n"
                 "  -m, --macro PATH       Geant4 macro (default: run.mac)\n"
                 "  -s, --seed N           random seed (default: time())\n"
                 "      --mode MODE        run mode: serial | event-mt | subevt (default: subevt)\n"
                 "      --subevt-route R   subevt routing: photon | cascade (default: photon)\n"
                 "                           photon  : G4QuasiOpticalPhoton metadata tokens\n"
                 "                                     (requires stackPhotons=true)\n"
                 "                           cascade : e-/e+/gamma EM cascade carriers\n"
                 "      --subevt-size N    sub-event stack capacity in tracks (default: 100)\n"
                 "  -t, --threads N        number of worker threads (default: 4, ignored in serial)\n"
                 "  -i, --interactive      open interactive viewer\n"
                 "  -h, --help             show this message\n";
}

int main(int argc, char **argv)
{
    OPTICKS_LOG(argc, argv);

    string gdml_file = "geom.gdml";
    string macro_name = "run.mac";
    long seed = static_cast<long>(std::time(nullptr));
    bool interactive = false;
    string mode = "subevt";
    string route = "photon";
    int subevt_size = 100;
    int nthreads = 4;

    for (int i = 1; i < argc; i++)
    {
        string a = argv[i];
        auto next = [&](const char *flag) -> const char * {
            if (i + 1 >= argc)
            {
                std::cerr << flag << " requires an argument\n";
                std::exit(EXIT_FAILURE);
            }
            return argv[++i];
        };

        if (a == "-g" || a == "--gdml")
            gdml_file = next("--gdml");
        else if (a == "-m" || a == "--macro")
            macro_name = next("--macro");
        else if (a == "-s" || a == "--seed")
            seed = std::atol(next("--seed"));
        else if (a == "--mode")
            mode = next("--mode");
        else if (a == "--subevt-route")
            route = next("--subevt-route");
        else if (a == "--subevt-size")
            subevt_size = std::atoi(next("--subevt-size"));
        else if (a == "-t" || a == "--threads")
            nthreads = std::atoi(next("--threads"));
        else if (a == "-i" || a == "--interactive")
            interactive = true;
        else if (a == "-h" || a == "--help")
        {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }
        else
        {
            std::cerr << "unknown option: " << a << "\n";
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    G4RunManagerType rm_type = G4RunManagerType::SubEvt;
    if (mode == "serial")
        rm_type = G4RunManagerType::Serial;
    else if (mode == "event-mt")
        rm_type = G4RunManagerType::MT;
    else if (mode == "subevt")
        rm_type = G4RunManagerType::SubEvt;
    else
    {
        std::cerr << "unknown --mode value: " << mode << " (expected serial | event-mt | subevt)\n";
        return EXIT_FAILURE;
    }

    ActionInitialization::Route route_kind = ActionInitialization::ROUTE_PHOTON;
    if (route == "photon")
        route_kind = ActionInitialization::ROUTE_PHOTON;
    else if (route == "cascade")
        route_kind = ActionInitialization::ROUTE_CASCADE;
    else
    {
        std::cerr << "unknown --subevt-route value: " << route << " (expected photon | cascade)\n";
        return EXIT_FAILURE;
    }

    CLHEP::HepRandom::setTheSeed(seed);
    G4cout << "Random seed set to: " << seed << G4endl;
    G4cout << "Run mode: " << mode << " (subevt-route=" << route << ", subevt-size=" << subevt_size << ")" << G4endl;

    // stackPhotons semantics on the Quasi classes (NOT the legacy G4Cerenkov):
    //   stackPhotons=false → G4QuasiCerenkov::PostStepDoIt short-circuits before
    //                        the offload branch; no QuasiOpticalPhoton secondary
    //                        is created. Genstep capture must happen at the
    //                        parent step via the SteppingAction intercept.
    //   stackPhotons=true  → exactly one G4QuasiOpticalPhoton metadata token is
    //                        pushed per parent step, carrying G4QuasiOpticalData
    //                        via G4CerenkovQuasiTrackInfo / G4ScintillationQuasiTrackInfo.
    //                        This is what photon-route sub-events need to route.
    //                        It does NOT cause G4 to track real G4OpticalPhotons.
    bool stack_photons = (route_kind == ActionInitialization::ROUTE_PHOTON);

    auto *optParams = G4OpticalParameters::Instance();
    optParams->SetProcessActivation("Cerenkov", false);
    optParams->SetProcessActivation("Scintillation", false);
    optParams->SetCerenkovOffloadPhotons(true);
    optParams->SetCerenkovStackPhotons(stack_photons);
    optParams->SetScintOffloadPhotons(true);
    optParams->SetScintStackPhotons(stack_photons);

    G4VModularPhysicsList *physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);
    physics->RegisterPhysics(new QuasiOpticalPhysics);

    auto *run_mgr = G4RunManagerFactory::CreateRunManager(rm_type);

    // Set thread count in C++ (not via /run/numberOfThreads in the macro):
    // sub-event mode dispatches macro commands to workers, and workers reject
    // /run/numberOfThreads with "command is issued to local thread." See RE03
    // for the same pattern.
    if (rm_type == G4RunManagerType::MT || rm_type == G4RunManagerType::SubEvt)
    {
        run_mgr->SetNumberOfThreads(nthreads);
    }

    // Sub-event parallel mode requires parallel-geometry optimisation off (per RE03).
    if (run_mgr->GetRunManagerType() == G4RunManager::subEventMasterRM)
    {
        G4GeometryManager::GetInstance()->RequestParallelOptimisation(false);
    }

    run_mgr->SetUserInitialization(physics);

    SteppingAction::CaptureMode capture_mode = (route_kind == ActionInitialization::ROUTE_PHOTON)
                                                   ? SteppingAction::CAPTURE_TOKEN
                                                   : SteppingAction::CAPTURE_PARENT;

    G4App *g4app = new G4App(gdml_file, capture_mode);

    ActionInitialization *actionInit = new ActionInitialization(g4app, subevt_size, route_kind);
    run_mgr->SetUserInitialization(actionInit);
    run_mgr->SetUserInitialization(g4app->det_cons_);

    G4UIExecutive *uix = nullptr;
    G4VisManager *vis = nullptr;

    if (interactive)
    {
        uix = new G4UIExecutive(argc, argv);
        vis = new G4VisExecutive;
        vis->Initialize();
    }

    G4UImanager *ui = G4UImanager::GetUIpointer();
    ui->ApplyCommand("/control/execute " + macro_name);

    if (interactive)
    {
        uix->SessionStart();
    }

    delete uix;

    return EXIT_SUCCESS;
}
