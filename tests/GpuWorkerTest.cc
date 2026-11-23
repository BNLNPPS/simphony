/**
 * @file GpuWorkerTest.cc
 * @brief Verifies that two GPU workers independently retain and reset optical events.
 *
 * A pair of simultaneous launches must reproduce each device's single-worker
 * reference photons byte for byte. GPU 0 retains its event while GPU 1 shuts
 * down and reattaches on the same thread, then launches again after GPU 1 exits.
 * CTest skips this case when fewer than two CUDA devices are visible.
 */
#include <cstring>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <vector>
#include <stdexcept>

#include <cuda_runtime.h>
#include "G4GDMLParser.hh"
#include "NP.hh"
#include "QEvt.hh"
#include "SEventConfig.hh"
#include "SEvt.hh"
#include "simphony.h"
#include "storch.h"

namespace
{
void prepare(int count)
{
    quad6 step{};
    std::memcpy(&step, &SEventConfig::Torch(), sizeof(step));
    step.q0.i.w = count;
    SEvt::Get_EGPU()->addGenstep(step);
}

std::vector<char> result(int ordinal, int event, int count)
{
    const auto* qevt = QEvt::Get();
    if (!qevt || SEventConfig::DeviceIndex() != ordinal)
        throw std::runtime_error("lost worker device or event");
    std::unique_ptr<NP> photons(qevt->gatherPhoton());
    if (!photons || photons->shape.at(0) != count)
        throw std::runtime_error("unexpected retained photon count");
    const auto* tracks = reinterpret_cast<const sphoton*>(photons->cvalues<float>());
    for (int i = 0; i < count; ++i)
        if (!(tracks[i].flagmask & TORCH)) throw std::runtime_error("downloaded photon was not simulated");
    simphony::reset(event);
    return std::move(photons->data);
}

std::vector<char> reference(int ordinal, int event, int count)
{
    simphony::initialize_gpu_worker(ordinal);
    prepare(count);
    simphony::simulate(event);
    auto photons = result(ordinal, event, count);
    simphony::shutdown_gpu_worker();
    return photons;
}

void concurrent_launch(int ordinal, int event, int count, std::latch& ready)
{
    try
    {
        simphony::initialize_gpu_worker(ordinal);
        prepare(count);
    }
    catch (...)
    {
        ready.count_down(); // Wake the other worker even if initialization failed.
        throw;
    }
    ready.arrive_and_wait();
    simphony::simulate(event);
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    int devices = 0;
    const auto status = cudaGetDeviceCount(&devices);
    if (status == cudaErrorNoDevice || status == cudaErrorInsufficientDriver || (status == cudaSuccess && devices < 2))
        return 77;
    if (status != cudaSuccess) return 1;

    try
    {
        SEventConfig::SetMaxGenstep(8);
        SEventConfig::SetMaxSlot(64);
        SEventConfig::SetMaxPhoton(64);
        G4GDMLParser parser;
        parser.Read(argv[1], false);
        simphony::initialize(parser.GetWorldVolume(), {.gpu = simphony::GpuRequirement::Disabled});

        const auto first = reference(0, 0, 17);
        const auto second = reference(1, 1, 31);

        std::async(std::launch::async, [&] {
            std::latch ready(2);
            auto peer = std::async(std::launch::async, [&] {
                concurrent_launch(1, 1, 31, ready);
                if (result(1, 1, 31) != second) throw std::runtime_error("GPU 1 concurrent photons differ");
                simphony::shutdown_gpu_worker();
                if (reference(1, 1, 31) != second) throw std::runtime_error("GPU 1 reattached photons differ");
            });

            concurrent_launch(0, 0, 17, ready);
            peer.get();
            if (result(0, 0, 17) != first) throw std::runtime_error("GPU 0 retained photons differ");
            prepare(17);
            simphony::simulate(0);
            if (result(0, 0, 17) != first) throw std::runtime_error("GPU 0 photons differ after peer exit");
            simphony::shutdown_gpu_worker();
        }).get();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
