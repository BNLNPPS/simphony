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

#include "GPUPhotonFileSource.h"
#include "config.h"

using namespace std;

int main(int argc, char **argv)
{
    OPTICKS_LOG(argc, argv);

    argparse::ArgumentParser program("GPUPhotonFileSource", "0.0.0");

    string gdml_file, config_name, macro_name, photon_file;
    bool interactive;

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

    program.add_argument("-p", "--photons")
        .help("path to input photon text file (one photon per line: pos_x pos_y pos_z time mom_x mom_y mom_z pol_x "
              "pol_y pol_z wavelength)")
        .required()
        .nargs(1)
        .store_into(photon_file);

    program.add_argument("-m", "--macro")
        .help("path to G4 macro")
        .default_value(string("run.mac"))
        .nargs(1)
        .store_into(macro_name);

    program.add_argument("-i", "--interactive")
        .help("whether to open an interactive window with a viewer")
        .flag()
        .store_into(interactive);

    program.add_argument("-s", "--seed").help("fixed random seed (default: time-based)").scan<'i', long>();

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const exception &err)
    {
        cerr << err.what() << endl;
        cerr << program;
        exit(EXIT_FAILURE);
    }

    long seed;
    if (program.is_used("--seed"))
    {
        seed = program.get<long>("--seed");
    }
    else
    {
        seed = static_cast<long>(time(nullptr));
    }
    CLHEP::HepRandom::setTheSeed(seed);
    G4cout << "Random seed set to: " << seed << G4endl;

    gphox::Config{config_name};

    // Configure Geant4
    // The physics list must be instantiated before other user actions
    G4VModularPhysicsList *physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);

    G4RunManager run_mgr;
    run_mgr.SetUserInitialization(physics);

    G4App *g4app = new G4App(photon_file, gdml_file);
    run_mgr.SetUserInitialization(g4app->det_cons_);
    run_mgr.SetUserAction(g4app->prim_gen_);
    run_mgr.SetUserAction(g4app->run_act_);
    run_mgr.SetUserAction(g4app->event_act_);
    run_mgr.Initialize();

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
