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
// In sub-event parallel mode the master thread generates the primary and
// fragments the EM cascade (e-, e+, gamma) into G4SubEvent batches that worker
// threads pull and track. Workers therefore need stepping / tracking / event
// actions but NOT the primary generator (the master already generated it).
//
// The branching here mirrors example RE03/RE03ActionInitialization but routes
// the optical-physics step capture (in SteppingAction) onto sub-event workers
// rather than on the single master thread.
struct ActionInitialization : public G4VUserActionInitialization
{
  private:
    G4App *fG4App;
    int fSubEventSize;

  public:
    ActionInitialization(G4App *app, int subevt_size) :
        G4VUserActionInitialization(),
        fG4App(app),
        fSubEventSize(subevt_size)
    {
    }

    void BuildForMaster() const override
    {
        auto *rm = G4RunManager::GetRunManager();

        if (rm->GetRunManagerType() == G4RunManager::subEventMasterRM)
        {
            // Register a single sub-event type with capacity fSubEventSize tracks.
            // The EM cascade carriers (e-/e+/gamma) get routed onto this stack and
            // dispatched to workers when the stack fills up.
            rm->RegisterSubEventType(0, fSubEventSize);
            rm->SetDefaultClassification(G4Electron::Definition(), fSubEvent_0);
            rm->SetDefaultClassification(G4Positron::Definition(), fSubEvent_0);
            rm->SetDefaultClassification(G4Gamma::Definition(), fSubEvent_0);

            // In sub-event mode the master owns the primary generator.
            SetUserAction(fG4App->prim_gen_);
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

    CLHEP::HepRandom::setTheSeed(seed);
    G4cout << "Random seed set to: " << seed << G4endl;
    G4cout << "Run mode: " << mode << " (subevt-size=" << subevt_size << ")" << G4endl;

    auto *optParams = G4OpticalParameters::Instance();
    optParams->SetProcessActivation("Cerenkov", false);
    optParams->SetProcessActivation("Scintillation", false);
    optParams->SetCerenkovOffloadPhotons(true);
    optParams->SetCerenkovStackPhotons(false);
    optParams->SetScintOffloadPhotons(true);
    optParams->SetScintStackPhotons(false);

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

    G4App *g4app = new G4App(gdml_file);

    ActionInitialization *actionInit = new ActionInitialization(g4app, subevt_size);
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
