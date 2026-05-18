#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "sysrap/srng.h"
#include "sysrap/storch.h"

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
    std::string name;

    /// Event persistence mode applied to SEventConfig.
    EventMode event_mode;

    /// Maximum event slots applied to SEventConfig.
    int maxslot;

    /// Base directory for event output folders.
    std::filesystem::path output_dir;

    storch torch;

  private:
    std::string Locate(std::string filename) const;
    void ReadConfig(std::string filepath);
    void Apply() const;
};

} // namespace gphox
