#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

#include <argparse/argparse.hpp>

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
    G4App* fG4App;
    ActionInitialization(G4App* app) :
        G4VUserActionInitialization(),
        fG4App(app)
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

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    argparse::ArgumentParser program("GPUMCTruth", "0.0.0");

    string gdml_file, macro_name;

    program.add_argument("-g", "--gdml").help("path to GDML file").required().nargs(1).store_into(gdml_file);

    program.add_argument("-m", "--macro").help("path to G4 macro").required().nargs(1).store_into(macro_name);

    program.add_argument("-s", "--seed").help("fixed random seed (default: time-based)").scan<'i', long>();

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const exception& err)
    {
        cerr << err.what() << endl;
        cerr << program;
        return EXIT_FAILURE;
    }

    long seed = program.is_used("--seed") ? program.get<long>("--seed") : static_cast<long>(time(nullptr));
    CLHEP::HepRandom::setTheSeed(seed);
    G4cout << "Random seed set to: " << seed << G4endl;

    G4VModularPhysicsList* physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);

    auto* run_mgr = G4RunManagerFactory::CreateRunManager();
    run_mgr->SetUserInitialization(physics);

    G4App* g4app = new G4App(gdml_file);

    ActionInitialization* actionInit = new ActionInitialization(g4app);
    run_mgr->SetUserInitialization(actionInit);
    run_mgr->SetUserInitialization(g4app->det_cons_);

    G4UImanager* ui = G4UImanager::GetUIpointer();
    ui->ApplyCommand("/control/execute " + macro_name);

    return EXIT_SUCCESS;
}
