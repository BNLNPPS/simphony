#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "torch.h"

namespace gphox
{

enum class EventMode
{
    DebugHeavy,
    DebugLite,
    Nothing,
    Minimal,
    Hit,
    HitPhoton,
    HitPhotonSeq,
    HitSeq
};

/**
 * Provides access to all configuration types and data.
 *
 * Config is the authoritative source for app-level event output policy.
 * Lower-level Opticks code still consumes this through SEventConfig after
 * Config::Apply has synchronized the selected values.
 */
class Config
{
  public:
    Config(std::string config_name = "dev");

    static std::string PtxPath(const std::string& ptx_name = "CSGOptiX7.ptx");

    /// A unique name associated with this Config
    std::string name{"dev"};

    /// Event persistence mode applied to SEventConfig.
    EventMode event_mode{EventMode::Minimal};

    /// Maximum event slots applied to SEventConfig.
    int maxslot{0};

    /// Maximum photon bounce count.
    int max_bounce{31};

    /// Maximum gensteps allocated for event uploads.
    int max_genstep{10000000};

    /// Ray offset after boundary crossing.
    float propagate_epsilon{0.05f};

    /// Ray offset after bulk interaction.
    float propagate_epsilon0{0.05f};

    /// Flag mask selecting which bulk interactions use propagate_epsilon0.
    std::string propagate_epsilon0_mask{"TO,CK,SI,SC,RE"};

    /// Base directory for event output folders.
    std::filesystem::path output_dir{std::filesystem::current_path()};

    storch torch{default_torch};

  private:
    std::string Locate(std::string filename) const;
    void ReadConfig(std::string filepath);
    void Apply() const;
};

} // namespace gphox
