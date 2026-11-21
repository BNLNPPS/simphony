#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "scuda.h"
#include "sphoton.h"
#include "squad.h"

namespace simphony
{
struct GpuDevice
{
    int         ordinal;
    std::string uuid, name;
    uint64_t    memory_bytes;
};

std::vector<GpuDevice> DiscoverGpuDevices();

struct GpuPayload
{
    int                  event_id;
    bool                 charged;
    std::vector<quad6>   gensteps;
    std::vector<sphoton> photons;
};

struct GpuResult
{
    std::vector<sphoton> hits;
    int64_t              service_start_ns, service_end_ns, cleanup_end_ns;
};

struct GpuPoolOptions
{
    std::vector<GpuDevice> devices;
    std::filesystem::path  executable, geometry, output;
    std::filesystem::path  config_file;
    double                 timeout_seconds{3600};
    int                    run_id{0};
};

// Construct one pool per run. All leases and their operations must finish before destruction.
class GpuEventPool
{
    struct State;
    std::unique_ptr<State> state_;

  public:
    class Lease
    {
        friend class GpuEventPool;
        State*     state_;
        size_t     index_;
        GpuPayload payload_;
        Lease(State* state, size_t index, GpuPayload&& payload);

      public:
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        ~Lease();
        const GpuDevice& device() const;
        GpuResult Simulate();
    };
    explicit GpuEventPool(GpuPoolOptions options);
    ~GpuEventPool();
    Lease Acquire(GpuPayload payload);
    // Call on the run master after every worker has released its lease, before publishing results.
    // Repeated calls return the same success or throw the saved failure. Acquire is closed afterward.
    void Finish();
};
} // namespace simphony
