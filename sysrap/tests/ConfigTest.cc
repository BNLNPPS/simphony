#include <cassert>
#include <cstdlib>
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
