#include <string>

#include <argparse/argparse.hpp>

#include "FTFP_BERT.hh"
#include "G4OpticalPhysics.hh"
#include "G4RunManager.hh"
#include "G4VModularPhysicsList.hh"

#include "G4UIExecutive.hh"
#include "G4UImanager.hh"
#include "G4VisExecutive.hh"

#include "sysrap/OPTICKS_LOG.hh"

#include "config.h"
#include "g4app.h"

using namespace std;

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    argparse::ArgumentParser program("simg4ox", "0.0.0");

    string gdml_file, config_name, macro_name;
    bool   interactive;

    program.add_argument("-g", "--gdml")
        .help("path to GDML file")
        .default_value(string("geom.gdml"))
        .nargs(1)
        .store_into(gdml_file);

    program.add_argument("-c", "--config")
        .help("the name of a config file")
        .default_value(string("dev"))
        .nargs(1)
        .store_into(config_name);

    program.add_argument("-m", "--macro")
        .help("path to G4 macro")
        .default_value(string("run.mac"))
        .nargs(1)
        .store_into(macro_name);

    program.add_argument("-i", "--interactive")
        .help("whether to open an interactive window with a viewer")
        .flag()
        .store_into(interactive);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const exception& err)
    {
        cerr << err.what() << endl;
        cerr << program;
        exit(EXIT_FAILURE);
    }

    simphony::Config cfg(config_name);

    // Configure Geant4
    // The physics list must be instantiated before other user actions
    G4VModularPhysicsList* physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);

    G4RunManager run_mgr;
    run_mgr.SetUserInitialization(physics);

    G4App* g4app = new G4App(cfg, gdml_file);
    run_mgr.SetUserInitialization(g4app->det_cons_);
    run_mgr.SetUserAction(g4app->prim_gen_);
    run_mgr.SetUserAction(g4app->run_act_);
    run_mgr.SetUserAction(g4app->event_act_);
    run_mgr.SetUserAction(g4app->tracking_);
    run_mgr.SetUserAction(g4app->stepping_);
    run_mgr.Initialize();

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
    {
        uix->SessionStart();
    }

    delete uix;

    return EXIT_SUCCESS;
}
