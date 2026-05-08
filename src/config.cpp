#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

#include <nlohmann/json.hpp>
#include <cuda_runtime.h>

#include "sysrap/SEventConfig.hh"

#include "config.h"
#include "config_path.h"

namespace gphox {

using namespace std;

constexpr const char *GPHOX_PTX_PATH_ENV = "CSGOptiX__optixpath";

namespace
{

struct EventModeInfo
{
    EventMode        mode;
    std::string_view name;
};

inline constexpr std::array EventModeInfos{
    EventModeInfo{EventMode::DebugHeavy, "DebugHeavy"},
    EventModeInfo{EventMode::DebugLite, "DebugLite"},
    EventModeInfo{EventMode::Nothing, "Nothing"},
    EventModeInfo{EventMode::Minimal, "Minimal"},
    EventModeInfo{EventMode::Hit, "Hit"},
    EventModeInfo{EventMode::HitPhoton, "HitPhoton"},
    EventModeInfo{EventMode::HitPhotonSeq, "HitPhotonSeq"},
    EventModeInfo{EventMode::HitSeq, "HitSeq"},
};

auto FindEventMode(EventMode mode)
{
    return std::ranges::find(EventModeInfos, mode, &EventModeInfo::mode);
}

auto FindEventMode(std::string_view name)
{
    return std::ranges::find(EventModeInfos, name, &EventModeInfo::name);
}

} // namespace

bool FileExists(const std::string &path)
{
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

std::filesystem::path Config::DefaultOutputDir()
{
    return std::filesystem::current_path();
}

std::filesystem::path ReadOutputDir(const nlohmann::json& event)
{
    if (event.contains("output_dir"))
        return event["output_dir"].get<std::string>();

    return Config::DefaultOutputDir();
}

Config::Config(std::string config_name) :
    name{config_name},
    event_mode{EventMode::Minimal},
    maxslot{0},
    output_dir{DefaultOutputDir()},
    torch{}
{
  ReadConfig(Locate(name + ".json"));
  Apply();
}

std::string Config::PtxPath(const std::string &ptx_name)
{
    const char *env_path = std::getenv(GPHOX_PTX_PATH_ENV);
    if (env_path && FileExists(env_path))
        return env_path;

    std::string default_path = std::string(GPHOX_PTX_DIR) + "/" + ptx_name;
    if (FileExists(default_path))
        return default_path;

    std::stringstream errmsg;
    errmsg << "Could not resolve PTX file \"" << ptx_name << "\".\n"
           << "Expected one of:\n"
           << "  - " << GPHOX_PTX_PATH_ENV << "=<path-to-ptx>\n"
           << "  - " << default_path;
    throw std::runtime_error(errmsg.str());
}

std::string Config::Locate(std::string filename) const
{
  std::vector<std::string> search_paths;

  const std::string user_dir{std::getenv("GPHOX_CONFIG_DIR") ? std::getenv("GPHOX_CONFIG_DIR") : ""};

  if (user_dir.empty())
  {
    std::string paths(GPHOX_CONFIG_SEARCH_PATHS);

    size_t last = 0;
    size_t next = 0;
    while ((next = paths.find(':', last)) != std::string::npos)
    {
      search_paths.push_back(paths.substr(last, next-last));
      last = next + 1;
    }

    search_paths.push_back(paths.substr(last));
  }
  else
  {
    search_paths.push_back(user_dir);
  }

  struct stat buffer;
  std::string filepath{""};
  for (std::string path : search_paths)
  {
    std::string fpath{path + "/" + filename};
    if (stat(fpath.c_str(), &buffer) == 0)
    {
      filepath = fpath;
      break;
    }
  }

  if (filepath.empty())
  {
    std::string errmsg{"Could not find config file \"" + filename + "\" in "};
    for (std::string path : search_paths) errmsg += (path + ":");
    throw std::runtime_error(errmsg);
  }

  return filepath;
}

EventMode Config::ParseEventMode(std::string_view name)
{
    const auto it = FindEventMode(name);
    if (it != EventModeInfos.end())
        return it->mode;

    throw std::invalid_argument(
        "Invalid event.mode \"" + std::string{name} + "\". Expected one of: " + ValidEventModes());
}

std::string Config::ValidEventModes()
{
    std::string names;
    for (const auto& info : EventModeInfos)
    {
        if (!names.empty())
            names += ", ";
        names += info.name;
    }
    return names;
}

std::string_view Config::EventModeName(EventMode mode)
{
    const auto it = FindEventMode(mode);
    if (it != EventModeInfos.end())
        return it->name;

    return "Minimal";
}

/**
 * Expects a valid filepath.
 */
void Config::ReadConfig(std::string filepath)
{
  nlohmann::json json;

  try {
    std::ifstream ifs(filepath);
    ifs >> json;

    if (json.contains("torch"))
    {
        nlohmann::json torch_ = json["torch"];

        torch = {
            .gentype = OpticksGenstep_::Type(torch_["gentype"]),
            .trackid = torch_["trackid"],
            .matline = torch_["matline"],
            .numphoton = torch_["numphoton"],
            .pos = make_float3(torch_["pos"][0], torch_["pos"][1], torch_["pos"][2]),
            .time = torch_["time"],
            .mom = normalize(make_float3(torch_["mom"][0], torch_["mom"][1], torch_["mom"][2])),
            .weight = torch_["weight"],
            .pol = make_float3(torch_["pol"][0], torch_["pol"][1], torch_["pol"][2]),
            .wavelength = torch_["wavelength"],
            .zenith = make_float2(torch_["zenith"][0], torch_["zenith"][1]),
            .azimuth = make_float2(torch_["azimuth"][0], torch_["azimuth"][1]),
            .radius = torch_["radius"],
            .distance = torch_["distance"],
            .mode = torch_["mode"],
            .type = storchtype::Type(torch_["type"])};
    }

    nlohmann::json event_ = json["event"];

    event_mode = ParseEventMode(event_["mode"].get<std::string>());
    maxslot = event_["maxslot"].get<int>();
    output_dir = ReadOutputDir(event_);
  }
  catch (nlohmann::json::exception& e)
  {
      throw std::runtime_error{"Failed reading config parameters from " + filepath + "\n" + e.what()};
  }
  catch (std::invalid_argument& e)
  {
      throw std::runtime_error{"Invalid config value in " + filepath + "\n" + e.what()};
  }
}

void Config::Apply() const
{
    const std::string event_mode_name{EventModeName(event_mode)};
    const std::string output_dir_str = output_dir.string();

    SEventConfig::SetEventMode(event_mode_name.c_str());
    SEventConfig::SetMaxSlot(maxslot);
    SEventConfig::SetOutFold(output_dir_str.c_str());
}
}
