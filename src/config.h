#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "sysrap/srng.h"
#include "sysrap/storch.h"

namespace gphox {


/**
 * Provides access to all configuration types and data.
 */
class Config
{
 public:

  Config(std::string config_name = "dev");

  static std::string PtxPath(const std::string &ptx_name = "CSGOptiX7.ptx");

  /// Returns the project version (e.g. "0.3.0" or "0.3.0-2-gabcdef-dirty")
  static const char *Version();

  /// Returns the short git commit hash, or empty string if unavailable
  static const char *GitRevision();

  /// A unique name associated with this Config
  std::string name;

  storch torch;

 private:

  std::string Locate(std::string filename) const;
  void ReadConfig(std::string filepath);
};

}
