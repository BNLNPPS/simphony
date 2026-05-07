// async_gpu_launch.cpp — Async CPU+GPU optical photon simulation example
//
// Demonstrates double-buffered async GPU processing where the CPU event
// loop continues while the GPU processes accumulated gensteps in batches.
//
// Usage:
//   async_gpu_launch -g apex.gdml -m run.mac [--async] [--sync]
//
// Default is --async.  Use --sync for the original end-of-run GPU mode.

#include <string>

#include <argparse/argparse.hpp>

#include "FTFP_BERT.hh"
#include "G4OpticalPhysics.hh"
#include "G4VModularPhysicsList.hh"

#include "G4UIExecutive.hh"
#include "G4UImanager.hh"
#include "G4VisExecutive.hh"

#include "async_gpu_launch.h"
#include "config.h"
#include "sysrap/OPTICKS_LOG.hh"

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

    void BuildForMaster() const override
    {
        SetUserAction(fG4App->run_act_);
    }

    void Build() const override
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

    argparse::ArgumentParser program("async_gpu_launch", "0.0.0");

    string gdml_file, macro_name;
    bool   interactive;

    program.add_argument("-g", "--gdml")
        .help("path to GDML file")
        .default_value(string("apex.gdml"))
        .nargs(1)
        .store_into(gdml_file);

    program.add_argument("-m", "--macro")
        .help("path to G4 macro")
        .default_value(string("run.mac"))
        .nargs(1)
        .store_into(macro_name);

    program.add_argument("-c", "--config")
        .help("config file name (without .json extension)")
        .default_value(string(""))
        .nargs(1);

    program.add_argument("-i", "--interactive").help("open interactive viewer").flag().store_into(interactive);

    program.add_argument("-s", "--seed").help("fixed random seed").scan<'i', long>();

    program.add_argument("--async").help("use async double-buffered GPU processing (default)").flag();

    program.add_argument("--sync").help("use synchronous end-of-run GPU processing").flag();

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

    // Seed
    long seed;
    if (program.is_used("--seed"))
        seed = program.get<long>("--seed");
    else
        seed = static_cast<long>(time(nullptr));
    CLHEP::HepRandom::setTheSeed(seed);
    G4cout << "Random seed: " << seed << G4endl;

    // Mode: async by default, sync if --sync is given
    bool enable_async = !program.get<bool>("--sync");
    G4cout << "Mode: " << (enable_async ? "ASYNC" : "SYNC") << G4endl;

    // Physics
    G4VModularPhysicsList* physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);

    auto* run_mgr = G4RunManagerFactory::CreateRunManager();
    run_mgr->SetUserInitialization(physics);

    // Application
    G4App* g4app = new G4App(gdml_file, enable_async);

    ActionInitialization* actionInit = new ActionInitialization(g4app);
    run_mgr->SetUserInitialization(actionInit);
    run_mgr->SetUserInitialization(g4app->det_cons_);

    // UI
    G4UIExecutive* uix = nullptr;
    G4VisManager*  vis = nullptr;

    if (interactive)
    {
        uix = new G4UIExecutive(argc, argv);
        vis = new G4VisExecutive;
        vis->Initialize();
    }

    G4UImanager* ui = G4UImanager::GetUIpointer();
    ui->ApplyCommand("/control/execute " + macro_name);

    if (interactive)
        uix->SessionStart();

    delete uix;
    return EXIT_SUCCESS;
}
