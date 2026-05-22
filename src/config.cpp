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

#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

#include "sysrap/SEventConfig.hh"

#include "config.h"
#include "config_path.h"

namespace gphox
{

using namespace std;

constexpr const char* GPHOX_PTX_PATH_ENV = "CSGOptiX__optixpath";

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

bool FileExists(const std::string& path)
{
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

std::string ValidEventModes()
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

std::string_view EventModeName(EventMode mode)
{
    const auto it = FindEventMode(mode);
    if (it != EventModeInfos.end())
        return it->name;

    return "Minimal";
}

EventMode ReadEventMode(const nlohmann::json& event)
{
    std::string name = event["mode"].get<std::string>();

    const auto it = FindEventMode(name);
    if (it != EventModeInfos.end())
        return it->mode;

    throw std::invalid_argument{
        "Invalid event.mode \"" + std::string{name} + "\". Expected one of: " + ValidEventModes()};
}

template <typename T>
void AssignIfPresent(const nlohmann::json& object, const char* key, T& value)
{
    if (const auto it = object.find(key); it != object.end())
        value = it->get<T>();
}

void AssignFloat2IfPresent(const nlohmann::json& object, const char* key, float2& value)
{
    if (const auto it = object.find(key); it != object.end())
        value = make_float2((*it)[0].get<float>(), (*it)[1].get<float>());
}

void AssignFloat3IfPresent(const nlohmann::json& object, const char* key, float3& value)
{
    if (const auto it = object.find(key); it != object.end())
        value = make_float3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
}

void AssignNormalizedFloat3IfPresent(const nlohmann::json& object, const char* key, float3& value)
{
    if (const auto it = object.find(key); it != object.end())
        value = normalize(make_float3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()));
}

void AssignEventModeIfPresent(const nlohmann::json& event, EventMode& mode)
{
    if (event.contains("mode"))
        mode = ReadEventMode(event);
}

void AssignOutputDirIfPresent(const nlohmann::json& event, std::filesystem::path& output_dir)
{
    if (const auto it = event.find("output_dir"); it != event.end())
        output_dir = it->get<std::string>();
}

void AssignTorchGentypeIfPresent(const nlohmann::json& torch, unsigned& gentype)
{
    if (const auto it = torch.find("gentype"); it != torch.end())
    {
        const std::string gentype_name = it->get<std::string>();
        if (OpticksGenstep_::Type(gentype_name) != OpticksGenstep_TORCH)
            throw std::invalid_argument{"Invalid torch.gentype \"" + gentype_name + "\". Expected TORCH"};

        gentype = OpticksGenstep_TORCH;
    }
}

void AssignTorchTypeIfPresent(const nlohmann::json& torch, unsigned& type)
{
    if (const auto it = torch.find("type"); it != torch.end())
        type = storchtype::Type(it->get<std::string>());
}

} // namespace

Config::Config(std::string config_name) :
    name{config_name}
{
    ReadConfig(Locate(name + ".json"));
    Apply();
}

std::string Config::PtxPath(const std::string& ptx_name)
{
    const char* env_path = std::getenv(GPHOX_PTX_PATH_ENV);
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
            search_paths.push_back(paths.substr(last, next - last));
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

/**
 * Expects a valid filepath.
 */
void Config::ReadConfig(std::string filepath)
{
    nlohmann::json json;

    try
    {
        std::ifstream ifs(filepath);
        ifs >> json;

        if (const auto it = json.find("torch"); it != json.end())
        {
            const nlohmann::json& torch_ = *it;

            AssignTorchGentypeIfPresent(torch_, torch.gentype);
            AssignIfPresent(torch_, "trackid", torch.trackid);
            AssignIfPresent(torch_, "matline", torch.matline);
            AssignIfPresent(torch_, "numphoton", torch.numphoton);
            AssignFloat3IfPresent(torch_, "pos", torch.pos);
            AssignIfPresent(torch_, "time", torch.time);
            AssignNormalizedFloat3IfPresent(torch_, "mom", torch.mom);
            AssignIfPresent(torch_, "weight", torch.weight);
            AssignFloat3IfPresent(torch_, "pol", torch.pol);
            AssignIfPresent(torch_, "wavelength", torch.wavelength);
            AssignFloat2IfPresent(torch_, "zenith", torch.zenith);
            AssignFloat2IfPresent(torch_, "azimuth", torch.azimuth);
            AssignIfPresent(torch_, "radius", torch.radius);
            AssignIfPresent(torch_, "distance", torch.distance);
            AssignIfPresent(torch_, "mode", torch.mode);
            AssignTorchTypeIfPresent(torch_, torch.type);
        }

        if (const auto it = json.find("event"); it != json.end())
        {
            const nlohmann::json& event_ = *it;

            AssignEventModeIfPresent(event_, event_mode);
            AssignIfPresent(event_, "maxslot", maxslot);
            AssignIfPresent(event_, "max_bounce", max_bounce);
            AssignIfPresent(event_, "propagate_epsilon", propagate_epsilon);
            AssignIfPresent(event_, "propagate_epsilon0", propagate_epsilon0);
            AssignIfPresent(event_, "propagate_epsilon0_mask", propagate_epsilon0_mask);
            AssignOutputDirIfPresent(event_, output_dir);
        }
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
    SEventConfig::SetMaxBounce(max_bounce);
    SEventConfig::SetPropagateEpsilon(propagate_epsilon);
    SEventConfig::SetPropagateEpsilon0(propagate_epsilon0);
    SEventConfig::SetPropagateEpsilon0Mask(propagate_epsilon0_mask.c_str());
    SEventConfig::SetOutFold(output_dir_str.c_str());
}

} // namespace gphox
