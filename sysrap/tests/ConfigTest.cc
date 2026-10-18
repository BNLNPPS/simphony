#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "EventTiming.hh"
#include "SEventConfig.hh"
#include "config.h"

namespace
{
namespace fs = std::filesystem;

void WriteText(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << text;
    output.close();
    assert(output);
}

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main()
{
    const fs::path directory = fs::temp_directory_path() /
                               ("ConfigTest-" + std::to_string(static_cast<long long>(::getpid())));
    const fs::path output_dir = directory / "output";
    fs::remove_all(directory);
    fs::create_directories(directory);

    WriteText(
        directory / "timing.json",
        "{\n"
        "  \"event\": {\"output_dir\": \"" +
            output_dir.string() + "\"},\n"
                                  "  \"event_timing\": {\n"
                                  "    \"output\": \"events.csv\",\n"
                                  "    \"profile\": {\n"
                                  "      \"enabled\": true,\n"
                                  "      \"output\": \"event_timing_profile_%03d.csv\",\n"
                                  "      \"path_index\": 7\n"
                                  "    }\n"
                                  "  }\n"
                                  "}\n");

    setenv("SIMPHONY_CONFIG_DIR", directory.c_str(), 1);

    const fs::path stale_output_dir = directory / "stale-output";
    SEventConfig::SetIntegrationMode(2);
    SEventConfig::SetMaxTime(77.f);
    SEventConfig::SetModeLite(2);
    SEventConfig::SetModeMerge(1);
    SEventConfig::SetOutFold(stale_output_dir.c_str());
    SEventConfig::SetInputPhoton("stale-photons.npy");
    WriteText(directory / "defaults.json", "{}\n");

    const simphony::Config defaults("defaults");
    AssertEventDefaults(defaults);
    AssertFacadeEventDefaults();

    WriteText(
        directory / "event.json",
        R"json({
  "event": {
    "event_mode": "HitPhotonSeq",
    "integration_mode": 3,
    "running_mode": "SRM_G4STATE_RERUN",
    "start_index": 11,
    "num_event": 7,
    "event_skipahead": 234567,
    "g4_state_spec": "2000:38",
    "g4_state_rerun": 23,
    "max_bounce": 47,
    "max_genstep": 7654321,
    "maxslot": 123456,
    "max_simtrace": 54321,
    "max_time": 123.5,
    "mode_lite": "Lite",
    "mode_merge": "Merged",
    "merge_window": 2.25,
    "event_reldir": "events/test",
    "hit_mask": "SD,SA",
    "propagate_epsilon": 0.125,
    "propagate_epsilon0": 0.25,
    "propagate_epsilon0_mask": "TO,CK",
    "propagate_refine": 1,
    "propagate_refine_distance": 12.5,
    "input_genstep": "gensteps_%03d.npy",
    "input_photon": "photons.npy",
    "input_photon_frame": "sensor:0:1",
    "input_photon_change_time": 4.5,
    "input_photon_record_time": "[10:80:10]",
    "input_photon_record_slice": "TO BT SA"
  }
})json");

    const simphony::Config event("event");
    assert(event.event_mode == simphony::EventMode::HitPhotonSeq);
    assert(event.integration_mode == 3);
    assert(event.running_mode == "SRM_G4STATE_RERUN");
    assert(event.start_index == 11);
    assert(event.num_event == 7);
    assert(event.event_skipahead == 234567);
    assert(event.g4_state_spec == "2000:38");
    assert(event.g4_state_rerun == 23);
    assert(event.max_bounce == 47);
    assert(event.max_genstep == 7654321);
    assert(event.maxslot == 123456);
    assert(event.max_simtrace == 54321);
    assert(event.max_time == 123.5f);
    assert(event.mode_lite == simphony::ModeLite::Lite);
    assert(event.mode_merge == simphony::ModeMerge::Merged);
    assert(event.merge_window == 2.25f);
    assert(event.event_reldir == "events/test");
    assert(event.hit_mask == "SD,SA");
    assert(event.propagate_epsilon == 0.125f);
    assert(event.propagate_epsilon0 == 0.25f);
    assert(event.propagate_epsilon0_mask == "TO,CK");
    assert(event.propagate_refine == 1u);
    assert(event.propagate_refine_distance == 12.5f);
    assert(event.input_genstep == "gensteps_%03d.npy");
    assert(event.input_photon == "photons.npy");
    assert(event.input_photon_frame == "sensor:0:1");
    assert(event.input_photon_change_time == 4.5f);
    assert(event.input_photon_record_time == "[10:80:10]");
    assert(event.input_photon_record_slice == "TO BT SA");

    assert(std::strcmp(SEventConfig::EventMode(), "HitPhotonSeq") == 0);
    assert(SEventConfig::IntegrationMode() == 3);
    assert(std::strcmp(SEventConfig::RunningModeLabel(), "SRM_G4STATE_RERUN") == 0);
    assert(SEventConfig::EventIndex(0) == 11);
    assert(SEventConfig::NumEvent() == 7);
    assert(SEventConfig::EventSkipahead() == 234567);
    assert(std::strcmp(SEventConfig::G4StateSpec(), "2000:38") == 0);
    assert(SEventConfig::G4StateRerun() == 23);
    assert(SEventConfig::MaxSimtrace() == 54321);
    assert(SEventConfig::MaxTime() == 123.5f);
    assert(SEventConfig::MergeWindow() == 2.25f);
    assert(std::strcmp(SEventConfig::EventReldir(), "events/test") == 0);
    assert(SEventConfig::HitMaskLabel() == "SD|SA");
    assert(SEventConfig::PropagateRefine() == 1u);
    assert(SEventConfig::PropagateRefineDistance() == 12.5f);
    assert(std::strcmp(SEventConfig::InputGenstep(), "gensteps_%03d.npy") == 0);
    assert(std::strcmp(SEventConfig::InputPhoton(), "photons.npy") == 0);
    assert(std::strcmp(SEventConfig::InputPhotonFrame(), "sensor:0:1") == 0);
    assert(SEventConfig::InputPhotonChangeTime() == 4.5f);
    assert(std::strcmp(SEventConfig::InputPhotonRecordTime(), "[10:80:10]") == 0);
    assert(std::strcmp(SEventConfig::InputPhotonRecordSlice(), "TO BT SA") == 0);

    WriteText(directory / "empty-event.json", "{\"event\": {}}\n");
    const simphony::Config empty_event("empty-event");
    AssertEventDefaults(empty_event);
    AssertFacadeEventDefaults();

    EventTimingProfile::Clear();
    const simphony::Config config("timing");

    assert(config.event_timing_output == output_dir / "events.csv");
    assert(config.event_timing_profile_enabled);
    assert(config.event_timing_profile_output == output_dir / "event_timing_profile_%03d.csv");
    assert(config.event_timing_profile_path_index == 7);
    assert(EventTimingProfile::Enabled());
    assert(EventTimingProfile::Path() == output_dir / "event_timing_profile_007.csv");
    assert(EventTimingRecorder(config.event_timing_output).enabled());
    const std::vector<EventTimingProfileRecord> records = EventTimingProfile::Records();
    assert(records.size() == 1u);
    assert(records.front().name == "EventTimingProfile__Configure");
    assert(records.front().sample.has(EventTimingCapture::Monotonic));

    WriteText(directory / "omitted.json", "{}\n");
    const simphony::Config omitted("omitted");
    assert(omitted.event_timing_output.empty());
    assert(!EventTimingRecorder(omitted.event_timing_output).enabled());
    assert(!EventTimingProfile::Enabled());

    WriteText(
        directory / "empty.json",
        "{\"event_timing\": {\"output\": \"\", \"profile\": {\"enabled\": false}}}\n");
    const simphony::Config empty("empty");
    assert(empty.event_timing_output.empty());
    assert(!EventTimingRecorder(empty.event_timing_output).enabled());
    assert(!EventTimingProfile::Enabled());

    WriteText(
        directory / "bad-timing.json",
        "{\"event_timing\": {\"profile\": {\"path_index\": \"seven\"}}}\n");
    bool rejected_bad_index = false;
    try
    {
        (void)simphony::Config("bad-timing");
    }
    catch (const std::runtime_error&)
    {
        rejected_bad_index = true;
    }
    assert(rejected_bad_index);

    WriteText(directory / "ser-none.json", "{\"ser\": {\"interval\": 8, \"hint\": \"none\"}}\n");
    const simphony::Config ser_none("ser-none");
    Require(ser_none.ser_hint == simphony::SerHint::None, "ser.hint none was not parsed");
    Require(SEventConfig::_SerHint == 0, "ser.hint none was not applied");

    WriteText(directory / "ser-bounce.json", "{\"ser\": {\"interval\": 8, \"hint\": \"bounce\"}}\n");
    const simphony::Config ser_bounce("ser-bounce");
    Require(ser_bounce.ser_hint == simphony::SerHint::Bounce, "ser.hint bounce was not parsed");
    Require(SEventConfig::_SerHint == 1, "ser.hint bounce was not applied");

    for (const auto& [name, value] : std::vector<std::pair<std::string, std::string>>{
             {"bad-ser-number", "2"},
             {"bad-ser-name", "\"boundary-wavelength\""},
         })
    {
        WriteText(directory / (name + ".json"), "{\"ser\": {\"hint\": " + value + "}}\n");
        bool rejected_bad_hint = false;
        try
        {
            (void)simphony::Config(name);
        }
        catch (const std::runtime_error&)
        {
            rejected_bad_hint = true;
        }
        Require(rejected_bad_hint, "invalid ser.hint was accepted");
    }

    EventTimingProfile::Configure(false);
    unsetenv("SIMPHONY_CONFIG_DIR");
    fs::remove_all(directory);

    setenv("SIMPHONY_CONFIG_DIR", SIMPHONY_SOURCE_CONFIG_DIR, 1);
    const auto assert_event_mode = [](const char* name, simphony::EventMode expected) {
        const simphony::Config config(name);
        assert(config.event_mode == expected);
    };
    assert_event_mode("8x8SiPM_crystal", simphony::EventMode::Minimal);
    const simphony::Config async_gpu("async_gpu");
    assert(async_gpu.event_mode == simphony::EventMode::Minimal);
    assert(async_gpu.max_bounce == 1000);
    const simphony::Config dd4hep_gpu("dd4hep_gpu");
    assert(dd4hep_gpu.event_mode == simphony::EventMode::Minimal);
    assert(dd4hep_gpu.integration_mode == 1);
    assert_event_mode("dev", simphony::EventMode::DebugLite);
    assert_event_mode("dev_timing", simphony::EventMode::DebugLite);
    assert_event_mode("drich_aerogel", simphony::EventMode::HitPhoton);
    assert_event_mode("drich_direct_sensor", simphony::EventMode::HitPhoton);
    assert_event_mode("drich_mirror", simphony::EventMode::HitPhoton);
    assert_event_mode("dune_mock_wls_detector_box", simphony::EventMode::DebugLite);
    assert_event_mode("pfrich", simphony::EventMode::Minimal);
    assert_event_mode("pfrich_profiled", simphony::EventMode::Minimal);
    assert_event_mode("sibling_pair", simphony::EventMode::HitPhoton);
    assert_event_mode("sphere_leak", simphony::EventMode::DebugLite);
    assert_event_mode("trap_iso", simphony::EventMode::DebugLite);
    assert_event_mode("wls_test", simphony::EventMode::HitPhoton);
    unsetenv("SIMPHONY_CONFIG_DIR");
    return 0;
}
