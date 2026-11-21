#include "GpuWorker.h"
#include "GpuWorkerProtocol.h"

#include "CSGFoundry.h"
#include "CSGOptiX.h"
#include "EventTiming.hh"
#include "OPTICKS_LOG.hh"
#include "QSim.hh"
#include "SEventConfig.hh"
#include "SEvt.hh"
#include "SSim.hh"
#include "config.h"
#include "spath.h"

#include <algorithm>
#include <cuda_runtime.h>
#include <fcntl.h>
#include <iostream>
#include <sys/prctl.h>
#include <sys/stat.h>

namespace wire = simphony::gpu_wire;
namespace
{
struct DiagnosticFd
{
    const int fd;
    DiagnosticFd(int fd, const std::filesystem::path& path) :
        fd(fd)
    {
        if (fd < 0)
            throw std::filesystem::filesystem_error("open GPU diagnostic", path, std::error_code(errno, std::generic_category()));
    }
    DiagnosticFd(const DiagnosticFd&) = delete;
    ~DiagnosticFd()
    {
        close(fd);
    }
};

void saveGenstepDump(int workerDir, const NP& gensteps)
{
    const char* path = std::getenv("QSim__SAVE_IGS_PATH");
    auto        name = path ? std::filesystem::path(path).filename() : std::filesystem::path("igs.npy");
    if (name.empty() || name == "." || name == ".." || name.native().find('$') != std::string::npos)
        name = "igs.npy";

    if (mkdirat(workerDir, "gensteps", 0700) != 0 && errno != EEXIST)
        throw std::system_error(errno, std::generic_category(), "create GPU gensteps directory");
    // Pin the directory and the newly created inode. Replacing either name with
    // a symlink cannot redirect writes after these opens.
    DiagnosticFd directory(openat(workerDir, "gensteps", O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC), "gensteps");
    DiagnosticFd file(openat(directory.fd, name.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600), name);
    // makeGenstepArrayFromVector produces a plain NumPy array without sidecars.
    gensteps.save(("/proc/self/fd/" + std::to_string(file.fd)).c_str());
}

int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(wire::Clock::now().time_since_epoch()).count();
}

CSGOptiX* initialize(const std::filesystem::path& config, const std::filesystem::path& geometry,
                     const std::filesystem::path& output, const char* expectedUuid)
{
    const auto devices = simphony::DiscoverGpuDevices();
    if (devices.size() != 1 || devices[0].uuid != expectedUuid)
        throw std::runtime_error("helper did not acquire its assigned CUDA-visible UUID");

    const auto cfg = simphony::Config::FromFile(config);
    // Expand tokens as SEvt::RunDir does. Parent components can resolve
    // differently once Opticks creates directories or follows a symlink.
    const std::filesystem::path reldir = spath::Resolve(cfg.event_reldir.c_str());
    if (std::ranges::find(reldir, std::filesystem::path("..")) != reldir.end())
        throw std::runtime_error("event.event_reldir contains a parent directory after token expansion: " + reldir.string());
    const auto root = std::filesystem::canonical(output);
    const auto eventDir = std::filesystem::weakly_canonical(root / reldir.relative_path());
    if (std::mismatch(root.begin(), root.end(), eventDir.begin(), eventDir.end()).first != root.end() ||
        eventDir.native().find('$') != std::string::npos)
        throw std::runtime_error("event.event_reldir must resolve to a literal path inside the GPU helper directory: " + eventDir.string());
    SEventConfig::SetOutFold(root.c_str());
    SEventConfig::SetEventReldir(eventDir.lexically_relative(root).c_str());
    // Preserve configured profile filenames (including the % index token) under this worker's directory.
    EventTimingProfile::Configure(cfg.event_timing_profile_enabled, output / cfg.event_timing_profile_output.filename(),
                                  cfg.event_timing_profile_path_index);
    SEventConfig::Initialize();
    if (!std::filesystem::is_directory(geometry / "CSGFoundry/SSim/stree") ||
        !std::filesystem::is_regular_file(geometry / "CSGFoundry/prim.npy"))
        throw std::runtime_error("missing GPU geometry snapshot in " + geometry.string());
    SSim::Load(geometry.c_str(), "CSGFoundry/SSim");
    CSGFoundry* foundry = CSGFoundry::Load(geometry.c_str());
    if (!foundry)
        throw std::runtime_error("could not load GPU geometry");
    CSGOptiX* backend = CSGOptiX::Create(foundry);
    if (!backend || !SEvt::Get_EGPU())
        throw std::runtime_error("could not initialize GPU backend");
    return backend;
}

void simulate(CSGOptiX* backend, const wire::Header& h, std::unique_ptr<NP>& input)
{
    auto deadline = wire::Deadline::max(); // Parent enforces the request deadline and cancels the socket.
    if ((h.type != wire::Type::Gensteps && h.type != wire::Type::Photons) || !h.count)
        throw std::runtime_error("expected a nonempty event request");
    if (h.count > INT_MAX)
        throw std::runtime_error("GPU event record count exceeds the backend's index range");
    std::vector<quad6> gensteps;
    if (h.type == wire::Type::Gensteps)
    {
        gensteps.resize(h.count);
        wire::readAll(wire::Socket, gensteps.data(), h.bytes, deadline);
    }
    else
    {
        if (h.count > static_cast<uint64_t>(SEventConfig::MaxSlot()))
            throw std::runtime_error("GPU input exceeds this helper's launch capacity");
        const auto count = static_cast<int>(h.count);
        if (!input || input->shape[0] != count)
            input.reset(NP::Make<float>(count, 4, 4));
        wire::readAll(wire::Socket, input->bytes(), h.bytes, deadline);
    }

    SEvt*      evt = SEvt::Get_EGPU();
    const auto start = nowNs();
    if (h.type == wire::Type::Photons)
        SEvt::SetInputPhoton(input.get());
    else
        for (const auto& step : gensteps) evt->addGenstep(step);
    backend->simulate(h.event, false);
    const auto rc = cudaDeviceSynchronize();
    if (rc != cudaSuccess)
        throw std::runtime_error(cudaGetErrorString(rc));
    const auto           end = nowNs();
    wire::Header         reply(wire::Type::Hits, h.run, h.event, evt->getNumHit());
    std::vector<sphoton> hits(reply.count);
    for (size_t i = 0; i < hits.size(); ++i) evt->getHit(hits[i], i);
    backend->reset(h.event);
    reply.start = start;
    reply.end = end;
    reply.cleanup = nowNs();
    wire::writeHeader(wire::Socket, reply, deadline);
    wire::writeAll(wire::Socket, hits.data(), reply.bytes, deadline);
}
} // namespace

int simphony::RunGpuWorker(int argc, char** argv)
{
    if (argc != 8)
    {
        std::cerr << "Invalid arguments for the internal simg4ox GPU worker\n";
        return 2;
    }
    int run = 0;
    int event = -1;
    try
    {
        const pid_t expectedParent = std::stoi(argv[7]);
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1)
            throw std::system_error(errno, std::generic_category(), "set GPU helper parent-death signal");
        if (getppid() != expectedParent)
            throw std::runtime_error("GPU helper owner exited during startup");
        run = std::stoi(argv[5]);
        // posix_spawn redirects the console into the per-device log. Keep the caller's working
        // directory for relative PTX and input paths without a shared plog logfile.
        SLOG                              log(argc, argv);
        static plog::ConsoleAppender<FMT> console;
        SLOG_INIT(log.level, &console, nullptr);
        OPTICKS_LOG_::Initialize(&log, plog::get(), nullptr);
        DiagnosticFd output(open(argv[4], O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC), argv[4]);
        auto*        backend = initialize(argv[2], argv[3], argv[4], argv[6]);
        backend->sim->save_genstep_dump = [fd = output.fd](const NP& gensteps) { saveGenstepDump(fd, gensteps); };
        SEvt::SetRunMeta<int>("G4RunID", run);
        // Each helper may see a sparse, out-of-order subset of the Geant4 run.
        EventTimingProfile::MarkUntagged("GpuWorker__BeginOfRun");
        const auto maxSlots = SEventConfig::MaxSlot();
        if (maxSlots <= 0)
            throw std::runtime_error("GPU backend reported no usable launch slots");
        wire::writeHeader(wire::Socket, {wire::Type::Ready, run, -1, static_cast<uint64_t>(maxSlots)}, wire::Deadline::max());
        std::unique_ptr<NP> input;
        for (;;)
        {
            wire::Header h(wire::Type::Ready, run, -1);
            try
            {
                h = wire::readHeader(wire::Socket, wire::Deadline::max());
            }
            catch (const wire::EndOfStream&)
            {
                event = -1; // Run-level errors belong to the final handshake.
                EventTimingProfile::MarkUntagged("GpuWorker__EndOfRun");
                EventTimingProfile::Write();
                wire::writeHeader(wire::Socket, {wire::Type::Finished, run, -1}, wire::Deadline::max());
                return 0;
            }
            event = h.event;
            if (h.run != run)
                throw std::runtime_error("GPU worker run identity mismatch");
            simulate(backend, h, input);
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        try
        {
            const auto       deadline = wire::Clock::now() + std::chrono::seconds(1);
            std::string_view error(e.what());
            error = error.substr(0, wire::MaxError);
            wire::writeHeader(wire::Socket, {wire::Type::Error, run, event, error.size()}, deadline);
            wire::writeAll(wire::Socket, error.data(), error.size(), deadline);
            // Keep the descriptor live until the parent has read the structured error
            // and canceled the pool, so its disconnect monitor cannot mask the cause.
            char acknowledgement;
            wire::readAll(wire::Socket, &acknowledgement, 1, deadline);
        }
        catch (const std::exception&)
        {
        }
        return 1;
    }
}
