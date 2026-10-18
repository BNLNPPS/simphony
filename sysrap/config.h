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
    /// Preserve the lower-level compatibility value when JSON says `Unspecified`.
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
    /// Preserve the lower-level compatibility value when JSON says `Unspecified`.
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
 *
 * Event settings use this precedence during the compatibility release:
 * an explicit JSON key, a deprecated `OPTICKS_*` environment fallback, then
 * the compiled default. Deprecated environment use is reported on stderr.
 * See `docs/configuration.md` for the complete schema and migration table.
 */
class Config
{
  public:
    Config(std::string config_name = "dev");

    /// Resolve a PTX file from SIMPHONY_PTX_DIR or configured search paths.
    static std::string PtxPath(const std::string& ptx_name = "CSGOptiX.ptx");

    /// Config basename selected by the application; default `dev` loads `dev.json`.
    std::string name{"dev"};

    /// JSON `event.event_mode`; default `Minimal`. Accepts all `EventMode` names and selects event I/O.
    EventMode event_mode{EventMode::Minimal};

    /// JSON `event.integration_mode`; compiled default `-1`. Values: -1 disabled, 1 GPU, 2 CPU, 3 both.
    int integration_mode{-1};

    /// JSON `event.running_mode`; compiled default `SRM_DEFAULT`. Accepts an `SRM_*` name.
    std::string running_mode{"SRM_DEFAULT"};

    /// JSON `event.start_index`; compiled default `0`. Offsets persisted event indices.
    int start_index{0};

    /// JSON `event.num_event`; compiled default `1`. Used by low-level multi-event helpers.
    int num_event{1};

    /// JSON `event.event_skipahead`; compiled default `100000`. Random values skipped between events.
    int event_skipahead{100000};

    /// JSON `event.g4_state_spec`; compiled default `1000:38`. Geant4 random-state allocation spec.
    std::string g4_state_spec{"1000:38"};

    /// JSON `event.g4_state_rerun`; compiled default `-1`. Photon index selected for a state rerun.
    int g4_state_rerun{-1};

    /// JSON `event.max_bounce`; compiled default `31`. Terminates photons after this many bounces.
    int max_bounce{31};

    /// JSON `event.max_genstep`; compiled default `10000000`. Maximum uploaded genstep count.
    int max_genstep{10000000};

    /// JSON `event.maxslot`; compiled default `0`. Zero selects a VRAM-based launch-slot heuristic.
    int maxslot{0};

    /// JSON `event.max_simtrace`; compiled default `10^12`. Maximum simtrace records per launch.
    std::int64_t max_simtrace{1000000000000LL};

    /// JSON `event.max_time`; compiled default `1e27` ns. Optical propagation time cutoff.
    float max_time{1.e27f};

    /// JSON `event.mode_lite`; default `Unspecified`; accepts all `ModeLite` names and selects layouts.
    ModeLite mode_lite{ModeLite::Unspecified};

    /// JSON `event.mode_merge`; default `Unspecified`; accepts all `ModeMerge` names and selects hit merging.
    ModeMerge mode_merge{ModeMerge::Unspecified};

    /// JSON `event.merge_window`; compiled default `0` ns. Time window used when merging hits.
    float merge_window{0.f};

    /// JSON `event.output_dir`; application default is the current working directory. Event output base.
    std::filesystem::path output_dir{std::filesystem::current_path()};

    /// JSON `event.event_reldir`; compiled default `ALL${VERSION:-0}`. Relative event subdirectory.
    std::string event_reldir{"ALL${VERSION:-0}"};

    /// JSON `event.hit_mask`; compiled default `SD`. Comma-delimited photon flag abbreviations.
    std::string hit_mask{"SD"};

    /// JSON `event_timing.output`; compiled default empty. Empty disables the per-event timeline CSV.
    std::filesystem::path event_timing_output{};

    /// JSON `event_timing.profile.enabled`; compiled default `false`.
    bool event_timing_profile_enabled{false};

    /// JSON `event_timing.profile.output`; compiled default `event_timing_profile.csv`.
    std::filesystem::path event_timing_profile_output{"event_timing_profile.csv"};

    /// JSON `event_timing.profile.path_index`; compiled default `0`. Substituted into a `%` path token.
    int event_timing_profile_path_index{0};

    /// JSON `event.propagate_epsilon`; compiled default `0.05` mm. Offset after boundary crossing.
    float propagate_epsilon{0.05f};

    /// JSON `event.propagate_epsilon0`; compiled default `0.05` mm. Offset after bulk interaction.
    float propagate_epsilon0{0.05f};

    /// JSON `event.propagate_epsilon0_mask`; compiled default `TO,CK,SI,SC,RE`.
    std::string propagate_epsilon0_mask{"TO,CK,SI,SC,RE"};

    int     ser_interval{0};
    SerHint ser_hint{SerHint::None};

    /// JSON `event.propagate_refine`; compiled default `0`. Bit mask enabling refinement diagnostics.
    unsigned propagate_refine{0u};

    /// JSON `event.propagate_refine_distance`; compiled default `5000` mm.
    float propagate_refine_distance{5000.f};

    /// JSON `event.input_genstep`; compiled default empty. Genstep path or indexed path pattern.
    std::string input_genstep{};

    /// JSON `event.input_photon`; compiled default empty. Input `.npy` photon array.
    std::string input_photon{};

    /// JSON `event.input_photon_frame`; compiled default empty. Frame used to transform input photons.
    std::string input_photon_frame{};

    /// JSON `event.input_photon_change_time`; compiled default `-1` ns, which preserves stored times.
    float input_photon_change_time{-1.f};

    /// JSON `event.input_photon_record_time`; compiled default empty. Record time-selection expression.
    std::string input_photon_record_time{};

    /// JSON `event.input_photon_record_slice`; compiled default empty. Record history slice expression.
    std::string input_photon_record_slice{};

    /// JSON `torch`; typed optical source parameters. Defaults come from `default_torch`.
    storch torch{default_torch};

  private:
    std::string Locate(std::string filename) const;
    void LoadLegacyEventConfig();
    void ReadConfig(std::string filepath);
    void Apply() const;
};

} // namespace simphony
