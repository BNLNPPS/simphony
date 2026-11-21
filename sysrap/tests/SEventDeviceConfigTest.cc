/**
 * @file SEventDeviceConfigTest.cc
 * @brief Verifies that each CUDA device has its own photon-slot limit.
 *
 * Two host threads select different GPUs and check that each new event uses
 * its device's limit. After both threads exit, the main thread switches
 * between the GPUs and reads back each override. It then checks that clearing
 * an override restores the job setting and that automatic sizing uses the
 * selected GPU's memory.
 *
 * The single-device case checks the same settings with one worker. With all
 * GPUs hidden, the test checks that the job limit remains available and that
 * attempting to set a device override raises an exception.
 */

#include "CUDA_CHECK.h"
#include "NPFold.h"
#include "SEventConfig.hh"
#include "SEvt.hh"
#include "scuda.h"
#include "squad.h"

#include "sphoton.h"
#include "srec.h"

#include "sevent.h"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <future>
#include <iostream>
#include <latch>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace
{
constexpr int64_t optixLimit = int64_t{1} << 30;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int64_t automaticLimit(int device)
{
    cudaDeviceProp properties{};
    CUDA_CHECK(cudaGetDeviceProperties(&properties, device));
    const size_t raw = SEventConfig::HeuristicMaxSlot(properties.totalGlobalMem, SEventConfig::ModeLite(), SEventConfig::ModeMerge());
    return std::min<int64_t>((raw / SEventConfig::M) * SEventConfig::M, optixLimit);
}

void expectDevice(int device, int64_t limit)
{
    require(SEventConfig::DeviceIndex() == device, "wrong CUDA-visible device");
    require(SEventConfig::MaxSlot() == limit, "wrong effective launch limit");
    sevent event{};
    event.init();
    require(event.max_slot == static_cast<size_t>(limit), "event initialized with the wrong launch limit");
    cudaDeviceProp properties{};
    CUDA_CHECK(cudaGetDeviceProperties(&properties, device));
    require(SEventConfig::HasDevice() && properties.name == std::string(SEventConfig::DeviceName()), "wrong device name");
}
} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "";
    constexpr int          jobLimit = 3000;
    constexpr size_t       gib = size_t{1} << 30;
    require(SEventConfig::HeuristicMaxSlot_Rounded(24 * gib, 1, 0) == 784000000, "wrong automatic limit below the OptiX ceiling");
    require(SEventConfig::HeuristicMaxSlot_Rounded(48 * gib, 1, 0) == optixLimit, "automatic limit exceeds the OptiX ceiling");
    SEventConfig::SetModeLite(1);
    SEventConfig::SetMaxSlot(jobLimit);
    require(SEventConfig::DeviceIndex() == -1 && SEventConfig::MaxSlot() == jobLimit, "wrong pre-initialization configuration");
    try
    {
        SEventConfig::SetMaxSlot(-1);
        throw std::runtime_error("accepted a negative job limit");
    }
    catch (const std::invalid_argument&)
    {
        require(SEventConfig::MaxSlot() == jobLimit, "invalid job limit changed the active setting");
    }
    if (mode == "--no-device")
    {
        SEventConfig::Initialize();
        require(SEventConfig::DeviceIndex() == -1 && !SEventConfig::HasDevice(), "expected all CUDA devices to be hidden");
        require(SEventConfig::DeviceName() == nullptr && SEventConfig::MaxSlot() == jobLimit, "wrong device-free configuration");
        try
        {
            SEventConfig::SetDeviceMaxSlot(123);
        }
        catch (const std::logic_error&)
        {
            return 0;
        }
        throw std::runtime_error("accepted a device override without a CUDA device");
    }

    const int numDevices = mode == "--single-device" ? 1 : 2;
    if (numDevices == 2)
        SEventConfig::SetMaxSlot(0);
    int  count = 0;
    auto status = cudaGetDeviceCount(&count);
    if (status == cudaErrorNoDevice || status == cudaErrorInsufficientDriver || (status == cudaSuccess && count < numDevices))
        return 77;
    CUDA_CHECK(status);

    // Each device inherits the job setting until its worker overrides it.
    constexpr std::array<int, 2>     limits{1000, 2000};
    std::latch                       start(1);
    std::array<std::future<void>, 2> workers;
    for (int device = 0; device < numDevices; ++device)
        workers[device] = std::async(std::launch::async, [&, device] {
            start.wait();
            CUDA_CHECK(cudaSetDevice(device));
            SEventConfig::Initialize();
            expectDevice(device, numDevices == 2 ? automaticLimit(device) : jobLimit);
            SEventConfig::SetDeviceMaxSlot(limits[device]);
            expectDevice(device, limits[device]);
        });
    start.count_down();
    for (int device = 0; device < numDevices; ++device) workers[device].get();

    SEventConfig::SetMaxSlot(jobLimit);
    // A different thread sees the limit owned by each device, including after a switch.
    for (int device = 0; device < numDevices; ++device)
    {
        CUDA_CHECK(cudaSetDevice(device));
        expectDevice(device, limits[device]);
        std::cout << "CUDA device " << device << " (" << SEventConfig::DeviceName() << "), max slots " << SEventConfig::MaxSlot() << '\n';
        SEventConfig::SetDeviceMaxSlot(0);
        expectDevice(device, jobLimit);
    }

    SEventConfig::SetMaxSlot(0);
    for (int device = 0; device < numDevices; ++device)
    {
        CUDA_CHECK(cudaSetDevice(device));
        expectDevice(device, automaticLimit(device));
    }

    SEventConfig::SetMaxSlot(std::numeric_limits<int>::max());
    expectDevice(numDevices - 1, optixLimit);
    SEventConfig::SetMaxSlot(jobLimit);
    SEvt* event = SEvt::Create_EGPU();
    SEventConfig::SetDeviceMaxSlot(std::numeric_limits<int>::max());
    expectDevice(numDevices - 1, optixLimit);
    event->addEventConfigArray();
    const NP* eventConfig = event->extrafold->get(SEventConfig::NAME);
    require(eventConfig && eventConfig->get_meta<int64_t>("MaxSlot", -1) == jobLimit, "event metadata lost its captured launch limit");
    const std::unique_ptr<NP> currentConfig(SEventConfig::Serialize());
    require(currentConfig->get_meta<int64_t>("MaxSlot", -1) == optixLimit, "standalone metadata lost the current device limit");
    SEventConfig::SetDeviceMaxSlot(0);
    expectDevice(numDevices - 1, jobLimit);
}
