// async_gpu_std.cpp — async CPU+GPU optical photon simulation, std-only.
//
// Same architecture as examples/async_gpu_launch but the GPU worker is a
// plain std::thread driven by std::mutex + std::condition_variable +
// std::queue, with no G4TaskGroup or G4Mutex.
//
// Usage:
//   async_gpu_std -g apex.gdml -m run.mac [--async] [--sync] [-s SEED]

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

#include "FTFP_BERT.hh"
#include "G4OpticalPhysics.hh"
#include "G4VModularPhysicsList.hh"

#include "G4UIExecutive.hh"
#include "G4UImanager.hh"
#include "G4VisExecutive.hh"

#include "G4RunManager.hh"
#include "G4RunManagerFactory.hh"
#include "G4VUserActionInitialization.hh"

#include "async_gpu_std.h"
#include "sysrap/OPTICKS_LOG.hh"

struct ActionInitialization : public G4VUserActionInitialization
{
    G4App *fG4App;
    ActionInitialization(G4App *app) : fG4App(app)
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

static void usage(const char *prog)
{
    std::cerr << "Usage: " << prog
              << " [options]\n"
                 "  -g, --gdml PATH      GDML file (default: apex.gdml)\n"
                 "  -m, --macro PATH     Geant4 macro (default: run.mac)\n"
                 "  -s, --seed N         random seed (default: time())\n"
                 "  -i, --interactive    open interactive viewer\n"
                 "      --async          double-buffered async GPU (default)\n"
                 "      --sync           end-of-run GPU simulation\n"
                 "  -h, --help           show this message\n";
}

int main(int argc, char **argv)
{
    OPTICKS_LOG(argc, argv);

    std::string gdml_file = "apex.gdml";
    std::string macro_name = "run.mac";
    long seed = static_cast<long>(std::time(nullptr));
    bool seed_set = false;
    bool interactive = false;
    bool sync_mode = false;

    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
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
        {
            seed = std::atol(next("--seed"));
            seed_set = true;
        }
        else if (a == "-i" || a == "--interactive")
            interactive = true;
        else if (a == "--sync")
            sync_mode = true;
        else if (a == "--async")
            sync_mode = false;
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
    G4cout << "Random seed: " << seed << (seed_set ? " (user)" : " (time)") << G4endl;

    bool enable_async = !sync_mode;
    G4cout << "Mode: " << (enable_async ? "ASYNC (std)" : "SYNC") << G4endl;

    G4VModularPhysicsList *physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);

    auto *run_mgr = G4RunManagerFactory::CreateRunManager();
    run_mgr->SetUserInitialization(physics);

    G4App *g4app = new G4App(gdml_file, enable_async);

    auto *actionInit = new ActionInitialization(g4app);
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
        uix->SessionStart();

    delete uix;
    return EXIT_SUCCESS;
}
