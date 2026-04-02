#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
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

bool FileExists(const std::string &path)
{
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

Config::Config(std::string config_name) :
  name{std::getenv("GPHOX_CONFIG") ? std::getenv("GPHOX_CONFIG") : config_name}
{
  ReadConfig(Locate(name + ".json"));
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


/**
 * Expects a valid filepath.
 */
void Config::ReadConfig(std::string filepath)
{
  nlohmann::json json;

  try {
    std::ifstream ifs(filepath);
    ifs >> json;

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
      .type = storchtype::Type(torch_["type"])
    };

    nlohmann::json event_ = json["event"];

    SEventConfig::SetEventMode( string(event_["mode"]).c_str() );
    SEventConfig::SetMaxSlot( event_["maxslot"] );
  }
  catch (nlohmann::json::exception& e) {
    std::string errmsg{"Failed reading config parameters from " + filepath + "\n" + e.what()};
    throw std::runtime_error{errmsg};
  }
}

}
