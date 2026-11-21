#pragma once

#include <string>
#include <string_view>

namespace simphony::gpu_detail
{
// CUDA supplies UUID bytes without the GPU-/MIG- prefix required by CUDA_VISIBLE_DEVICES.
// Match explicit instance IDs by value: ordinals can be reordered and CUDA may expose
// only a subset of the compute instances named in the mask.
inline std::string DeviceSelector(std::string_view uuid, std::string_view mask, std::string_view physicalUuid, std::string_view deviceName)
{
    std::string instance = "MIG-" + std::string(uuid);
    for (;;)
    {
        const auto comma = mask.find(',');
        const auto token = mask.substr(0, comma);
        if (token.starts_with("MIG-") && token.size() > 4 && instance.starts_with(token))
            return instance;
        if (comma == std::string_view::npos)
            break;
        mask.remove_prefix(comma + 1);
    }
    std::string gpu = "GPU-" + std::string(uuid);
    if (physicalUuid.starts_with("GPU-"))
        return gpu == physicalUuid ? gpu : instance;
    // Compute-only containers may omit driver procfs. CUDA device names for a
    // partitioned device include its MIG profile, e.g. "NVIDIA A100 ... MIG 1g.5gb".
    return deviceName.find(" MIG ") == std::string_view::npos ? gpu : instance;
}
} // namespace simphony::gpu_detail
