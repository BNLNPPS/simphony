#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "EventTiming.hh"
#include "nlohmann/json.hpp"

namespace
{
namespace fs = std::filesystem;

std::vector<EventTimingSample> sequence;
std::size_t                    sequence_index = 0;

EventTimingSample TimedSample(
    std::int64_t steady_ns,
    std::int64_t process_cpu_ns,
    std::int64_t thread_cpu_ns)
{
    EventTimingSample sample{};
    sample.valid_mask = EventTimingSample::bit(EventTimingCapture::Monotonic) | EventTimingSample::bit(EventTimingCapture::ProcessCpu) | EventTimingSample::bit(EventTimingCapture::ThreadCpu);
    sample.steady_time_ns = steady_ns;
    sample.process_cpu_ns = process_cpu_ns;
    sample.thread_cpu_ns = thread_cpu_ns;
    return sample;
}

EventTimingSample NextSample(EventTimingCapture mask)
{
    assert((EventTimingSample::bit(mask) & EventTimingSample::bit(EventTimingCapture::Monotonic)) != 0u);
    assert((EventTimingSample::bit(mask) & EventTimingSample::bit(EventTimingCapture::ProcessCpu)) != 0u);
    assert((EventTimingSample::bit(mask) & EventTimingSample::bit(EventTimingCapture::ThreadCpu)) != 0u);
    assert(sequence_index < sequence.size());
    return sequence[sequence_index++];
}

void SetSequence(std::initializer_list<EventTimingSample> samples)
{
    sequence.assign(samples.begin(), samples.end());
    sequence_index = 0;
}

fs::path TestDirectory()
{
    return fs::temp_directory_path() /
           ("EventTimingRecorderTest-" + std::to_string(static_cast<long long>(::getpid())));
}

std::vector<std::string> Split(std::string_view text, char delimiter)
{
    std::vector<std::string> fields;
    std::size_t              start = 0;
    for (std::size_t index = 0; index <= text.size(); ++index)
    {
        if (index == text.size() || text[index] == delimiter)
        {
            fields.emplace_back(text.substr(start, index - start));
            start = index + 1;
        }
    }
    return fields;
}

std::unordered_map<std::string, std::string> ReadSingleRow(const fs::path& path)
{
    std::ifstream input(path);
    assert(input);
    std::string header;
    std::string values;
    const bool  read_header = static_cast<bool>(std::getline(input, header));
    const bool  read_values = static_cast<bool>(std::getline(input, values));
    assert(read_header);
    assert(read_values);
    std::string extra;
    const bool  read_extra = static_cast<bool>(std::getline(input, extra));
    assert(!read_extra);

    const auto names = Split(header, ',');
    const auto fields = Split(values, ',');
    assert(names.size() == fields.size());
    std::unordered_map<std::string, std::string> row;
    for (std::size_t index = 0; index < names.size(); ++index)
        row.emplace(names[index], fields[index]);
    return row;
}

void AssertNear(double actual, double expected)
{
    assert(std::abs(actual - expected) < 1e-9);
}

EventTimingMetadata Metadata()
{
    EventTimingMetadata metadata{};
    metadata.geometry = "geom\"etry\\name\nline";
    metadata.config = "config=value";
    metadata.macro = "/run/beamOn 1";
    metadata.simphony_version = "test-version";
    metadata.geant4_version = "11.test";
    metadata.primary_particle = "mu-";
    metadata.primary_momentum_gev_c = 5.0;
    metadata.primary_multiplicity = 100000;
    metadata.random_seed = 12345;
    metadata.gpu_name = "GPU \"test\"";
    metadata.gpu_device_id = 2;
    metadata.gpu_memory_bytes = 24'000'000'000ull;
    metadata.cuda_driver_version = 12040;
    metadata.cuda_runtime_version = 12030;
    return metadata;
}

void CompleteOneEvent(EventTimingRecorder& recorder)
{
    recorder.BeginRun();
    recorder.BeginEvent(0);
    recorder.SubmitGpu(4, 500);
    recorder.BeginGpu();
    recorder.EndGpu();
    recorder.EndEvent(25, 7);
}

void TestGoldenCsvAndManifest(const fs::path& directory)
{
    const fs::path output = directory / "events.csv";
    SetSequence({
        TimedSample(1'000'000'000, 100'000, 50'000),
        TimedSample(1'100'000'000, 1'000'000, 500'000),
        TimedSample(1'300'000'000, 1'300'000, 650'000),
        TimedSample(1'350'000'000, 1'350'000, 700'000),
        TimedSample(1'750'000'000, 1'600'000, 850'000),
        TimedSample(2'000'000'000, 1'900'000, 1'050'000),
    });

    EventTimingRecorder recorder(output, &NextSample);
    CompleteOneEvent(recorder);
    recorder.Write(Metadata());
    assert(sequence_index == sequence.size());

    std::ifstream csv(output);
    assert(csv);
    std::string header;
    const bool  read_header = static_cast<bool>(std::getline(csv, header));
    assert(read_header);
    const std::string expected_header =
        "scenario,dispatch_mode,event_id,start_time,end_time,cpu_start_time,cpu_end_time,"
        "cpu_pre_start_time,cpu_pre_end_time,gpu_submit_time,gpu_start_time,gpu_end_time,"
        "gpu_wait_start_time,gpu_wait_end_time,cpu_post_start_time,cpu_post_end_time,"
        "start_offset_s,end_offset_s,cpu_start_time_offset_s,cpu_end_time_offset_s,"
        "cpu_pre_start_time_offset_s,cpu_pre_end_time_offset_s,gpu_submit_time_offset_s,"
        "gpu_start_time_offset_s,gpu_end_time_offset_s,gpu_wait_start_time_offset_s,"
        "gpu_wait_end_time_offset_s,cpu_post_start_time_offset_s,cpu_post_end_time_offset_s,"
        "runtime_s,cpu_pre_runtime_s,cpu_post_runtime_s,cpu_runtime_s,gpu_queue_delay_s,"
        "gpu_runtime_s,gpu_wait_runtime_s,primary_particle,primary_momentum_gev_c,"
        "primary_multiplicity,num_gensteps,num_photons,num_gpu_hits,num_g4_hits,"
        "process_cpu_pre_s,process_cpu_post_s,process_cpu_s,thread_cpu_pre_s,"
        "thread_cpu_post_s,thread_cpu_s";
    assert(header == expected_header);

    const auto row = ReadSingleRow(output);
    assert(row.at("scenario") == "simg4ox_blocking");
    assert(row.at("dispatch_mode") == "blocking");
    assert(row.at("event_id") == "0");
    assert(row.at("primary_particle") == "mu-");
    assert(row.at("num_gensteps") == "4");
    assert(row.at("num_photons") == "500");
    assert(row.at("num_gpu_hits") == "25");
    assert(row.at("num_g4_hits") == "7");

    const auto value = [&row](const char* name) { return std::stod(row.at(name)); };
    AssertNear(value("cpu_pre_runtime_s"),
               value("cpu_pre_end_time_offset_s") - value("cpu_pre_start_time_offset_s"));
    AssertNear(value("gpu_queue_delay_s"),
               value("gpu_start_time_offset_s") - value("gpu_submit_time_offset_s"));
    AssertNear(value("gpu_runtime_s"),
               value("gpu_end_time_offset_s") - value("gpu_start_time_offset_s"));
    AssertNear(value("cpu_post_runtime_s"),
               value("cpu_post_end_time_offset_s") - value("cpu_post_start_time_offset_s"));
    AssertNear(value("cpu_runtime_s"),
               value("cpu_pre_runtime_s") + value("cpu_post_runtime_s"));
    AssertNear(value("gpu_wait_runtime_s"),
               value("gpu_queue_delay_s") + value("gpu_runtime_s"));
    AssertNear(value("process_cpu_pre_s"), 0.0003);
    AssertNear(value("process_cpu_post_s"), 0.0003);
    AssertNear(value("thread_cpu_pre_s"), 0.00015);
    AssertNear(value("thread_cpu_post_s"), 0.0002);

    const fs::path manifest_path = directory / "events.manifest.json";
    std::ifstream  manifest_input(manifest_path);
    assert(manifest_input);
    const nlohmann::json      manifest = nlohmann::json::parse(manifest_input);
    const EventTimingMetadata metadata = Metadata();
    assert(manifest.at("schema_version") == 2);
    assert(manifest.at("application") == "simg4ox");
    assert(manifest.at("dispatch_mode") == "blocking");
    assert(manifest.at("event_count") == 1);
    assert(manifest.at("geometry") == metadata.geometry);
    assert(manifest.at("gpu_name") == metadata.gpu_name);
    assert(manifest.at("gpu_device_id") == metadata.gpu_device_id);
    assert(manifest.at("gpu_memory_bytes") == metadata.gpu_memory_bytes);
    assert(manifest.at("cuda_driver_version") == metadata.cuda_driver_version);
    assert(manifest.at("cuda_runtime_version") == metadata.cuda_runtime_version);

    for (const fs::directory_entry& entry : fs::directory_iterator(directory))
        assert(entry.path().filename().string().find(".tmp.") == std::string::npos);
}

void ExpectLogicError(
    const fs::path&                                  output,
    const std::function<void(EventTimingRecorder&)>& action)
{
    EventTimingRecorder recorder(output);
    recorder.BeginRun();
    bool threw = false;
    try
    {
        action(recorder);
    }
    catch (const std::logic_error& error)
    {
        threw = std::string(error.what()).find("EventTimingRecorder") != std::string::npos;
    }
    assert(threw);
    assert(!fs::exists(output));
    fs::path manifest = output;
    manifest.replace_extension(".manifest.json");
    assert(!fs::exists(manifest));
}

void TestInvalidTransitions(const fs::path& directory)
{
    int        index = 0;
    const auto path = [&] { return directory / ("invalid-" + std::to_string(index++) + ".csv"); };

    ExpectLogicError(path(), [](EventTimingRecorder& recorder) { recorder.SubmitGpu(1, 2); });
    ExpectLogicError(path(), [](EventTimingRecorder& recorder) { recorder.BeginGpu(); });
    ExpectLogicError(path(), [](EventTimingRecorder& recorder) { recorder.EndGpu(); });
    ExpectLogicError(path(), [](EventTimingRecorder& recorder) { recorder.EndEvent(1, 2); });
    ExpectLogicError(path(), [](EventTimingRecorder& recorder) {
        recorder.BeginEvent(0);
        recorder.BeginEvent(1);
    });
    ExpectLogicError(path(), [](EventTimingRecorder& recorder) {
        recorder.BeginEvent(0);
        recorder.BeginGpu();
    });
    ExpectLogicError(path(), [](EventTimingRecorder& recorder) {
        recorder.BeginEvent(0);
        recorder.SubmitGpu(1, 2);
        recorder.EndGpu();
    });
    ExpectLogicError(path(), [](EventTimingRecorder& recorder) {
        recorder.BeginEvent(0);
        recorder.SubmitGpu(1, 2);
        recorder.BeginGpu();
        recorder.EndEvent(1, 2);
    });
    ExpectLogicError(path(), [](EventTimingRecorder& recorder) {
        recorder.BeginEvent(0);
        recorder.Write(Metadata());
    });
    ExpectLogicError(path(), [](EventTimingRecorder& recorder) {
        recorder.BeginEvent(0);
        recorder.BeginRun();
    });

    EventTimingRecorder disabled;
    disabled.SubmitGpu(1, 2);
    disabled.BeginGpu();
    disabled.EndGpu();
    disabled.EndEvent(1, 2);
    disabled.Write(Metadata());
}

void TestClockInversion(const fs::path& directory)
{
    SetSequence({
        TimedSample(100, 0, 0),
        TimedSample(200, 0, 0),
        TimedSample(150, 0, 0),
    });
    const fs::path      output = directory / "inversion.csv";
    EventTimingRecorder recorder(output, &NextSample);
    recorder.BeginRun();
    recorder.BeginEvent(3);
    bool threw = false;
    try
    {
        recorder.SubmitGpu(1, 2);
    }
    catch (const std::logic_error&)
    {
        threw = true;
    }
    assert(threw);
    assert(!fs::exists(output));
}

void TestIoFailure(const fs::path& directory)
{
    const fs::path regular_file = directory / "not-a-directory";
    {
        std::ofstream output(regular_file);
        output << "file";
    }
    const fs::path      destination = regular_file / "events.csv";
    EventTimingRecorder recorder(destination);
    CompleteOneEvent(recorder);

    bool threw = false;
    try
    {
        recorder.Write(Metadata());
    }
    catch (const std::runtime_error& error)
    {
        threw = std::string(error.what()).find(destination.string()) != std::string::npos;
    }
    assert(threw);
    assert(!fs::exists(destination));
}
} // namespace

int main()
{
    const fs::path directory = TestDirectory();
    fs::remove_all(directory);
    fs::create_directories(directory);

    TestGoldenCsvAndManifest(directory);
    TestInvalidTransitions(directory);
    TestClockInversion(directory);
    TestIoFailure(directory);

    fs::remove_all(directory);
    return 0;
}
