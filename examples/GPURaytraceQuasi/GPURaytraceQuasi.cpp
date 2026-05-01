#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

#include "FTFP_BERT.hh"
#include "G4LossTableManager.hh"
#include "G4OpticalParameters.hh"
#include "G4OpticalPhysics.hh"
#include "G4ParticleTable.hh"
#include "G4ProcessManager.hh"
#include "G4QuasiCerenkov.hh"
#include "G4QuasiScintillation.hh"
#include "G4VModularPhysicsList.hh"
#include "G4VPhysicsConstructor.hh"

#include "G4UIExecutive.hh"
#include "G4UImanager.hh"
#include "G4VisExecutive.hh"

#include "sysrap/OPTICKS_LOG.hh"

#include "GPURaytraceQuasi.h"

// Companion physics constructor: replaces G4Cerenkov / G4Scintillation with their
// G4 11.4 Quasi variants. Registered AFTER G4OpticalPhysics; G4OpticalPhysics is
// configured to skip legacy Cerenkov / Scintillation registration via
// SetProcessActivation("Cerenkov", false) / SetProcessActivation("Scintillation", false).
//
// Required because G4OpticalPhysics::ConstructProcess in v11.4.1 hardcodes legacy
// G4Cerenkov / G4Scintillation and does not honour the SetCerenkovOffloadPhotons /
// SetScintOffloadPhotons flags. Verified against G4 v11.4.1 release source at
// source/physics_lists/constructors/electromagnetic/src/G4OpticalPhysics.cc — no
// branch on the offload parameter exists.
class QuasiOpticalPhysics : public G4VPhysicsConstructor
{
  public:
    QuasiOpticalPhysics(const G4String &name = "QuasiOptical") : G4VPhysicsConstructor(name) {}
    ~QuasiOpticalPhysics() override = default;

    void ConstructParticle() override
    {
        // G4OpticalPhysics already calls G4QuasiOpticalPhoton::QuasiOpticalPhotonDefinition()
        // in its ConstructParticle(). Nothing to do.
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

#include "G4RunManager.hh"
#include "G4RunManagerFactory.hh"
#include "G4VUserActionInitialization.hh"

using namespace std;

struct ActionInitialization : public G4VUserActionInitialization
{
  private:
    G4App *fG4App; // Store the pointer to G4App

  public:
    // Note the signature: now we take a pointer to the G4App itself
    ActionInitialization(G4App *app) : G4VUserActionInitialization(), fG4App(app)
    {
    }

    virtual void BuildForMaster() const override
    {
        SetUserAction(fG4App->run_act_);
    }

    virtual void Build() const override
    {
        SetUserAction(fG4App->prim_gen_);
        SetUserAction(fG4App->run_act_);
        SetUserAction(fG4App->event_act_);
        SetUserAction(fG4App->tracking_);
        SetUserAction(fG4App->stepping_);
    }
};

static void usage(const char *prog)
{
    std::cerr << "Usage: " << prog
              << " [options]\n"
                 "  -g, --gdml PATH      GDML file (default: geom.gdml)\n"
                 "  -m, --macro PATH     Geant4 macro (default: run.mac)\n"
                 "  -s, --seed N         random seed (default: time())\n"
                 "  -i, --interactive    open interactive viewer\n"
                 "  -h, --help           show this message\n";
}

int main(int argc, char **argv)
{
    OPTICKS_LOG(argc, argv);

    string gdml_file = "geom.gdml";
    string macro_name = "run.mac";
    long seed = static_cast<long>(std::time(nullptr));
    bool interactive = false;

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

    CLHEP::HepRandom::setTheSeed(seed);
    G4cout << "Random seed set to: " << seed << G4endl;

    // Configure Geant4 optical photon offload (G4 11.4 G4QuasiCerenkov / G4QuasiScintillation).
    //
    // G4OpticalPhysics::ConstructProcess in v11.4.1 (and current master) hardcodes legacy
    // G4Cerenkov / G4Scintillation and does not branch on the offload flags. So:
    //   1) Tell G4OpticalPhysics to skip Cerenkov / Scintillation entirely.
    //   2) Register a small companion physics constructor that installs the Quasi variants.
    //
    // StackPhotons=false: no real G4OpticalPhoton secondaries; Opticks does the propagation.
    // OffloadPhotons=true: read by our QuasiOpticalPhysics constructor to gate registration.
    auto *optParams = G4OpticalParameters::Instance();
    optParams->SetProcessActivation("Cerenkov", false);
    optParams->SetProcessActivation("Scintillation", false);
    optParams->SetCerenkovOffloadPhotons(true);
    optParams->SetCerenkovStackPhotons(false);
    optParams->SetScintOffloadPhotons(true);
    optParams->SetScintStackPhotons(false);

    // The physics list must be instantiated before other user actions
    G4VModularPhysicsList *physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);
    physics->RegisterPhysics(new QuasiOpticalPhysics);

    auto *run_mgr = G4RunManagerFactory::CreateRunManager();
    run_mgr->SetUserInitialization(physics);

    G4App *g4app = new G4App(gdml_file);

    ActionInitialization *actionInit = new ActionInitialization(g4app);
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
