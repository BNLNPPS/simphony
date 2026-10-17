#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "torch.h"

namespace simphony
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

enum class ModeLite : int
{
    /// Preserve SEventConfig's lower-level default or OPTICKS_MODE_LITE value.
    Unspecified = -1,

    /// Store standard photon/hit components. ModeMerge still controls merged hits.
    Standard = 0,

    /// Store compact photonlite/hitlite components instead of the standard forms.
    Lite = 1,

    /// Debug mode that gathers standard, lite, and local comparison components.
    DebugCompare = 2
};

enum class ModeMerge : int
{
    /// Preserve SEventConfig's lower-level default or OPTICKS_MODE_MERGE value.
    Unspecified = -1,

    /// Keep unmerged hit or hitlite components as the canonical hit output.
    Separate = 0,

    /// Use merged hit or hitlitemerged components as the canonical hit output.
    Merged = 1
};

enum class SerHint : int
{
    None = 0,
    Bounce = 1
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

    /// Resolve a PTX file from SIMPHONY_PTX_DIR or configured search paths.
    static std::string PtxPath(const std::string& ptx_name = "CSGOptiX.ptx");

    /// A unique name associated with this Config
    std::string name{"dev"};

    /// Event persistence mode applied to SEventConfig.
    EventMode event_mode{EventMode::Minimal};

    /// Opticks/framework integration mode: -1 disabled, 1 GPU, 2 CPU, 3 both.
    int integration_mode{-1};

    /// Event source or Geant4 state handling mode accepted by SRM.
    std::string running_mode{"SRM_DEFAULT"};

    /// First externally visible event index.
    int start_index{0};

    /// Number of events in the run.
    int num_event{1};

    /// Random-number skipahead applied between event indices.
    int event_skipahead{100000};

    /// Geant4 random-state allocation specification.
    std::string g4_state_spec{"1000:38"};

    /// Photon index selected when rerunning saved Geant4 state.
    int g4_state_rerun{-1};

    /// Maximum photon bounce count.
    int max_bounce{31};

    /// Maximum gensteps allocated for event uploads.
    int max_genstep{10000000};

    /// Maximum event slots applied to SEventConfig.
    int maxslot{0};

    /// Maximum simtrace records allocated for a launch.
    std::int64_t max_simtrace{1000000000000LL};

    /// Simulation time cutoff in nanoseconds.
    float max_time{1.e27f};

    /// Optional compact photon/hit storage mode.
    ModeLite mode_lite{ModeLite::Unspecified};

    /// Optional hit merge mode, combined with mode_lite to choose hit output.
    ModeMerge mode_merge{ModeMerge::Unspecified};

    /// Time window in nanoseconds used when merging hits.
    float merge_window{0.f};

    /// Base directory for event output folders.
    std::filesystem::path output_dir{std::filesystem::current_path()};

    /// Relative directory created below output_dir for event arrays.
    std::string event_reldir{"ALL${VERSION:-0}_${OPTICKS_EVENT_NAME:-no_opticks_event_name}"};

    /// Optical flag abbreviations selecting photons copied to hit output.
    std::string hit_mask{"SD"};

    /// Per-event CPU/GPU timing CSV. Empty disables event timeline recording.
    std::filesystem::path event_timing_output{};

    /// Enable detailed lifecycle and memory profile sampling.
    bool event_timing_profile_enabled{false};

    /// Detailed timing profile CSV path.
    std::filesystem::path event_timing_profile_output{"event_timing_profile.csv"};

    /// Integer substituted into a formatted detailed profile path.
    int event_timing_profile_path_index{0};

    /// Ray offset after boundary crossing.
    float propagate_epsilon{0.05f};

    /// Ray offset after bulk interaction.
    float propagate_epsilon0{0.05f};

    /// Flag mask selecting which bulk interactions use propagate_epsilon0.
    std::string propagate_epsilon0_mask{"TO,CK,SI,SC,RE"};

    int     ser_interval{0};
    SerHint ser_hint{SerHint::None};

    /// Bit mask enabling propagation refinement diagnostics.
    unsigned propagate_refine{0u};

    /// Maximum propagation refinement distance in millimetres.
    float propagate_refine_distance{5000.f};

    /// Input genstep path or indexed path pattern.
    std::string input_genstep{};

    /// Input photon array path.
    std::string input_photon{};

    /// Geometry frame specification used to transform input photons.
    std::string input_photon_frame{};

    /// Replacement time for input photons; negative preserves stored times.
    float input_photon_change_time{-1.f};

    /// Time-selection expression when sourcing photons from a record array.
    std::string input_photon_record_time{};

    /// History slice used when sourcing photons from a record array.
    std::string input_photon_record_slice{};

    storch torch{default_torch};

  private:
    std::string Locate(std::string filename) const;
    void LoadLegacyEventConfig();
    void ReadConfig(std::string filepath);
    void Apply() const;
};

} // namespace simphony
