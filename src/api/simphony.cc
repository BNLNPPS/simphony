#include "simphony.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include "CSGFoundry.h"
#include "CSGOptiX.h"
#include "SEventConfig.hh"
#include "SLOG.hh"
#include "SSim.hh"
#include "U4GDML.h"
#include "U4Tree.h"

namespace
{
struct Runtime
{
    std::mutex mutex;
    std::atomic<bool> initialized{false};
    std::atomic<bool> gpu_available{false};
    const G4VPhysicalVolume* world = nullptr;
    simphony::GeometryOptions options;
    U4Tree* tree = nullptr;
    CSGFoundry* foundry = nullptr;
    CSGOptiX* backend = nullptr;
    std::optional<int> pending_event;
};

Runtime& runtime()
{
    static Runtime instance;
    return instance;
}

bool same_options(
    const simphony::GeometryOptions& lhs,
    const simphony::GeometryOptions& rhs) noexcept
{
    return lhs.sensor_identifier == rhs.sensor_identifier && lhs.gpu == rhs.gpu;
}

void require_initialized(const Runtime& state)
{
    if (!state.initialized.load(std::memory_order_acquire))
        throw std::logic_error("simphony is not initialized");
}

void require_gpu(const Runtime& state)
{
    require_initialized(state);
    if (state.backend == nullptr)
        throw std::logic_error("simphony has no GPU backend");
}
}

namespace simphony
{
void initialize(const G4VPhysicalVolume* world, const GeometryOptions& options)
{
    if (world == nullptr)
        throw std::invalid_argument("simphony::initialize requires a non-null Geant4 world");

    Runtime& state = runtime();
    std::lock_guard lock(state.mutex);

    if (state.initialized.load(std::memory_order_acquire))
    {
        if (state.world == world && same_options(state.options, options)) return;
        throw std::logic_error("simphony is already initialized with different geometry or options");
    }

    bool create_gpu = false;
    if (options.gpu != GpuRequirement::Disabled)
    {
        if (SEventConfig::CONTEXT == nullptr) SEventConfig::Initialize();
        create_gpu = SEventConfig::HasDevice();
        if (options.gpu == GpuRequirement::Required && !create_gpu)
            throw std::runtime_error("simphony requires a CUDA-capable GPU, but none is available");
    }

    SSim* simulation = SSim::CreateOrReuse();
    if (simulation == nullptr)
        throw std::runtime_error("simphony failed to create its simulation state");

    U4Tree* tree = U4Tree::Create(
        simulation->get_tree(), world, options.sensor_identifier);
    if (tree == nullptr)
        throw std::runtime_error("simphony failed to translate the Geant4 geometry");

    simulation->initSceneFromTree();
    CSGFoundry* foundry = CSGFoundry::CreateFromSim();
    if (foundry == nullptr)
        throw std::runtime_error("simphony failed to create its CSG geometry");

    CSGOptiX* backend = create_gpu ? CSGOptiX::Create(foundry) : nullptr;
    if (create_gpu && backend == nullptr)
        throw std::runtime_error("simphony failed to create its GPU backend");

    state.world = world;
    state.options = options;
    state.tree = tree;
    state.foundry = foundry;
    state.backend = backend;
    state.gpu_available.store(backend != nullptr, std::memory_order_release);
    state.initialized.store(true, std::memory_order_release);
}

bool is_initialized() noexcept
{
    return runtime().initialized.load(std::memory_order_acquire);
}

bool has_gpu() noexcept
{
    return runtime().gpu_available.load(std::memory_order_acquire);
}

void simulate(int event_id, bool reset_after)
{
    Runtime& state = runtime();
    std::lock_guard lock(state.mutex);
    require_gpu(state);

    if (state.pending_event.has_value())
        throw std::logic_error("simphony has an event awaiting reset");

    state.backend->simulate(event_id, reset_after);
    if (!reset_after) state.pending_event = event_id;
}

void reset(int event_id)
{
    Runtime& state = runtime();
    std::lock_guard lock(state.mutex);
    require_gpu(state);

    if (!state.pending_event.has_value())
        throw std::logic_error("simphony has no event awaiting reset");
    if (*state.pending_event != event_id)
        throw std::logic_error("simphony reset event id does not match the pending event");

    state.backend->reset(event_id);
    state.pending_event.reset();
}

void save_geometry(const std::filesystem::path& directory)
{
    if (directory.empty())
        throw std::invalid_argument("simphony::save_geometry requires a destination directory");

    Runtime& state = runtime();
    std::lock_guard lock(state.mutex);
    require_initialized(state);

    const std::string base = directory.string();
    U4GDML::Write(state.world, base.c_str(), "origin.gdml");
    state.foundry->save(base.c_str());
}
}
