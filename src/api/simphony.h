#pragma once

#include <filesystem>

#include "SIMPHONY_API_EXPORT.hh"

class G4VPhysicalVolume;
struct U4SensorIdentifier;

namespace simphony
{
enum class GpuRequirement
{
    Required,
    Optional,
    Disabled,
};

struct GeometryOptions
{
    U4SensorIdentifier* sensor_identifier = nullptr;
    GpuRequirement gpu = GpuRequirement::Required;
};

SIMPHONY_API void initialize(
    const G4VPhysicalVolume* world,
    const GeometryOptions& options = {});

SIMPHONY_API bool is_initialized() noexcept;
SIMPHONY_API bool has_gpu() noexcept;

SIMPHONY_API void simulate(int event_id, bool reset = false);
SIMPHONY_API void reset(int event_id);

SIMPHONY_API void save_geometry(const std::filesystem::path& directory);
}
