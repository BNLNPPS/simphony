#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

#include "FTFP_BERT.hh"
#include "G4OpticalPhysics.hh"
#include "G4VModularPhysicsList.hh"

#include "G4UImanager.hh"

#include "eic-opticks/sysrap/OPTICKS_LOG.hh"

#include "GPUMCTruth.h"

#include "G4RunManager.hh"
#include "G4RunManagerFactory.hh"
#include "G4VUserActionInitialization.hh"

using namespace std;

struct ActionInitialization : public G4VUserActionInitialization
{
    G4App *fG4App;
    ActionInitialization(G4App *app) : G4VUserActionInitialization(), fG4App(app) {}

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

int main(int argc, char **argv)
{
    OPTICKS_LOG(argc, argv);

    const char *gdml_env  = getenv("EIC_GDML");
    const char *macro_env = getenv("EIC_MACRO");
    const char *seed_env  = getenv("EIC_SEED");

    if (!gdml_env || !macro_env)
    {
        cerr << "Usage: EIC_GDML=<path> EIC_MACRO=<path> [EIC_SEED=<int>] "
                "[OPTICKS_MC_TRUTH=1] GPUMCTruth"
             << endl;
        return EXIT_FAILURE;
    }

    string gdml_file(gdml_env);
    string macro_name(macro_env);
    long seed = seed_env ? strtol(seed_env, nullptr, 10) : static_cast<long>(time(nullptr));
    CLHEP::HepRandom::setTheSeed(seed);
    G4cout << "Random seed set to: " << seed << G4endl;

    G4VModularPhysicsList *physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);

    auto *run_mgr = G4RunManagerFactory::CreateRunManager();
    run_mgr->SetUserInitialization(physics);

    G4App *g4app = new G4App(gdml_file);

    ActionInitialization *actionInit = new ActionInitialization(g4app);
    run_mgr->SetUserInitialization(actionInit);
    run_mgr->SetUserInitialization(g4app->det_cons_);

    G4UImanager *ui = G4UImanager::GetUIpointer();
    ui->ApplyCommand("/control/execute " + macro_name);

    return EXIT_SUCCESS;
}
