#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "EventTiming.hh"
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

    EventTimingProfile::Configure(false);
    unsetenv("SIMPHONY_CONFIG_DIR");
    fs::remove_all(directory);
    return 0;
}
