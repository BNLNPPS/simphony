#include "GpuEventPool.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <cuda_runtime_api.h>

#include "G4ios.hh"
#include "simphony.h"
#include "sysrap/NP.hh"
#include "sysrap/SEventConfig.hh"
#include "sysrap/SEvt.hh"

namespace
{
void checkCuda(cudaError_t result)
{
    if (result != cudaSuccess)
        throw std::runtime_error(cudaGetErrorString(result));
}

std::string deviceUuid(int ordinal)
{
    cudaDeviceProp properties{};
    checkCuda(cudaGetDeviceProperties(&properties, ordinal));
    std::ostringstream uuid;
    uuid << (std::strstr(properties.name, "MIG") ? "MIG-" : "GPU-") << std::hex << std::setfill('0');
    for (size_t i = 0; i < sizeof(properties.uuid.bytes); ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            uuid << '-';
        uuid << std::setw(2) << static_cast<unsigned>(static_cast<unsigned char>(properties.uuid.bytes[i]));
    }
    return uuid.str();
}

size_t requiredSlots(const simphony::EventBuffer& buffer)
{
    if (const auto* photons = std::get_if<std::vector<sphoton>>(&buffer.records))
        return photons->size();
    // Transport can slice between gensteps, but a single genstep must fit.
    size_t largest = 0;
    for (const auto& step : std::get<std::vector<quad6>>(buffer.records))
        largest = std::max(largest, static_cast<size_t>(step.numphoton()));
    return largest;
}

} // namespace

namespace simphony
{
GpuEventResult SimulateGpuEvent(int event_id, const EventBuffer* buffer, std::unique_ptr<NP>& input,
                                EventTimingRecorder* timing)
{
    SEvt* evt = SEvt::Get_EGPU();
    if (buffer)
    {
        if (const auto* photons = std::get_if<std::vector<sphoton>>(&buffer->records))
        {
            const int count = static_cast<int>(photons->size());
            if (!input || input->shape[0] != count)
                input.reset(NP::Make<float>(count, 4, 4));
            std::memcpy(input->bytes(), photons->data(), photons->size() * sizeof(sphoton));
            evt->setInputPhoton(input.get());
        }
        else
            for (const auto& step : std::get<std::vector<quad6>>(buffer->records))
                evt->addGenstep(step);
    }

    simphony::simulate(event_id, false);
    checkCuda(cudaDeviceSynchronize());
    if (timing)
        timing->EndGpu();

    GpuEventResult result{static_cast<size_t>(evt->getNumPhotonCollected()), std::vector<sphoton>(evt->getNumHit())};
    for (size_t i = 0; i < result.hits.size(); ++i)
        evt->getHit(result.hits[i], i);
    simphony::reset(event_id);
    return result;
}
GpuEventPool::GpuEventPool(int device_count) :
    devices_(device_count)
{
    if (!device_count)
        throw std::runtime_error("MT GPU dispatch requires a visible CUDA device");
    try
    {
        for (int i = 0; i < device_count; ++i)
            threads_.emplace_back(&GpuEventPool::Serve, this, i);
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return error_ || ready_ == devices_.size(); });
        if (error_)
            std::rethrow_exception(error_);
    }
    catch (...)
    {
        Stop();
        throw;
    }
}

GpuEventPool::~GpuEventPool()
{
    Stop();
}

void GpuEventPool::Stop()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    for (auto& thread : threads_)
        if (thread.joinable())
            thread.join();
}

void GpuEventPool::Fail(std::exception_ptr error)
{
    std::lock_guard lock(mutex_);
    if (!error_)
        error_ = error;
    changed_.notify_all();
}

void GpuEventPool::BeginRun(EventTimingRecorder* master)
{
    std::lock_guard lock(mutex_);
    if (error_)
        std::rethrow_exception(error_);
    if (master)
        for (auto& device : devices_)
        {
            device.timing.emplace(master->output());
            device.timing->BeginRunFrom(*master);
        }
}

void GpuEventPool::Submit(EventBuffer buffer, size_t g4_hits, EventTimingRecorder* producer)
{
    size_t           slot_count = requiredSlots(buffer);
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return error_ || pending_.size() < devices_.size(); });
    auto timing = producer ? std::optional(producer->DetachQueuedEvent()) : std::nullopt;
    if (error_)
        return; // The master propagates the original error before publishing any results.
    if (buffer.genstepCount() > std::numeric_limits<int>::max() || slot_count > std::numeric_limits<int>::max() ||
        std::none_of(devices_.begin(), devices_.end(), [&](const Device& d) { return slot_count <= d.max_slots; }))
    {
        error_ = std::make_exception_ptr(std::runtime_error(
            "Event " + std::to_string(buffer.event_id) + " requires a launch larger than any visible GPU supports"));
    }
    else
        pending_.push_back({std::move(buffer), g4_hits, slot_count, std::move(timing)});
    lock.unlock();
    changed_.notify_all();
}

std::vector<sphoton> GpuEventPool::FinishRun(EventTimingRecorder* master)
{
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return error_ || (pending_.empty() && !active_); });
    if (error_)
    {
        auto error = error_;
        lock.unlock();
        Stop();
        std::rethrow_exception(error);
    }
    if (master)
        for (auto& device : devices_)
            master->Merge(*device.timing);
    std::vector<sphoton> hits;
    size_t               count = 0;
    for (const auto& item : results_) count += item.second.hits.size();
    hits.reserve(count);
    for (const auto& [event, result] : results_)
    {
        G4cout << "EventAction::SimulateOnGPU: Event " << event << ": Simulated GPU photons: " << result.photons << G4endl;
        G4cout << "EventAction::EndOfEventAction: Event " << event << ": Collected GPU hits: " << result.hits.size() << G4endl;
        hits.insert(hits.end(), result.hits.begin(), result.hits.end());
    }
    results_.clear();
    return hits;
}

void GpuEventPool::Serve(int ordinal)
{
    std::unique_ptr<NP> input; // SEvt borrows this array until its worker context closes.
    bool                initialized = false;
    int                 event_id = -1;
    try
    {
        initialize_gpu_worker(ordinal);
        initialized = true;
        Device&    device = devices_[ordinal];
        const auto slot_count = SEventConfig::MaxSlot();
        if (slot_count <= 0)
            throw std::runtime_error("GPU worker initialized without launch capacity");
        device.uuid = deviceUuid(ordinal);
        std::unique_lock lock(mutex_);
        device.max_slots = static_cast<size_t>(slot_count);
        ++ready_;
        changed_.notify_all();
        while (!stopping_ && !error_)
        {
            auto next = std::find_if(pending_.begin(), pending_.end(),
                                     [&](const Job& job) { return job.required_slots <= device.max_slots; });
            if (next == pending_.end())
            {
                changed_.wait(lock);
                continue;
            }
            Job job = std::move(*next);
            event_id = job.buffer.event_id;
            pending_.erase(next);
            ++active_;
            lock.unlock();
            changed_.notify_all();

            auto* timing = job.timing ? &*device.timing : nullptr;
            if (timing)
                timing->BeginQueuedEvent(*job.timing, ordinal, device.uuid);
            auto result = SimulateGpuEvent(job.buffer.event_id, &job.buffer, input, timing);
            if (timing)
            {
                timing->ReleaseGpu();
                timing->EndEvent(result.hits.size(), job.g4_hits);
            }

            lock.lock();
            if (!results_.emplace(job.buffer.event_id, std::move(result)).second)
                throw std::logic_error("Duplicate GPU result for event " + std::to_string(job.buffer.event_id));
            event_id = -1;
            --active_;
            changed_.notify_all();
        }
    }
    catch (const std::exception& error)
    {
        std::string context = "GPU " + std::to_string(ordinal);
        if (event_id >= 0)
            context += " event " + std::to_string(event_id);
        Fail(std::make_exception_ptr(std::runtime_error(context + ": " + error.what())));
    }
    catch (...)
    {
        Fail(std::current_exception());
    }
    if (initialized)
    {
        try
        {
            shutdown_gpu_worker();
        }
        catch (...)
        {
            Fail(std::current_exception());
        }
    }
}
} // namespace simphony
