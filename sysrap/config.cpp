#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

#include "EventTiming.hh"
#include "SEventConfig.hh"

#include "config.h"
#include "config_path.h"

namespace simphony
{

using namespace std;

constexpr const char* PTX_DIR_ENV = "SIMPHONY_PTX_DIR";
constexpr const char* CONFIG_DIR_ENV = "SIMPHONY_CONFIG_DIR";

namespace
{

template <typename Enum>
struct NamedEnumInfo
{
    Enum             value;
    std::string_view name;
};

inline constexpr std::array EventModeInfos{
    NamedEnumInfo<EventMode>{EventMode::DebugHeavy, "DebugHeavy"},
    NamedEnumInfo<EventMode>{EventMode::DebugLite, "DebugLite"},
    NamedEnumInfo<EventMode>{EventMode::Nothing, "Nothing"},
    NamedEnumInfo<EventMode>{EventMode::Minimal, "Minimal"},
    NamedEnumInfo<EventMode>{EventMode::Hit, "Hit"},
    NamedEnumInfo<EventMode>{EventMode::HitPhoton, "HitPhoton"},
    NamedEnumInfo<EventMode>{EventMode::HitPhotonSeq, "HitPhotonSeq"},
    NamedEnumInfo<EventMode>{EventMode::HitSeq, "HitSeq"},
};

inline constexpr std::array TorchGentypeInfos{
    NamedEnumInfo<unsigned>{OpticksGenstep_TORCH, "TORCH"},
};

inline constexpr std::array TorchTypeInfos{
    NamedEnumInfo<unsigned>{T_DISC, "disc"},
    NamedEnumInfo<unsigned>{T_LINE, "line"},
    NamedEnumInfo<unsigned>{T_POINT, "point"},
    NamedEnumInfo<unsigned>{T_CIRCLE, "circle"},
    NamedEnumInfo<unsigned>{T_RECTANGLE, "rectangle"},
    NamedEnumInfo<unsigned>{T_SPHERE_MARSAGLIA, "sphere_marsaglia"},
    NamedEnumInfo<unsigned>{T_SPHERE, "sphere"},
};

inline constexpr std::array ModeLiteInfos{
    NamedEnumInfo<ModeLite>{ModeLite::Unspecified, "Unspecified"},
    NamedEnumInfo<ModeLite>{ModeLite::Standard, "Standard"},
    NamedEnumInfo<ModeLite>{ModeLite::Lite, "Lite"},
    NamedEnumInfo<ModeLite>{ModeLite::DebugCompare, "DebugCompare"},
};

inline constexpr std::array ModeMergeInfos{
    NamedEnumInfo<ModeMerge>{ModeMerge::Unspecified, "Unspecified"},
    NamedEnumInfo<ModeMerge>{ModeMerge::Separate, "Separate"},
    NamedEnumInfo<ModeMerge>{ModeMerge::Merged, "Merged"},
};

inline constexpr std::array SerHintInfos{
    NamedEnumInfo<SerHint>{SerHint::None, "none"},
    NamedEnumInfo<SerHint>{SerHint::Bounce, "bounce"},
};

bool FileExists(const std::string& path)
{
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

std::vector<std::string> SplitSearchPaths(std::string_view paths)
{
    std::vector<std::string> search_paths;

    size_t last = 0;
    size_t next = 0;
    while ((next = paths.find(':', last)) != std::string_view::npos)
    {
        search_paths.push_back(std::string{paths.substr(last, next - last)});
        last = next + 1;
    }

    search_paths.push_back(std::string{paths.substr(last)});
    return search_paths;
}

template <typename Enum, size_t N>
std::string ValidNamedEnumNames(const std::array<NamedEnumInfo<Enum>, N>& infos)
{
    std::string names;
    for (const auto& info : infos)
    {
        if (!names.empty())
            names += ", ";
        names += info.name;
    }
    return names;
}

template <typename Enum, size_t N>
std::string_view NamedEnumName(
    Enum                                      value,
    const std::array<NamedEnumInfo<Enum>, N>& infos,
    std::string_view                          fallback)
{
    const auto it = std::ranges::find(infos, value, &NamedEnumInfo<Enum>::value);
    if (it != infos.end())
        return it->name;

    return fallback;
}

template <typename Enum, size_t N>
Enum ReadNamedEnum(
    const nlohmann::json&                     object,
    const char*                               section,
    const char*                               key,
    const std::array<NamedEnumInfo<Enum>, N>& infos)
{
    const std::string full_key = std::string{section} + "." + key;
    const auto&       value = object.at(key);
    const std::string valid_names = ValidNamedEnumNames(infos);

    if (!value.is_string())
        throw std::invalid_argument{"Invalid " + full_key + " value. Expected string name, one of: " + valid_names};

    const std::string name = value.get<std::string>();
    const auto        it = std::ranges::find(infos, std::string_view{name}, &NamedEnumInfo<Enum>::name);
    if (it != infos.end())
        return it->value;

    throw std::invalid_argument{"Invalid " + full_key + " \"" + name + "\". Expected one of: " + valid_names};
}

std::string_view EventModeName(EventMode mode)
{
    return NamedEnumName(mode, EventModeInfos, "Minimal");
}

template <typename Enum, size_t N>
Enum NamedEnumValue(
    std::string_view                          name,
    const std::array<NamedEnumInfo<Enum>, N>& infos,
    Enum                                      fallback)
{
    const auto it = std::ranges::find(infos, name, &NamedEnumInfo<Enum>::name);
    return it == infos.end() ? fallback : it->value;
}

ModeLite ModeLiteValue(std::int64_t value)
{
    switch (value)
    {
    case 0:
        return ModeLite::Standard;
    case 1:
        return ModeLite::Lite;
    case 2:
        return ModeLite::DebugCompare;
    default:
        return ModeLite::Unspecified;
    }
}

ModeMerge ModeMergeValue(std::int64_t value)
{
    switch (value)
    {
    case 0:
        return ModeMerge::Separate;
    case 1:
        return ModeMerge::Merged;
    default:
        return ModeMerge::Unspecified;
    }
}

std::string StringOrEmpty(const char* value)
{
    return value ? value : "";
}

void WarnLegacyEventEnvironment()
{
    static constexpr std::array legacy_keys{
        std::pair{"OPTICKS_INTEGRATION_MODE", "event.integration_mode"},
        std::pair{"OPTICKS_EVENT_MODE", "event.event_mode"},
        std::pair{"OPTICKS_RUNNING_MODE", "event.running_mode"},
        std::pair{"OPTICKS_START_INDEX", "event.start_index"},
        std::pair{"OPTICKS_NUM_EVENT", "event.num_event"},
        std::pair{"OPTICKS_EVENT_SKIPAHEAD", "event.event_skipahead"},
        std::pair{"OPTICKS_G4STATE_SPEC", "event.g4_state_spec"},
        std::pair{"OPTICKS_G4STATE_RERUN", "event.g4_state_rerun"},
        std::pair{"OPTICKS_MAX_SLOT", "event.maxslot"},
        std::pair{"OPTICKS_MAX_GENSTEP", "event.max_genstep"},
        std::pair{"OPTICKS_MAX_SIMTRACE", "event.max_simtrace"},
        std::pair{"OPTICKS_MAX_BOUNCE", "event.max_bounce"},
        std::pair{"OPTICKS_MAX_TIME", "event.max_time"},
        std::pair{"OPTICKS_MODE_LITE", "event.mode_lite"},
        std::pair{"OPTICKS_MODE_MERGE", "event.mode_merge"},
        std::pair{"OPTICKS_MERGE_WINDOW", "event.merge_window"},
        std::pair{"OPTICKS_OUT_FOLD", "event.output_dir"},
        std::pair{"OPTICKS_EVENT_RELDIR", "event.event_reldir"},
        std::pair{"OPTICKS_HIT_MASK", "event.hit_mask"},
        std::pair{"OPTICKS_PROPAGATE_EPSILON", "event.propagate_epsilon"},
        std::pair{"OPTICKS_PROPAGATE_EPSILON0", "event.propagate_epsilon0"},
        std::pair{"OPTICKS_PROPAGATE_EPSILON0_MASK", "event.propagate_epsilon0_mask"},
        std::pair{"OPTICKS_PROPAGATE_REFINE", "event.propagate_refine"},
        std::pair{"OPTICKS_PROPAGATE_REFINE_DISTANCE", "event.propagate_refine_distance"},
        std::pair{"OPTICKS_INPUT_GENSTEP", "event.input_genstep"},
        std::pair{"OPTICKS_INPUT_PHOTON", "event.input_photon"},
        std::pair{"OPTICKS_INPUT_PHOTON_FRAME", "event.input_photon_frame"},
        std::pair{"OPTICKS_INPUT_PHOTON_CHANGE_TIME", "event.input_photon_change_time"},
        std::pair{"OPTICKS_INPUT_PHOTON_RECORD_TIME", "event.input_photon_record_time"},
        std::pair{"OPTICKS_INPUT_PHOTON_RECORD_SLICE", "event.input_photon_record_slice"},
    };

    for (const auto& [environment, json_key] : legacy_keys)
    {
        if (std::getenv(environment))
        {
            std::cerr << "Deprecated environment setting " << environment
                      << " is in use; migrate to " << json_key
                      << " (an explicit JSON value takes precedence).\n";
        }
    }
}

template <typename T>
void Assign(const nlohmann::json& object, const char* key, T& value)
{
    if (const auto it = object.find(key); it != object.end())
        value = it->get<T>();
}

void AssignFloat2(const nlohmann::json& object, const char* key, float2& value)
{
    if (const auto it = object.find(key); it != object.end())
        value = make_float2((*it)[0].get<float>(), (*it)[1].get<float>());
}

void AssignFloat3(const nlohmann::json& object, const char* key, float3& value)
{
    if (const auto it = object.find(key); it != object.end())
        value = make_float3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
}

void AssignNormalizedFloat3(const nlohmann::json& object, const char* key, float3& value)
{
    if (const auto it = object.find(key); it != object.end())
    {
        const float3 candidate = make_float3(
            (*it)[0].get<float>(),
            (*it)[1].get<float>(),
            (*it)[2].get<float>());
        constexpr float epsilon = 1e-12f;
        const float     length_squared = candidate.x * candidate.x + candidate.y * candidate.y + candidate.z * candidate.z;
        if (length_squared <= epsilon)
            throw std::invalid_argument(std::string("Cannot normalize vector for key '") + key + "'");

        value = normalize(candidate);
    }
}

template <typename Enum, size_t N>
void AssignNamedEnum(
    const nlohmann::json&                     object,
    const char*                               section,
    const char*                               key,
    Enum&                                     value,
    const std::array<NamedEnumInfo<Enum>, N>& infos)
{
    if (object.contains(key))
        value = ReadNamedEnum(object, section, key, infos);
}

void AssignOutputDir(const nlohmann::json& event, std::filesystem::path& output_dir)
{
    if (const auto it = event.find("output_dir"); it != event.end())
        output_dir = it->get<std::string>();
}

void AssignPath(const nlohmann::json& object, const char* key, std::filesystem::path& value)
{
    if (const auto it = object.find(key); it != object.end())
        value = it->get<std::string>();
}

void ResolveOutputPath(const std::filesystem::path& output_dir, std::filesystem::path& path)
{
    if (!path.empty() && path.is_relative())
        path = output_dir / path;
}

} // namespace

Config::Config(std::string config_name) :
    name{config_name}
{
    LoadLegacyEventConfig();
    WarnLegacyEventEnvironment();
    ReadConfig(Locate(name + ".json"));
    Apply();
}

void Config::LoadLegacyEventConfig()
{
    event_mode = NamedEnumValue(
        StringOrEmpty(SEventConfig::EventMode()), EventModeInfos, EventMode::Minimal);
    integration_mode = SEventConfig::IntegrationMode();
    running_mode = StringOrEmpty(SEventConfig::RunningModeLabel());
    start_index = SEventConfig::StartIndex();
    num_event = SEventConfig::NumEvent();
    event_skipahead = SEventConfig::EventSkipahead();
    g4_state_spec = StringOrEmpty(SEventConfig::G4StateSpec());
    g4_state_rerun = SEventConfig::G4StateRerunConfigured();
    max_bounce = static_cast<int>(SEventConfig::MaxBounce());
    max_genstep = static_cast<int>(SEventConfig::MaxGenstep());
    maxslot = static_cast<int>(SEventConfig::MaxSlot());
    max_simtrace = SEventConfig::MaxSimtrace();
    max_time = SEventConfig::MaxTime();
    mode_lite = ModeLiteValue(SEventConfig::ModeLite());
    mode_merge = ModeMergeValue(SEventConfig::ModeMerge());
    merge_window = SEventConfig::MergeWindow();
    output_dir = StringOrEmpty(SEventConfig::OutFold());
    event_reldir = StringOrEmpty(SEventConfig::EventReldir());
    hit_mask = SEventConfig::HitMaskLabel();
    propagate_epsilon = SEventConfig::PropagateEpsilon();
    propagate_epsilon0 = SEventConfig::PropagateEpsilon0();
    propagate_epsilon0_mask = SEventConfig::PropagateEpsilon0MaskLabel();
    propagate_refine = SEventConfig::PropagateRefine();
    propagate_refine_distance = SEventConfig::PropagateRefineDistance();
    input_genstep = StringOrEmpty(SEventConfig::InputGenstep());
    input_photon = StringOrEmpty(SEventConfig::InputPhoton());
    input_photon_frame = StringOrEmpty(SEventConfig::InputPhotonFrame());
    input_photon_change_time = SEventConfig::InputPhotonChangeTime();
    input_photon_record_time = StringOrEmpty(SEventConfig::InputPhotonRecordTime());
    input_photon_record_slice = StringOrEmpty(SEventConfig::InputPhotonRecordSlice());
}

std::string Config::PtxPath(const std::string& ptx_name)
{
    std::vector<std::string> search_paths;
    if (const char* env_dir = std::getenv(PTX_DIR_ENV); env_dir && *env_dir)
        search_paths.emplace_back(env_dir);

    const auto configured_paths = SplitSearchPaths(SIMPHONY_PTX_SEARCH_PATHS);
    search_paths.insert(search_paths.end(), configured_paths.begin(), configured_paths.end());

    std::vector<std::string> candidates;
    for (const auto& dir : search_paths)
    {
        if (dir.empty())
            continue;

        std::string candidate = (std::filesystem::path{dir} / ptx_name).string();
        candidates.push_back(candidate);
        if (FileExists(candidate))
            return candidate;
    }

    std::stringstream errmsg;
    errmsg << "Could not resolve PTX file \"" << ptx_name << "\".\n"
           << "Expected one of:\n"
           << "  - " << PTX_DIR_ENV << "=<directory containing " << ptx_name << ">\n";
    for (const auto& candidate : candidates)
        errmsg << "  - " << candidate << "\n";
    throw std::runtime_error(errmsg.str());
}

std::string Config::Locate(std::string filename) const
{
    std::vector<std::string> search_paths;

    const char*       config_dir = std::getenv(CONFIG_DIR_ENV);
    const std::string user_dir{config_dir ? config_dir : ""};

    if (user_dir.empty())
    {
        search_paths = SplitSearchPaths(SIMPHONY_CONFIG_SEARCH_PATHS);
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
        errmsg += "\nSet " + std::string{CONFIG_DIR_ENV} + " to override the config search directory.";
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

            AssignNamedEnum(torch_, "torch", "gentype", torch.gentype, TorchGentypeInfos);
            Assign(torch_, "trackid", torch.trackid);
            Assign(torch_, "matline", torch.matline);
            Assign(torch_, "numphoton", torch.numphoton);
            AssignFloat3(torch_, "pos", torch.pos);
            Assign(torch_, "time", torch.time);
            AssignNormalizedFloat3(torch_, "mom", torch.mom);
            Assign(torch_, "weight", torch.weight);
            AssignFloat3(torch_, "pol", torch.pol);
            Assign(torch_, "wavelength", torch.wavelength);
            AssignFloat2(torch_, "zenith", torch.zenith);
            AssignFloat2(torch_, "azimuth", torch.azimuth);
            Assign(torch_, "radius", torch.radius);
            Assign(torch_, "distance", torch.distance);
            Assign(torch_, "mode", torch.mode);
            AssignNamedEnum(torch_, "torch", "type", torch.type, TorchTypeInfos);
        }

        if (const auto it = json.find("event"); it != json.end())
        {
            const nlohmann::json& event_ = *it;

            Assign(event_, "integration_mode", integration_mode);
            Assign(event_, "running_mode", running_mode);
            Assign(event_, "start_index", start_index);
            Assign(event_, "num_event", num_event);
            Assign(event_, "event_skipahead", event_skipahead);
            Assign(event_, "g4_state_spec", g4_state_spec);
            Assign(event_, "g4_state_rerun", g4_state_rerun);
            Assign(event_, "max_bounce", max_bounce);
            Assign(event_, "max_genstep", max_genstep);
            Assign(event_, "maxslot", maxslot);
            Assign(event_, "max_simtrace", max_simtrace);
            Assign(event_, "max_time", max_time);
            AssignNamedEnum(event_, "event", "event_mode", event_mode, EventModeInfos);
            AssignNamedEnum(event_, "event", "mode_lite", mode_lite, ModeLiteInfos);
            AssignNamedEnum(event_, "event", "mode_merge", mode_merge, ModeMergeInfos);
            Assign(event_, "merge_window", merge_window);
            AssignOutputDir(event_, output_dir);
            Assign(event_, "event_reldir", event_reldir);
            Assign(event_, "hit_mask", hit_mask);
            Assign(event_, "propagate_epsilon", propagate_epsilon);
            Assign(event_, "propagate_epsilon0", propagate_epsilon0);
            Assign(event_, "propagate_epsilon0_mask", propagate_epsilon0_mask);
            Assign(event_, "propagate_refine", propagate_refine);
            Assign(event_, "propagate_refine_distance", propagate_refine_distance);
            Assign(event_, "input_genstep", input_genstep);
            Assign(event_, "input_photon", input_photon);
            Assign(event_, "input_photon_frame", input_photon_frame);
            Assign(event_, "input_photon_change_time", input_photon_change_time);
            Assign(event_, "input_photon_record_time", input_photon_record_time);
            Assign(event_, "input_photon_record_slice", input_photon_record_slice);
        }

        if (const auto it = json.find("ser"); it != json.end())
        {
            const nlohmann::json& ser_ = *it;

            Assign(ser_, "interval", ser_interval);
            AssignNamedEnum(ser_, "ser", "hint", ser_hint, SerHintInfos);
        }

        if (const auto it = json.find("event_timing"); it != json.end())
        {
            const nlohmann::json& event_timing = *it;
            AssignPath(event_timing, "output", event_timing_output);

            if (const auto profile_it = event_timing.find("profile"); profile_it != event_timing.end())
            {
                const nlohmann::json& profile = *profile_it;
                Assign(profile, "enabled", event_timing_profile_enabled);
                AssignPath(profile, "output", event_timing_profile_output);
                Assign(profile, "path_index", event_timing_profile_path_index);
            }
        }

        ResolveOutputPath(output_dir, event_timing_output);
        ResolveOutputPath(output_dir, event_timing_profile_output);
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
    const auto        nullable = [](const std::string& value) { return value.empty() ? nullptr : value.c_str(); };

    SEventConfig::SetIntegrationMode(integration_mode);
    SEventConfig::SetEventMode(event_mode_name.c_str());
    SEventConfig::SetRunningMode(running_mode.c_str());
    SEventConfig::SetStartIndex(start_index);
    SEventConfig::SetNumEvent(num_event);
    SEventConfig::SetEventSkipahead(event_skipahead);
    SEventConfig::SetG4StateSpec(nullable(g4_state_spec));
    SEventConfig::SetG4StateRerun(g4_state_rerun);
    SEventConfig::SetMaxBounce(max_bounce);
    SEventConfig::SetMaxGenstep(max_genstep);
    SEventConfig::SetMaxSlot(maxslot);
    SEventConfig::SetMaxSimtrace(max_simtrace);
    SEventConfig::SetMaxTime(max_time);
    if (mode_lite != ModeLite::Unspecified)
        SEventConfig::SetModeLite(static_cast<int>(mode_lite));
    if (mode_merge != ModeMerge::Unspecified)
        SEventConfig::SetModeMerge(static_cast<int>(mode_merge));
    SEventConfig::SetMergeWindow(merge_window);
    SEventConfig::SetOutFold(output_dir_str.c_str());
    SEventConfig::SetEventReldir(nullable(event_reldir));
    SEventConfig::SetHitMask(hit_mask.c_str(), hit_mask.find('|') == std::string::npos ? ',' : '|');
    SEventConfig::SetPropagateEpsilon(propagate_epsilon);
    SEventConfig::SetPropagateEpsilon0(propagate_epsilon0);
    SEventConfig::SetPropagateEpsilon0Mask(
        propagate_epsilon0_mask.c_str(),
        propagate_epsilon0_mask.find('|') == std::string::npos ? ',' : '|');
    SEventConfig::SetPropagateRefine(propagate_refine);
    SEventConfig::SetPropagateRefineDistance(propagate_refine_distance);
    SEventConfig::SetInputGenstep(nullable(input_genstep));
    SEventConfig::SetInputPhoton(nullable(input_photon));
    SEventConfig::SetInputPhotonFrame(nullable(input_photon_frame));
    SEventConfig::SetInputPhotonChangeTime(input_photon_change_time);
    SEventConfig::SetInputPhotonRecordTime(nullable(input_photon_record_time));
    SEventConfig::SetInputPhotonRecordSlice(nullable(input_photon_record_slice));
    SEventConfig::_SerInterval = ser_interval;
    SEventConfig::_SerHint = static_cast<int>(ser_hint);

    EventTimingProfile::Configure(
        event_timing_profile_enabled,
        event_timing_profile_output,
        event_timing_profile_path_index);
    if (event_timing_profile_enabled)
        EventTimingProfile::Mark("EventTimingProfile__Configure");
}

} // namespace simphony
