#include <string>
#include <thread>

#include <argparse/argparse.hpp>

#include "FTFP_BERT.hh"
#include "G4OpticalPhysics.hh"
#include "G4MTRunManager.hh"
#include "G4RunManager.hh"
#include "G4VModularPhysicsList.hh"
#include "G4UImanager.hh"

#include "G4OpticalParameters.hh"

#include "StandAloneGeant4Validation.h"
#include "src/config.h"

using namespace std;

int main(int argc, char **argv)
{
    argparse::ArgumentParser program("StandAloneGeant4Validation", "0.0.0");

    string gdml_file, config_name;
    int num_threads = 0;

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

    program.add_argument("-s", "--seed")
        .help("fixed random seed (default: time-based)")
        .scan<'i', long>();

    program.add_argument("-t", "--threads")
        .help("number of threads (0=sequential, default: hardware concurrency)")
        .default_value(-1)
        .scan<'i', int>()
        .store_into(num_threads);

    program.add_argument("--aligned")
        .help("enable photon-by-photon aligned comparison with GPU (forces sequential)")
        .default_value(false)
        .implicit_value(true);

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
        seed = program.get<long>("--seed");
    else
        seed = static_cast<long>(time(nullptr));

    bool aligned = program.get<bool>("--aligned");

    gphox::Config cfg(config_name);
    int total_photons = cfg.torch.numphoton;

    // Aligned mode forces sequential (U4Random is single-threaded)
    if (aligned)
        num_threads = 0;

    // Determine threading mode
    bool use_mt = (num_threads != 0);
    if (num_threads < 0)
        num_threads = std::thread::hardware_concurrency();
    if (num_threads < 1)
        num_threads = 1;

    // In MT mode: split photons across events, one event per thread-batch
    // In sequential mode: one event with all photons (original behavior)
    int num_events, photons_per_event;
    if (use_mt)
    {
        num_events = num_threads * 4;  // 4 events per thread for load balancing
        photons_per_event = (total_photons + num_events - 1) / num_events;
        // Adjust num_events so we don't overshoot
        num_events = (total_photons + photons_per_event - 1) / photons_per_event;
    }
    else
    {
        num_events = 1;
        photons_per_event = total_photons;
    }

    int actual_photons = num_events * photons_per_event;

    G4cout << "Random seed set to: " << seed << G4endl;
    G4cout << "G4: " << total_photons << " photons, "
           << num_events << " events x " << photons_per_event << " photons/event"
           << " (" << actual_photons << " actual)"
           << (use_mt ? ", " + to_string(num_threads) + " threads" : ", sequential")
           << G4endl;

    HitAccumulator accumulator;
    PhotonFateAccumulator fate;
    StepRecordAccumulator *record = nullptr;

    if (aligned)
    {
        fate.Resize(total_photons);
        record = new StepRecordAccumulator(total_photons, 32);
    }

    G4VModularPhysicsList *physics = new FTFP_BERT;
    if (aligned)
        physics->RegisterPhysics(new AlignedOpticalPhysics);
    else
        physics->RegisterPhysics(new G4OpticalPhysics);

    // Use exponential WLS time profile (default is delta = zero delay)
    G4OpticalParameters::Instance()->SetWLSTimeProfile("exponential");

    if (use_mt)
    {
        auto *run_mgr = new G4MTRunManager;
        run_mgr->SetNumberOfThreads(num_threads);
        run_mgr->SetUserInitialization(physics);
        run_mgr->SetUserInitialization(new G4OnlyDetectorConstruction(gdml_file, &accumulator));
        run_mgr->SetUserInitialization(
            new G4OnlyActionInitialization(cfg, &accumulator, &fate, photons_per_event, num_events, aligned, record));
        run_mgr->Initialize();

        CLHEP::HepRandom::setTheSeed(seed);

        G4cout << "G4: Starting MT run with " << num_events << " events..." << G4endl;
        run_mgr->BeamOn(num_events);

        delete run_mgr;
    }
    else
    {
        G4RunManager run_mgr;
        run_mgr.SetUserInitialization(physics);
        run_mgr.SetUserInitialization(new G4OnlyDetectorConstruction(gdml_file, &accumulator));
        run_mgr.SetUserInitialization(
            new G4OnlyActionInitialization(cfg, &accumulator, &fate, photons_per_event, num_events, aligned, record));

        if (aligned)
        {
            G4cout << "G4: Aligned mode — creating U4Random" << G4endl;
            U4Random::Create();
        }

        run_mgr.Initialize();

        CLHEP::HepRandom::setTheSeed(seed);

        G4cout << "G4: Starting sequential run..." << G4endl;
        run_mgr.BeamOn(num_events);
    }

    delete record;
    return EXIT_SUCCESS;
}
