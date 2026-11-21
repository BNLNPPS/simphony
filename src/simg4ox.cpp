#include "nlohmann/json.hpp"
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include <argparse/argparse.hpp>

#include "FTFP_BERT.hh"
#include "G4OpticalParameters.hh"
#include "G4OpticalPhysics.hh"
#include "G4RunManager.hh"
#include "G4RunManagerFactory.hh"
#include "G4VModularPhysicsList.hh"
#include "Randomize.hh"

#include "G4UIExecutive.hh"
#include "G4UImanager.hh"
#include "G4Version.hh"
#include "G4VisExecutive.hh"

#include "sysrap/EventTiming.hh"
#include "sysrap/OPTICKS_LOG.hh"
#include "sysrap/SEventConfig.hh"

#include "GpuDirectory.h"
#include "GpuWorker.h"
#include "GpuWorkerProtocol.h"
#include "config.h"
#include "g4app.h"

using namespace std;

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == simphony::GpuWorkerFlag)
        return simphony::RunGpuWorker(argc, argv);

    OPTICKS_LOG(argc, argv);

    argparse::ArgumentParser program("simg4ox", "0.0.0");

    string gdml_file, config_name, macro_name, particle_name;
    bool   interactive;
    bool   no_cpu_recorder{false};

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

    program.add_argument("-s", "--seed").help("fixed random seed").scan<'i', long>();

    program.add_argument("-t", "--threads")
        .help("number of Geant4 CPU worker threads (1 selects the serial run manager)")
        .default_value(1)
        .scan<'i', int>();

    program.add_argument("--gpu-dispatch")
        .help("GPU dispatch: auto uses a helper per visible GPU for MT jobs; direct uses the first GPU; pool forces helpers")
        .default_value(std::string("auto"));
    program.add_argument("--gpu-timeout")
        .help("positive helper startup/request timeout in seconds")
        .default_value(3600.0)
        .scan<'g', double>();
    program.add_argument("--list-gpus").help("print CUDA-visible devices as JSON and exit").flag();

    program.add_argument("--no-cpu-recorder")
        .help("disable the full CPU SEvt history recorder for a matched serial/MT optical torch comparison")
        .flag()
        .store_into(no_cpu_recorder);

    program.add_argument("--particle")
        .help("Geant4 primary particle name; enables generated optical-photon offload (for example, mu-)")
        .store_into(particle_name);

    program.add_argument("--momentum-gev-c")
        .help("primary-particle momentum in GeV/c")
        .default_value(5.0)
        .scan<'g', double>();

    program.add_argument("--multiplicity")
        .help("number of primary particles per Geant4 event")
        .default_value(1)
        .scan<'i', int>();

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

    const int num_threads = program.get<int>("--threads");
    if (num_threads < 1)
    {
        cerr << "--threads must be a positive integer" << endl;
        return EXIT_FAILURE;
    }

    const auto   gpu_dispatch = program.get<std::string>("--gpu-dispatch");
    const double gpu_timeout = program.get<double>("--gpu-timeout");
    if (gpu_dispatch != "auto" && gpu_dispatch != "direct" && gpu_dispatch != "pool")
    {
        std::cerr << "--gpu-dispatch must be auto, direct, or pool" << std::endl;
        return EXIT_FAILURE;
    }
    if (gpu_dispatch == "pool" && num_threads == 1)
    {
        std::cerr << "--gpu-dispatch pool requires --threads > 1" << std::endl;
        return EXIT_FAILURE;
    }
    if (!std::isfinite(gpu_timeout) || gpu_timeout <= 0)
    {
        std::cerr << "--gpu-timeout must be positive and finite" << std::endl;
        return EXIT_FAILURE;
    }
    try
    {
        simphony::gpu_wire::deadlineAfter(simphony::gpu_wire::Clock::now(), gpu_timeout);
    }
    catch (const std::invalid_argument&)
    {
        std::cerr << "--gpu-timeout exceeds the representable clock interval" << std::endl;
        return EXIT_FAILURE;
    }

    if (no_cpu_recorder && program.is_used("--particle"))
    {
        cerr << "--no-cpu-recorder is supported only for the optical-photon torch source" << endl;
        return EXIT_FAILURE;
    }

    if (program.is_used("--particle") && particle_name == "opticalphoton")
    {
        cerr << "--particle opticalphoton is unsupported; "
                "omit --particle to use the optical-photon torch source"
             << endl;
        return EXIT_FAILURE;
    }

#ifndef G4MULTITHREADED
    if (num_threads > 1)
    {
        cerr << "This Geant4 installation was built without multithreading support" << endl;
        return EXIT_FAILURE;
    }
#endif

    const auto visible_devices = simphony::DiscoverGpuDevices();
    if (program.get<bool>("--list-gpus"))
    {
        nlohmann::json listed = nlohmann::json::array();
        for (const auto& device : visible_devices)
            listed.push_back({{"ordinal", device.ordinal}, {"uuid", device.uuid}, {"name", device.name}, {"memory_bytes", device.memory_bytes}});
        std::cout << listed.dump() << std::endl;
        return EXIT_SUCCESS;
    }
    if (visible_devices.empty())
    {
        std::cerr << "simg4ox requires at least one CUDA-visible GPU" << std::endl;
        return EXIT_FAILURE;
    }
    const bool use_gpu_pool = num_threads > 1 &&
                              (gpu_dispatch == "pool" || (gpu_dispatch == "auto" && visible_devices.size() > 1));
    std::vector<simphony::GpuDevice> used_devices = use_gpu_pool
                                                        ? visible_devices
                                                        : std::vector<simphony::GpuDevice>{visible_devices.front()};
    simphony::Config cfg(config_name);

    // Device discovery used to happen as a side effect of constructing the
    // serial CPU SEvt. MT workers deliberately do not share that instance, so
    // initialize global event/device metadata explicitly on the master.
    if (!use_gpu_pool)
        SEventConfig::Initialize();

    PrimaryParticleConfig primary;
    primary.enabled = program.is_used("--particle");
    primary.particle = primary.enabled ? particle_name : "opticalphoton";
    primary.momentum_gev_c = program.get<double>("--momentum-gev-c");
    primary.multiplicity = program.get<int>("--multiplicity");
    primary.position_mm = G4ThreeVector(cfg.torch.pos.x, cfg.torch.pos.y, cfg.torch.pos.z);
    primary.direction = G4ThreeVector(cfg.torch.mom.x, cfg.torch.mom.y, cfg.torch.mom.z);

    if (primary.enabled && (primary.momentum_gev_c <= 0.0 || primary.multiplicity <= 0))
    {
        cerr << "Particle momentum and multiplicity must both be positive" << endl;
        return EXIT_FAILURE;
    }

    EventTimingRecorder timing(cfg.event_timing_output);
    EventTimingMetadata timing_metadata;
    timing_metadata.geometry = std::filesystem::absolute(gdml_file).string();
    timing_metadata.config = config_name;
    timing_metadata.macro = std::filesystem::absolute(macro_name).string();
    timing_metadata.simphony_version = SIMPHONY_VERSION;
    timing_metadata.geant4_version = G4Version;
    timing_metadata.primary_particle = primary.particle;
    timing_metadata.primary_momentum_gev_c = primary.enabled ? primary.momentum_gev_c : 0.0;
    timing_metadata.primary_multiplicity = primary.enabled ? primary.multiplicity : cfg.torch.numphoton;
    timing_metadata.num_threads = num_threads;
    timing_metadata.gpu_dispatch = use_gpu_pool ? "pool" : "direct";
    for (const auto& device : used_devices)
        timing_metadata.gpu_devices.push_back({device.ordinal, device.uuid, device.name, device.memory_bytes});
    timing_metadata.cpu_recorder_enabled = num_threads == 1 && !no_cpu_recorder;
    timing_metadata.torch_seed = primary.enabled ? -1 : 0;

    if (timing.enabled())
    {
        int            device = 0;
        cudaDeviceProp properties{};
        if (cudaGetDevice(&device) == cudaSuccess && cudaGetDeviceProperties(&properties, device) == cudaSuccess)
        {
            timing_metadata.gpu_name = properties.name;
            timing_metadata.gpu_device_id = device;
            timing_metadata.gpu_memory_bytes = properties.totalGlobalMem;
        }
        cudaDriverGetVersion(&timing_metadata.cuda_driver_version);
        cudaRuntimeGetVersion(&timing_metadata.cuda_runtime_version);
    }

    if (program.is_used("--seed"))
    {
        const long seed = program.get<long>("--seed");
        CLHEP::HepRandom::setTheSeed(seed);
        timing_metadata.random_seed = seed;
        G4cout << "Random seed set to: " << seed << G4endl;
    }

    // Configure Geant4
    // The physics list must be instantiated before other user actions
    G4VModularPhysicsList* physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);

    if (primary.enabled)
    {
        // Retain Cerenkov/scintillation photon counts for genstep collection,
        // but do not place optical secondaries on the Geant4 CPU stack.
        G4OpticalParameters::Instance()->SetCerenkovStackPhotons(false);
        G4OpticalParameters::Instance()->SetScintStackPhotons(false);
        G4cout << "Primary source: " << primary.multiplicity << ' ' << primary.particle << " at "
               << primary.momentum_gev_c << " GeV/c per event, position " << primary.position_mm
               << ", direction " << primary.direction.unit() << G4endl;
    }

    const bool    multithreaded = num_threads > 1;
    G4RunManager* run_mgr = multithreaded
                                ? G4RunManagerFactory::CreateRunManager(G4RunManagerType::MTOnly, true, num_threads)
                                : G4RunManagerFactory::CreateRunManager(G4RunManagerType::SerialOnly);

    const G4int configured_threads = multithreaded ? run_mgr->GetNumberOfThreads() : 1;
    if (multithreaded && configured_threads != num_threads)
    {
        cerr << "Requested " << num_threads << " Geant4 CPU threads, but the run manager configured "
             << configured_threads << endl;
        delete run_mgr;
        delete physics;
        return EXIT_FAILURE;
    }

    G4cout << "simg4ox: Geant4 run manager: " << (multithreaded ? "MT" : "serial")
           << ", CPU threads: " << configured_threads << G4endl;

    run_mgr->SetUserInitialization(physics);
    auto shared_state = std::make_shared<Simg4oxSharedState>(timing.enabled() ? &timing : nullptr);
    shared_state->gpu_devices = used_devices;
    if (use_gpu_pool)
    {
        try
        {
            auto& options = shared_state->gpu_options.emplace();
            options.devices = used_devices;
            options.executable = "/proc/self/exe";
            options.timeout_seconds = gpu_timeout;
            const auto output_dir = std::filesystem::absolute(simphony::ResolveRunOutputPath(cfg.output_dir));
            std::filesystem::create_directories(output_dir);
            options.output = simphony::CreateUniqueDirectory(output_dir / "gpu-workers-XXXXXX");
            G4cout << "simg4ox: GPU worker output: " << options.output << G4endl;
        }
        catch (const std::exception& error)
        {
            shared_state->RemoveGpuSnapshot(error.what());
            delete run_mgr;
            return EXIT_FAILURE;
        }
    }
    G4cout << "simg4ox: GPU dispatch: " << (use_gpu_pool ? "pool" : "direct")
           << ", GPUs: " << used_devices.size() << G4endl;
    run_mgr->SetUserInitialization(new DetectorConstruction(gdml_file, cfg, shared_state->gpu_options ? &*shared_state->gpu_options : nullptr));
    run_mgr->SetUserInitialization(new ActionInitialization(
        cfg, shared_state, multithreaded, primary, timing.enabled() ? &timing : nullptr, timing_metadata,
        !no_cpu_recorder));

    G4UIExecutive* uix = nullptr;
    G4VisManager*  vis = nullptr;

    if (interactive)
    {
        uix = new G4UIExecutive(argc, argv);
        vis = new G4VisExecutive;
        vis->Initialize();
    }

    int status = EXIT_SUCCESS;
    try
    {
        G4UImanager::GetUIpointer()->ApplyCommand("/control/execute " + macro_name);
        if (interactive)
            uix->SessionStart();
    }
    catch (const std::exception& error)
    {
        shared_state->RemoveGpuSnapshot(error.what());
        status = EXIT_FAILURE;
    }

    delete uix;
    delete vis;
    delete run_mgr;

    return status;
}
