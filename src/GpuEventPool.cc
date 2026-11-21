#include "GpuEventPool.h"
#include "GpuDeviceSelector.h"
#include "GpuWorker.h"
#include "GpuWorkerProtocol.h"

#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cuda_runtime.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <utility>

extern char** environ;
namespace simphony
{
namespace wire = gpu_wire;
using namespace std::chrono_literals;

namespace
{
void check(int error, const char* operation)
{
    if (error)
        throw std::system_error(error, std::generic_category(), operation);
}

void checkCuda(cudaError_t error)
{
    if (error != cudaSuccess)
        throw std::runtime_error(std::string("CUDA discovery: ") + cudaGetErrorString(error));
}

struct Fd
{
    int value;
    explicit Fd(int fd) :
        value(fd)
    {
    }
    Fd(const Fd&) = delete;
    ~Fd()
    {
        if (value >= 0)
            close(value);
    }
};

struct Actions
{
    posix_spawn_file_actions_t value;
    Actions()
    {
        check(posix_spawn_file_actions_init(&value), "spawn actions");
    }
    ~Actions()
    {
        posix_spawn_file_actions_destroy(&value);
    }
};

struct Worker
{
    int      socket{-1};
    pid_t    pid{-1};
    bool     busy{false};
    int      exit_status{-1};
    bool     termination_needed{false};
    uint64_t max_slots{};
};

bool validHitCount(const GpuPayload& payload, uint64_t count)
{
    if (!payload.charged)
        return count <= payload.photons.size();
    // Each generated photon can contribute at most one hit. Avoid overflowing a genstep sum.
    for (const auto& step : payload.gensteps)
    {
        if (count <= step.numphoton())
            return true;
        count -= step.numphoton();
    }
    return count == 0;
}

Worker spawn(const GpuPoolOptions& options, const GpuDevice& device)
{
    const auto dir = std::filesystem::absolute(options.output) / ("gpu-" + std::to_string(device.ordinal));
    std::filesystem::create_directories(dir);
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) < 0)
        throw std::system_error(errno, std::generic_category(), "GPU socketpair");
    Fd parent(sockets[0]), child(sockets[1]);

    Actions actions;
    check(posix_spawn_file_actions_adddup2(&actions.value, child.value, wire::Socket), "spawn GPU socket");
    check(posix_spawn_file_actions_addopen(&actions.value, STDOUT_FILENO, (dir / "worker.log").c_str(),
                                           O_CREAT | O_WRONLY | O_TRUNC, 0600),
          "spawn GPU log");
    check(posix_spawn_file_actions_adddup2(&actions.value, STDOUT_FILENO, STDERR_FILENO), "spawn GPU stderr");
    // No unrelated parent descriptors (e.g. files opened by Geant4) enter the helper.
    check(posix_spawn_file_actions_addclosefrom_np(&actions.value, wire::Socket + 1), "spawn close descriptors");

    std::vector<std::string> args{options.executable.string(), GpuWorkerFlag, options.config_file.string(),
                                  std::filesystem::absolute(options.geometry).string(), dir.string(),
                                  std::to_string(options.run_id), device.uuid, std::to_string(getpid())};
    std::vector<char*>       argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);

    std::string mask = "CUDA_VISIBLE_DEVICES=" + device.uuid;
    // SEvt reads SAVE_RUNDIR at shared-library initialization, before main.
    char               saveRunDir[] = "SEvt__SAVE_RUNDIR=1";
    std::vector<char*> env;
    for (char** variable = environ; *variable; ++variable)
    {
        const std::string_view entry(*variable);
        if (!entry.starts_with("CUDA_VISIBLE_DEVICES=") && !entry.starts_with("SEvt__SAVE_RUNDIR="))
            env.push_back(*variable);
    }
    env.push_back(saveRunDir);
    env.push_back(mask.data());
    env.push_back(nullptr);

    pid_t pid;
    check(posix_spawn(&pid, argv.front(), &actions.value, nullptr, argv.data(), env.data()), "spawn GPU helper");
    return {std::exchange(parent.value, -1), pid};
}

void reap(std::vector<Worker>& workers, wire::Deadline gracefulDeadline)
{
    auto waitUntil = [&](wire::Deadline deadline) {
        for (;;)
        {
            bool pending = false;
            for (auto& w : workers)
            {
                if (w.pid < 0)
                    continue;
                int         status;
                const pid_t rc = waitpid(w.pid, &status, WNOHANG);
                if (rc == w.pid)
                {
                    w.exit_status = status;
                    w.pid = -1;
                }
                else if (rc < 0 && errno == ECHILD)
                    w.pid = -1;
                else
                    pending = true;
            }
            if (!pending)
                return true;
            if (wire::Clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(10ms);
        }
    };
    if (waitUntil(gracefulDeadline))
        return;
    for (auto& w : workers)
        if (w.pid > 0)
        {
            w.termination_needed = true;
            kill(w.pid, SIGTERM);
        }
    if (waitUntil(wire::Clock::now() + 250ms))
        return;
    for (auto& w : workers)
        if (w.pid > 0)
            kill(w.pid, SIGKILL);
    if (waitUntil(wire::Clock::now() + 1s))
        return;
    // A process stuck in uninterruptible kernel I/O may defer SIGKILL. Let the caller
    // return on time and retain responsibility for waitpid until Linux releases it.
    for (auto& w : workers)
        if (w.pid > 0)
            std::thread([pid = w.pid] {
                while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR)
                {
                }
            }).detach();
}
} // namespace

std::vector<GpuDevice> DiscoverGpuDevices()
{
    int        count = 0;
    const auto rc = cudaGetDeviceCount(&count);
    if (rc == cudaErrorNoDevice)
        return {};
    checkCuda(rc);
    std::vector<GpuDevice> devices;
    const char*            mask = std::getenv("CUDA_VISIBLE_DEVICES");
    for (int i = 0; i < count; ++i)
    {
        cudaDeviceProp prop{};
        checkCuda(cudaGetDeviceProperties(&prop, i));
        std::ostringstream uuid;
        uuid << std::hex << std::setfill('0');
        for (int j = 0; j < 16; ++j)
        {
            if (j == 4 || j == 6 || j == 8 || j == 10)
                uuid << '-';
            uuid << std::setw(2) << static_cast<unsigned>(static_cast<unsigned char>(prop.uuid.bytes[j]));
        }
        char pci[32]{};
        checkCuda(cudaDeviceGetPCIBusId(pci, sizeof(pci), i));
        for (char& c : pci)
            if (c >= 'A' && c <= 'F')
                c += 'a' - 'A';
        std::ifstream info(std::filesystem::path("/proc/driver/nvidia/gpus") / pci / "information");
        std::string   physicalUuid;
        for (std::string line; std::getline(info, line);)
            if (line.starts_with("GPU UUID:"))
            {
                std::istringstream(line.substr(9)) >> physicalUuid;
                break;
            }
        devices.push_back({i, gpu_detail::DeviceSelector(uuid.str(), mask ? mask : "", physicalUuid, prop.name),
                           prop.name, prop.totalGlobalMem});
    }
    return devices;
}

struct GpuEventPool::State
{
    GpuPoolOptions          options;
    std::vector<Worker>     workers;
    uint64_t                largest_capacity{};
    size_t                  next{0};
    std::mutex              mutex;
    std::condition_variable available;
    std::string             failure;
    bool                    stopping{false};
    std::thread             monitor;

    explicit State(GpuPoolOptions opts) :
        options(std::move(opts))
    {
    }
    wire::Deadline deadline() const
    {
        return wire::deadlineAfter(wire::Clock::now(), options.timeout_seconds);
    }

    void start()
    {
        if (options.devices.empty() || options.run_id < 0 || !std::isfinite(options.timeout_seconds) || options.timeout_seconds <= 0)
            throw std::invalid_argument("GPU pool requires devices, nonnegative run ID and a positive finite deadline");
        workers.reserve(options.devices.size());
        const auto startupDeadline = deadline();
        for (const auto& device : options.devices) workers.push_back(spawn(options, device));
        for (size_t i = 0; i < workers.size(); ++i)
        {
            try
            {
                workers[i].max_slots = wire::expect(workers[i].socket, wire::Type::Ready, options.run_id, -1, startupDeadline).count;
                largest_capacity = std::max(largest_capacity, workers[i].max_slots);
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error(context(i) + e.what());
            }
        }
        monitor = std::thread([this] { watch(); });
    }

    std::string context(size_t i) const
    {
        return "GPU helper " + options.devices[i].uuid + " (logs in " + options.output.string() +
               "/gpu-" + std::to_string(options.devices[i].ordinal) + "): ";
    }

    void watch()
    {
        std::vector<pollfd> sockets;
        for (auto& w : workers) sockets.push_back({w.socket, POLLRDHUP, 0});
        for (;;)
        {
            int ready = poll(sockets.data(), sockets.size(), -1);
            if (ready < 0 && errno == EINTR)
                continue;
            std::lock_guard lock(mutex);
            if (stopping || !failure.empty())
                return;
            if (ready < 0)
                failLocked("GPU helper monitor: " + std::system_category().message(errno));
            else
                for (size_t i = 0; i < sockets.size(); ++i)
                    if (sockets[i].revents)
                    {
                        failLocked(context(i) + "connection lost");
                        break;
                    }
            return;
        }
    }

    void failLocked(std::string message)
    {
        if (!failure.empty())
            return;
        failure = std::move(message);
        for (auto& w : workers) shutdown(w.socket, SHUT_RDWR);
        available.notify_all();
    }

    void closeAndReap(wire::Deadline gracefulDeadline)
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            for (auto& w : workers) shutdown(w.socket, SHUT_RDWR);
            available.notify_all();
        }
        if (monitor.joinable())
            monitor.join();
        for (auto& w : workers) close(w.socket);
        reap(workers, gracefulDeadline);
    }

    ~State()
    {
        if (!workers.empty())
            closeAndReap(wire::Clock::now() + 500ms);
    }

    void finish()
    {
        if (!workers.empty())
        {
            const auto end = deadline();
            {
                // Suppress the disconnect monitor before sending EOF. A Finished frame
                // followed by a successful waitpid now distinguishes a normal exit.
                std::lock_guard lock(mutex);
                if (std::any_of(workers.begin(), workers.end(), [](const Worker& w) { return w.busy; }))
                    throw std::logic_error("GPU pool Finish requires all leases to be released");
                stopping = true;
                for (auto& w : workers) shutdown(w.socket, SHUT_WR);
                available.notify_all();
            }
            for (size_t i = 0; i < workers.size() && failure.empty(); ++i)
            {
                try
                {
                    wire::expect(workers[i].socket, wire::Type::Finished, options.run_id, -1, end);
                }
                catch (const std::exception& e)
                {
                    failure = context(i) + e.what();
                }
            }
            closeAndReap(failure.empty() ? end : wire::Clock::now() + 500ms);
            for (size_t i = 0; i < workers.size() && failure.empty(); ++i)
            {
                const auto& w = workers[i];
                if (w.termination_needed)
                    failure = context(i) + "helper did not exit by the shutdown deadline";
                else if (w.exit_status < 0)
                    failure = context(i) + "no child exit status";
                else if (WIFSIGNALED(w.exit_status))
                    failure = context(i) + "terminated by signal " + std::to_string(WTERMSIG(w.exit_status));
                else if (!WIFEXITED(w.exit_status) || WEXITSTATUS(w.exit_status) != 0)
                    failure = context(i) + "unsuccessful exit status " + std::to_string(WEXITSTATUS(w.exit_status));
            }
            workers.clear();
        }
        if (!failure.empty())
            throw std::runtime_error(failure);
    }
    void release(size_t i)
    {
        std::lock_guard lock(mutex);
        workers[i].busy = false;
        // Different queued events may be waiting for different devices.
        available.notify_all();
    }

    GpuResult simulate(size_t i, const GpuPayload& p)
    {
        const auto end = deadline();
        const int  fd = workers[i].socket;
        try
        {
            wire::Header h(p.charged ? wire::Type::Gensteps : wire::Type::Photons, options.run_id, p.event_id,
                           p.charged ? p.gensteps.size() : p.photons.size());
            wire::writeHeader(fd, h, end);
            const void* data = p.charged ? static_cast<const void*>(p.gensteps.data()) : static_cast<const void*>(p.photons.data());
            wire::writeAll(fd, data, h.bytes, end);
            auto reply = wire::expect(fd, wire::Type::Hits, options.run_id, p.event_id, end);
            if (!validHitCount(p, reply.count))
                throw std::runtime_error("GPU helper hit count exceeds submitted photons");
            if (reply.start <= 0 || reply.end < reply.start || reply.cleanup < reply.end)
                throw std::runtime_error("invalid GPU helper service timestamps");
            GpuResult result{{}, reply.start, reply.end, reply.cleanup};
            result.hits.resize(reply.count);
            wire::readAll(fd, result.hits.data(), reply.bytes, end);
            std::lock_guard lock(mutex);
            if (!failure.empty())
                throw std::runtime_error(failure);
            return result;
        }
        catch (const std::exception& e)
        {
            std::lock_guard lock(mutex);
            failLocked(context(i) + e.what());
            throw std::runtime_error(failure);
        }
    }
};

GpuEventPool::GpuEventPool(GpuPoolOptions opts) :
    state_(std::make_unique<State>(std::move(opts)))
{
    state_->start();
}
GpuEventPool::~GpuEventPool() = default;
void GpuEventPool::Finish()
{
    state_->finish();
}
GpuEventPool::Lease GpuEventPool::Acquire(GpuPayload payload)
{
    // Torch input becomes one genstep. The backend can slice a charged event only between gensteps.
    uint64_t requiredSlots = payload.photons.size();
    for (const auto& step : payload.gensteps) requiredSlots = std::max<uint64_t>(requiredSlots, step.numphoton());
    const auto       idle = [requiredSlots](const Worker& w) { return !w.busy && w.max_slots >= requiredSlots; };
    auto&            s = *state_;
    std::unique_lock lock(s.mutex);
    if (!s.stopping && s.failure.empty())
    {
        if (payload.event_id < 0 || (payload.charged && (!payload.photons.empty() || payload.gensteps.empty())) ||
            (!payload.charged && (!payload.gensteps.empty() || payload.photons.empty())))
            s.failLocked("expected one nonempty GPU event payload");
        else if (requiredSlots > s.largest_capacity)
            s.failLocked("GPU event " + std::to_string(payload.event_id) + " requires " + std::to_string(requiredSlots) +
                         " slots for one genstep, but the largest helper supports " + std::to_string(s.largest_capacity));
    }
    s.available.wait(lock, [&] { return s.stopping || !s.failure.empty() || std::any_of(s.workers.begin(), s.workers.end(), idle); });
    if (s.stopping)
        throw std::logic_error("GPU pool is closed");
    if (!s.failure.empty())
        throw std::runtime_error(s.failure);
    while (!idle(s.workers[s.next])) s.next = (s.next + 1) % s.workers.size();
    const size_t i = s.next;
    s.next = (s.next + 1) % s.workers.size();
    s.workers[i].busy = true;
    return {&s, i, std::move(payload)};
}
GpuEventPool::Lease::Lease(State* state, size_t index, GpuPayload&& payload) :
    state_(state),
    index_(index),
    payload_(std::move(payload))
{
}
GpuEventPool::Lease::Lease(Lease&& other) noexcept :
    state_(std::exchange(other.state_, nullptr)),
    index_(other.index_),
    payload_(std::move(other.payload_))
{
}
GpuEventPool::Lease& GpuEventPool::Lease::operator=(Lease&& other) noexcept
{
    if (this == &other)
        return *this;
    if (state_)
        state_->release(index_);
    state_ = std::exchange(other.state_, nullptr);
    index_ = other.index_;
    payload_ = std::move(other.payload_);
    return *this;
}
GpuEventPool::Lease::~Lease()
{
    if (state_)
        state_->release(index_);
}
const GpuDevice& GpuEventPool::Lease::device() const
{
    return state_->options.devices[index_];
}
GpuResult GpuEventPool::Lease::Simulate()
{
    return state_->simulate(index_, payload_);
}
} // namespace simphony
