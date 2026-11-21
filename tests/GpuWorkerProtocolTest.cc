#include "GpuWorkerProtocol.h"
#include "GpuDeviceSelector.h"
#include "GpuWorker.h"
#include <fcntl.h>
#include <iostream>

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <future>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>

using namespace simphony;
namespace wire = simphony::gpu_wire;
using namespace std::chrono_literals;

namespace
{
void require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

template <typename F>
std::string rejects(F operation)
{
    try
    {
        operation();
    }
    catch (const std::exception& error)
    {
        return error.what();
    }
    throw std::runtime_error("operation unexpectedly succeeded");
}

void deviceSelectors()
{
    const std::string physical = "GPU-deadbeef-1122-3344-5566-778899aabbcc";
    const std::string ci = "01234567-abcd-0123-4567-89ab01234567";
    using gpu_detail::DeviceSelector;
    require(DeviceSelector(ci, physical + ",MIG-" + ci, "", "") == "MIG-" + ci,
            "explicit compute instance UUID was rewritten as a physical GPU UUID");
    require(DeviceSelector(ci, "1,0", physical, "") == "MIG-" + ci,
            "numeric mask lost compute instance identity");
    require(DeviceSelector(physical.substr(4), "", physical, "") == physical, "physical GPU UUID changed");
}
void protocol()
{
    const auto   tick = wire::Clock::duration{1};
    const auto   origin = wire::Deadline::max() - 2 * tick;
    const double tickSeconds = std::chrono::duration<double>(tick).count();
    require(wire::deadlineAfter(origin, 0.5 * tickSeconds) == origin + tick, "last finite clock tick must remain usable");
    require(rejects([&] { wire::deadlineAfter(origin, 1.5 * tickSeconds); }).find("timeout") != std::string::npos,
            "a finite timeout rounded up to the unlimited sentinel");

    int sockets[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, "socketpair");
    auto         deadline = wire::Clock::now() + 5s;
    wire::Header header{wire::Type::Photons, 5, 729, 2};
    const char*  bytes = reinterpret_cast<const char*>(&header);
    auto         sender = std::async(std::launch::async, [&] {
        for (size_t offset = 0; offset < sizeof(header); ++offset)
            wire::writeAll(sockets[0], bytes + offset, 1, deadline);
    });
    auto         received = wire::readHeader(sockets[1], deadline);
    require(received.event == 729 && received.run == 5 && received.count == 2, "partial header transfer");
    sender.get();
    for (const auto type : {wire::Type::Photons, wire::Type::Hits})
    {
        wire::Header large(type, 5, 730, 134217729);
        wire::writeHeader(sockets[0], large, deadline);
        const auto frame = wire::readHeader(sockets[1], deadline);
        require(frame.event == 730 && frame.count == 134217729 && frame.bytes == 8589934656ULL,
                "a whole event beyond 8 GiB lost its identity or size");
    }
    rejects([] { wire::Header(wire::Type::Hits, 5, 730, UINT64_MAX); });
    auto invalidReady = wire::Header(wire::Type::Ready, 5, -1, 8);
    invalidReady.count = 0;
    wire::writeHeader(sockets[0], invalidReady, deadline);
    rejects([&] { wire::readHeader(sockets[1], deadline); });
    for (int bad = 0; bad < 5; ++bad)
    {
        auto malformed = header;
        switch (bad)
        {
        case 0:
            ++malformed.version;
            break;
        case 1:
            malformed.type = static_cast<wire::Type>(999);
            break;
        case 2:
            malformed.count = UINT64_MAX;
            break;
        case 3:
            ++malformed.bytes;
            break;
        case 4:
            malformed.event = -5;
            break;
        }
        wire::writeAll(sockets[0], &malformed, sizeof(malformed), deadline);
        rejects([&] { wire::readHeader(sockets[1], deadline); });
    }
    wire::writeAll(sockets[0], bytes, 7, deadline);
    close(sockets[0]);
    const auto truncated = rejects([&] { wire::readHeader(sockets[1], deadline); });
    require(truncated.find("truncated") != std::string::npos, "partial request header was treated as a clean worker shutdown");
    try
    {
        wire::readHeader(sockets[1], deadline);
        throw std::runtime_error("missing boundary EOF");
    }
    catch (const wire::EndOfStream&)
    {
    }
    rejects([&] { wire::writeAll(sockets[1], bytes, 7, deadline); });
    close(sockets[1]);
}

int fixture(char** argv)
{
    const std::string mode = argv[2];
    const auto        output = std::filesystem::path(argv[4]);
    const int         run = std::stoi(argv[5]);
    require(std::string(std::getenv("CUDA_VISIBLE_DEVICES")) == argv[6], "child device mask");
    require(std::string(std::getenv("SEvt__SAVE_RUNDIR")) == "1", "helper-local run metadata");
    require(getppid() == std::stoi(argv[7]), "GPU helper owner identity");
    std::ofstream(output / "pid") << getpid();
    if (mode == "hang-start" || mode == "ignore-close")
        signal(SIGTERM, SIG_IGN);
    if (mode == "hang-start")
        for (;;) pause();
    const auto     deadline = wire::Clock::now() + 20s;
    const uint64_t maxSlots = mode == "capacity" ? (std::string(argv[6]) == "GPU-fixture-A" ? 4 : 8) : 65536;
    wire::writeHeader(3, {wire::Type::Ready, run, -1, maxSlots}, deadline);
    if (mode == "ignore-close")
        for (;;) pause();
    if (mode == "finish" || mode == "fail-at-exit" || mode == "capacity")
    {
        try
        {
            wire::readHeader(3, deadline);
        }
        catch (const wire::EndOfStream&)
        {
            wire::writeHeader(3, {wire::Type::Finished, run, -1}, deadline);
            return mode == "fail-at-exit" ? 42 : 0;
        }
        throw std::runtime_error("expected final EOF");
    }
    auto request = wire::readHeader(3, deadline);
    require(request.run == run && request.event == 729 && request.type == wire::Type::Photons, "payload identity");
    if (mode == "hang-write")
        for (;;) pause();
    std::vector<sphoton> photons(request.count);
    wire::readAll(3, photons.data(), request.bytes, deadline);
    if (mode == "hang-request")
        for (;;) pause();
    auto reply = wire::Header(wire::Type::Hits, run, mode == "mismatch" ? 728 : 729, request.count + (mode == "excess-hits"));
    reply.start = 11;
    reply.end = 12;
    reply.cleanup = 13;
    wire::writeHeader(3, reply, deadline);
    if (mode != "excess-hits")
        wire::writeAll(3, photons.data(), reply.bytes, deadline);
    try
    {
        wire::readHeader(3, deadline);
    }
    catch (const wire::EndOfStream&)
    {
    }
    return 0;
}

GpuPoolOptions options(const char* self, const std::filesystem::path& dir, std::string mode)
{
    GpuPoolOptions opts;
    opts.executable = self;
    opts.output = dir;
    opts.geometry = dir;
    opts.devices = {{4, "GPU-fixture-A", "fixture", 0}};
    opts.config_file = std::move(mode);
    opts.timeout_seconds = 0.5;
    opts.run_id = 19;
    return opts;
}

GpuPayload payload(size_t count = 65536)
{
    GpuPayload data;
    data.event_id = 729;
    data.charged = false;
    data.photons.resize(count);
    for (size_t i = 0; i < count; ++i) data.photons[i].wavelength = float(i);
    return data;
}

pid_t childPid(const std::filesystem::path& root)
{
    for (auto const& entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.path().filename() == "pid")
        {
            pid_t pid = 0;
            std::ifstream(entry.path()) >> pid;
            require(pid > 0, "invalid fixture pid");
            return pid;
        }
    throw std::runtime_error("missing fixture pid");
}

void outputFailures(const std::filesystem::path& root)
{
    const auto          arrayPath = root / "data.npy";
    const auto          metaPath = root / "data_meta.txt";
    std::unique_ptr<NP> value(NP::Make<float>(8));
    std::filesystem::create_symlink("/dev/full", arrayPath);
    require(rejects([&] { value->save(arrayPath.c_str()); }).find(arrayPath.string()) != std::string::npos,
            "buffered array write failure lost its path");
    std::filesystem::remove(arrayPath);
    std::filesystem::create_symlink("/dev/full", metaPath);
    value->set_meta<int>("run", 1);
    require(rejects([&] { value->save(arrayPath.c_str()); }).find(metaPath.string()) != std::string::npos,
            "buffered sidecar write failure lost its path");
}
void lifecycle(const char* self, const std::filesystem::path& root)
{
    auto tooLong = options(self, root / "overflow", "finish");
    tooLong.timeout_seconds = 1e20;
    require(rejects([&] { GpuEventPool pool(tooLong); }).find("timeout") != std::string::npos,
            "unrepresentable timeout enabled an infinite wait");
    for (const auto mode : {"echo", "mismatch", "excess-hits", "hang-request", "hang-write", "kill", "ignore-close", "hang-start"})
    {
        auto       opts = options(self, root / mode, mode);
        const auto begin = wire::Clock::now();
        if (opts.config_file == "hang-start")
            rejects([&] { GpuEventPool pool(opts); });
        else
        {
            GpuEventPool pool(opts);
            auto         lease = pool.Acquire(payload());
            require(lease.device().ordinal == 4, "device identity");
            if (opts.config_file == "echo")
            {
                auto reply = lease.Simulate();
                require(reply.hits.size() == 65536 && reply.service_start_ns == 11 && reply.cleanup_end_ns == 13,
                        "partial payload and result");
                for (size_t i = 0; i < reply.hits.size(); ++i)
                    require(reply.hits[i].wavelength == float(i), "chunked transfer changed a photon or its order");
            }
            else if (opts.config_file != "ignore-close")
            {
                auto blocked = std::async(std::launch::async, [&] { rejects([&] { pool.Acquire(payload(1)); }); });
                require(blocked.wait_for(10ms) == std::future_status::timeout, "second lease must wait");
                if (opts.config_file == "kill")
                    require(kill(childPid(opts.output), SIGKILL) == 0, "failed to kill fixture");
                const auto cause = rejects([&] { lease.Simulate(); });
                if (opts.config_file == "excess-hits")
                    require(cause.find("hit count") != std::string::npos, "invalid hit count reached receive allocation");
                require(blocked.wait_for(1s) == std::future_status::ready, "failure must wake waiting worker");
                blocked.get();
            }
        }
        require(wire::Clock::now() - begin < 4s, "startup/request/shutdown exceeded bound");
        errno = 0;
        require(waitpid(childPid(opts.output), nullptr, WNOHANG) == -1 && errno == ECHILD, "helper was not reaped");
    }
}
void cancelsOtherDevices(const char* self, const std::filesystem::path& root)
{
    auto opts = options(self, root, "hang-request");
    opts.devices.push_back({8, "GPU-fixture-B", "fixture", 0});
    opts.timeout_seconds = 10;
    GpuEventPool pool(opts);
    auto         first = pool.Acquire(payload());
    auto         second = pool.Acquire(payload(1));
    require(first.device().ordinal != second.device().ordinal, "simultaneous leases must belong to distinct devices");
    auto inflight = std::async(std::launch::async, [&] { rejects([&] { first.Simulate(); }); });
    auto queued = std::async(std::launch::async, [&] { rejects([&] { pool.Acquire(payload(1)); }); });
    require(inflight.wait_for(30ms) == std::future_status::timeout && queued.wait_for(10ms) == std::future_status::timeout,
            "workers must be waiting");
    const auto secondDir = opts.output / ("gpu-" + std::to_string(second.device().ordinal));
    require(kill(childPid(secondDir), SIGKILL) == 0, "kill helper with idle lease");
    require(inflight.wait_for(1s) == std::future_status::ready && queued.wait_for(1s) == std::future_status::ready,
            "a lost peer must cancel both another device's I/O and a queued lease before their deadline");
    inflight.get();
    queued.get();
}
void capacityRouting(const char* self, const std::filesystem::path& root)
{
    auto opts = options(self, root, "capacity");
    opts.devices.push_back({8, "GPU-fixture-B", "fixture", 0});
    GpuEventPool     pool(opts);
    std::future<int> waiting;
    GpuPayload       charged{729, true, std::vector<quad6>(3), {}};
    charged.gensteps[0].set_numphoton(1);
    charged.gensteps[1].set_numphoton(4);
    charged.gensteps[2].set_numphoton(4);
    {
        auto large = pool.Acquire(payload(8));
        require(large.device().ordinal == 8, "torch input must fit in a single genstep");
        waiting = std::async(std::launch::async, [&] { return pool.Acquire(payload(5)).device().ordinal; });
        require(waiting.wait_for(30ms) == std::future_status::timeout, "large input took an incompatible idle device");
        require(pool.Acquire(charged).device().ordinal == 4, "charged event could not span multiple launches");
    }
    require(waiting.get() == 8, "compatible device release did not wake a queued event");
    charged.gensteps[1].set_numphoton(5);
    require(pool.Acquire(charged).device().ordinal == 8, "largest charged genstep did not fit assigned device");
    std::string cause;
    {
        auto large = pool.Acquire(payload(8));
        auto queued = std::async(std::launch::async, [&] { return rejects([&] { pool.Acquire(payload(8)); }); });
        require(queued.wait_for(30ms) == std::future_status::timeout, "second large input must wait");
        cause = rejects([&] { pool.Acquire(payload(9)); });
        require(cause.find("729") != std::string::npos && cause.find("9 slots") != std::string::npos,
                "unschedulable event lost its identity or required capacity");
        require(queued.wait_for(1s) == std::future_status::ready && queued.get() == cause,
                "unschedulable event did not cancel the queued lease");
    }
    require(rejects([&] { pool.Finish(); }) == cause, "capacity failure was lost during finalization");
}
void finalization(const char* self, const std::filesystem::path& root)
{
    for (const auto mode : {"fail-at-exit", "killed-before-finish", "mismatch", "ignore-close", "finish"})
    {
        const auto   opts = options(self, root / mode, mode);
        GpuEventPool pool(opts);
        if (opts.config_file == "killed-before-finish")
        {
            const auto pid = childPid(opts.output);
            require(kill(pid, SIGKILL) == 0, "kill idle worker");
            siginfo_t info{};
            require(waitid(P_PID, pid, &info, WEXITED | WNOWAIT) == 0 && info.si_status == SIGKILL, "idle worker must have died before Finish");
        }
        std::string earlierFailure;
        if (opts.config_file == "mismatch")
        {
            auto lease = pool.Acquire(payload());
            earlierFailure = rejects([&] { lease.Simulate(); });
        }
        if (opts.config_file == "finish")
        {
            pool.Finish();
            pool.Finish();
            rejects([&] { pool.Acquire(payload(1)); });
        }
        else
        {
            const auto begin = wire::Clock::now();
            const auto cause = rejects([&] { pool.Finish(); });
            require(wire::Clock::now() - begin < 3s, "checked shutdown exceeded its deadline and termination grace");
            require(rejects([&] { pool.Finish(); }) == cause, "Finish lost the original failure");
            if (opts.config_file == "fail-at-exit")
                require(cause.find("42") != std::string::npos, "Finish lost the nonzero exit status");
            if (!earlierFailure.empty())
                require(cause == earlierFailure, "Finish lost the in-flight failure");
        }
        errno = 0;
        require(waitpid(childPid(opts.output), nullptr, WNOHANG) == -1 && errno == ECHILD, "Finish left child ownership outstanding");
    }
}

int realFinalization(const char* worker, const char* geometry)
{
    const auto devices = DiscoverGpuDevices();
    if (devices.empty())
        return 77;
    const auto root = std::filesystem::current_path() / ("gpu-finalization-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto config = root / "finalization.json";
    std::ofstream(config) << R"({"event_timing":{"profile":{"enabled":true,"output":"profile.csv"}}})";
    auto opts = options(worker, root, config.string());
    opts.devices = {devices.front()};
    opts.geometry = geometry;
    opts.timeout_seconds = 60;
    GpuEventPool pool(opts);
    const auto   output = root / ("gpu-" + std::to_string(devices.front().ordinal)) / "profile.csv";
    std::filesystem::create_directory(output);
    const auto cause = rejects([&] { pool.Finish(); });
    require(cause.find("Unable to replace EventTiming profile") != std::string::npos &&
                cause.find("profile.csv") != std::string::npos,
            "Finish lost the helper profile write error");
    std::filesystem::remove_all(root);
    return 0;
}
// Exercise the real helper while it cannot observe EOF or respond to SIGTERM.
// The subreaper lets this test prove that the orphan exited and clean it up on failure.
int realWorker(const char* worker, const char* geometry, const char* config)
{
    require(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0, "set subreaper");
    int ready[2];
    require(pipe2(ready, O_CLOEXEC) == 0, "create readiness pipe");
    const pid_t owner = fork(); // Neither this test process nor its parent has initialized CUDA.
    require(owner >= 0, "fork test owner");
    if (owner == 0)
    {
        close(ready[0]);
        try
        {
            auto opts = options(worker, std::filesystem::current_path() / "gpu-smoke", config);
            opts.geometry = geometry;
            auto devices = DiscoverGpuDevices();
            if (devices.empty())
                _exit(77);
            opts.devices = {devices.front()};
            opts.timeout_seconds = 90;
            GpuEventPool pool(opts);
            pid_t        helper = -1;
            std::ifstream("/proc/thread-self/children") >> helper;
            require(helper > 0, "cannot identify the ready helper");
            require(write(ready[1], &helper, sizeof(helper)) == sizeof(helper), "signal helper readiness");
            for (;;) pause();
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << std::endl;
            _exit(1);
        }
    }
    close(ready[1]);
    pid_t   helper = -1;
    ssize_t bytes = read(ready[0], &helper, sizeof(helper));
    close(ready[0]);
    if (bytes != sizeof(helper))
    {
        int status;
        waitpid(owner, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 77)
            return 77;
        throw std::runtime_error("real helper startup failed");
    }
    require(kill(helper, SIGSTOP) == 0, "stop real worker");
    require(kill(owner, SIGKILL) == 0 && waitpid(owner, nullptr, 0) == owner, "kill/reap test owner");
    const auto deadline = wire::Clock::now() + 3s;
    while (wire::Clock::now() < deadline)
    {
        int status = 0;
        if (waitpid(helper, &status, WNOHANG) == helper)
        {
            require(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "real helper did not receive parent-death signal");
            return 0;
        }
        std::this_thread::sleep_for(10ms);
    }
    kill(helper, SIGKILL);
    waitpid(helper, nullptr, 0);
    throw std::runtime_error("real GPU helper survived its parent");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 8 && std::string(argv[1]) == simphony::GpuWorkerFlag)
            return fixture(argv);
        if (argc == 4 && std::string(argv[1]) == "--finalization")
            return realFinalization(argv[2], argv[3]);
        if (argc == 5 && std::string(argv[1]) == "--parent-death")
            return realWorker(argv[2], argv[3], argv[4]);
        if (argc == 5 && std::string(argv[1]) == "--startup")
        {
            auto opts = options(argv[2], std::filesystem::current_path() / "gpu-smoke", argv[4]);
            opts.geometry = argv[3];
            opts.devices = {DiscoverGpuDevices().at(0)};
            opts.timeout_seconds = 60;
            GpuEventPool pool(opts);
            pool.Finish();
            return 0;
        }
        deviceSelectors();
        protocol();
        auto root = std::filesystem::temp_directory_path() / ("simphony-protocol-" + std::to_string(getpid()));
        std::filesystem::create_directory(root);
        const auto self = std::filesystem::canonical("/proc/self/exe");
        outputFailures(root);
        finalization(self.c_str(), root / "finish");
        lifecycle(self.c_str(), root);
        capacityRouting(self.c_str(), root / "capacity");
        cancelsOtherDevices(self.c_str(), root / "cancel-peers");
        std::filesystem::remove_all(root);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
