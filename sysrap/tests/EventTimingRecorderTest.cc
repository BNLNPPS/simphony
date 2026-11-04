// These contract checks must also execute in Release study builds.
#undef NDEBUG
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
        "thread_cpu_post_s,thread_cpu_s,gpu_release_time,gpu_release_time_offset_s,"
        "cpu_post_locked_runtime_s,cpu_post_unlocked_runtime_s,gpu_gate_runtime_s,worker_id,num_threads";
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

    AssertNear(value("gpu_release_time_offset_s"), value("gpu_end_time_offset_s"));
    AssertNear(value("cpu_post_locked_runtime_s"), 0.0);
    AssertNear(value("cpu_post_unlocked_runtime_s"), value("cpu_post_runtime_s"));
    AssertNear(value("gpu_gate_runtime_s"), value("gpu_runtime_s"));

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

void TestAbsoluteTimestampsRoundTripAtLongUptime(const fs::path& directory)
{
    constexpr std::int64_t origin_ns = 10'000'000'000'000'000ll;
    constexpr std::int64_t start_ns = origin_ns + 101;
    constexpr std::int64_t end_ns = origin_ns + 541;
    SetSequence({
        TimedSample(origin_ns, 0, 0),
        TimedSample(start_ns, 0, 0),
        TimedSample(origin_ns + 211, 0, 0),
        TimedSample(origin_ns + 307, 0, 0),
        TimedSample(origin_ns + 419, 0, 0),
        TimedSample(end_ns, 0, 0),
    });

    const fs::path      output = directory / "long-uptime.csv";
    EventTimingRecorder recorder(output, &NextSample);
    CompleteOneEvent(recorder);
    recorder.Write(Metadata());

    const auto row = ReadSingleRow(output);
    assert(std::stod(row.at("start_time")) == static_cast<double>(start_ns) / 1'000'000'000.0);
    assert(std::stod(row.at("end_time")) == static_cast<double>(end_ns) / 1'000'000'000.0);
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

void TestGpuReleaseBoundary(const fs::path& directory)
{
    SetSequence({
        TimedSample(1'000'000'000, 0, 0),
        TimedSample(1'100'000'000, 0, 0),
        TimedSample(1'300'000'000, 0, 0),
        TimedSample(1'350'000'000, 0, 0),
        TimedSample(1'750'000'000, 0, 0),
        TimedSample(1'900'000'000, 0, 0),
        TimedSample(2'000'000'000, 0, 0),
    });
    EventTimingRecorder recorder(directory / "release.csv", &NextSample);
    recorder.BeginRun();
    recorder.BeginEvent(0);
    recorder.SubmitGpu(4, 500);
    recorder.BeginGpu();
    recorder.EndGpu();
    recorder.ReleaseGpu();
    bool duplicate_rejected = false;
    try
    {
        recorder.ReleaseGpu();
    }
    catch (const std::logic_error&)
    {
        duplicate_rejected = true;
    }
    assert(duplicate_rejected);
    recorder.EndEvent(25, 7);
    recorder.Write(Metadata());
    const auto row = ReadSingleRow(directory / "release.csv");
    AssertNear(std::stod(row.at("gpu_release_time_offset_s")), 0.9);
    AssertNear(std::stod(row.at("cpu_post_locked_runtime_s")), 0.15);
    AssertNear(std::stod(row.at("cpu_post_unlocked_runtime_s")), 0.1);
    AssertNear(std::stod(row.at("gpu_gate_runtime_s")), 0.55);
    assert(sequence_index == sequence.size());
}

void TestGpuReleaseClockInversion(const fs::path& directory)
{
    SetSequence({TimedSample(100, 0, 0), TimedSample(200, 0, 0),
                 TimedSample(300, 0, 0), TimedSample(400, 0, 0),
                 TimedSample(500, 0, 0), TimedSample(450, 0, 0)});
    EventTimingRecorder recorder(directory / "release-inversion.csv", &NextSample);
    recorder.BeginRun();
    recorder.BeginEvent(0);
    recorder.SubmitGpu(1, 2);
    recorder.BeginGpu();
    recorder.EndGpu();
    bool rejected = false;
    try
    {
        recorder.ReleaseGpu();
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    assert(rejected);
}

void ExpectMergeError(const std::function<void()>& action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    assert(rejected);
}

void FinishWorkerEvent(EventTimingRecorder& recorder, int event_id)
{
    recorder.BeginEvent(event_id);
    recorder.SubmitGpu(1, 100);
    recorder.BeginGpu();
    recorder.EndGpu();
    recorder.ReleaseGpu();
    recorder.EndEvent(25, 7);
}

void TestSharedOriginWorkerMerge(const fs::path& directory)
{
    const auto output = directory / "merged.csv";
    SetSequence({TimedSample(1'000'000'000, 0, 0)});
    EventTimingRecorder master(output, &NextSample);
    master.BeginRun();
    EventTimingRecorder first(output, &NextSample), second(output, &NextSample);
    first.SetWorkerId(0);
    second.SetWorkerId(1);
    first.BeginRunFrom(master);
    second.BeginRunFrom(master);
    assert(sequence_index == sequence.size()); // Cloning must not sample a new origin.

    // Record event 1 first, while its CPU pre interval overlaps event 0.
    SetSequence({TimedSample(1'200'000'000, 0, 0), TimedSample(1'400'000'000, 2, 2),
                 TimedSample(1'950'000'000, 3, 3), TimedSample(2'200'000'000, 4, 4),
                 TimedSample(2'300'000'000, 5, 5), TimedSample(2'400'000'000, 6, 6)});
    FinishWorkerEvent(second, 1);
    SetSequence({TimedSample(1'100'000'000, 0, 0), TimedSample(1'300'000'000, 2, 2),
                 TimedSample(1'350'000'000, 3, 3), TimedSample(1'750'000'000, 4, 4),
                 TimedSample(1'900'000'000, 5, 5), TimedSample(2'000'000'000, 6, 6)});
    FinishWorkerEvent(first, 0);
    master.Merge(second);
    master.Merge(first);
    EventTimingMetadata metadata = Metadata();
    metadata.num_threads = 2;
    metadata.cpu_recorder_enabled = false;
    metadata.torch_seed = 0;
    master.Write(metadata);

    std::ifstream csv(output);
    std::string   header, line;
    assert(static_cast<bool>(std::getline(csv, header)));
    const auto                                                names = Split(header, ',');
    std::vector<std::unordered_map<std::string, std::string>> rows;
    while (std::getline(csv, line))
    {
        const auto values = Split(line, ',');
        assert(values.size() == names.size());
        std::unordered_map<std::string, std::string> row;
        for (std::size_t i = 0; i < names.size(); ++i)
            row.emplace(names[i], values[i]);
        rows.push_back(std::move(row));
    }
    assert(rows.size() == 2);
    for (int i = 0; i < 2; ++i)
    {
        assert(rows[i].at("event_id") == std::to_string(i));
        assert(rows[i].at("worker_id") == std::to_string(i));
        assert(rows[i].at("num_threads") == "2");
        assert(rows[i].at("dispatch_mode") == "synchronous");
        assert(rows[i].at("num_photons") == "100");
    }
    AssertNear(std::stod(rows[0].at("start_offset_s")), 0.1);
    AssertNear(std::stod(rows[1].at("start_offset_s")), 0.2);
    AssertNear(std::stod(rows[0].at("gpu_release_time_offset_s")), 0.9);
    AssertNear(std::stod(rows[1].at("gpu_start_time_offset_s")), 0.95);
    std::ifstream manifest_input(directory / "merged.manifest.json");
    const auto    manifest = nlohmann::json::parse(manifest_input);
    assert(manifest.at("event_count") == 2);
    assert(manifest.at("num_threads") == 2);
    assert(manifest.at("cpu_recorder_enabled") == false);
    assert(manifest.at("torch_seed") == 0);
    assert(manifest.at("run_origin_steady_ns") == 1'000'000'000);
    ExpectMergeError([&] { master.Merge(first); });
    ExpectMergeError([&] { first.BeginEvent(2); });
}

void TestWorkerMergeGuards(const fs::path& directory)
{
    const auto          output = directory / "merge-guards.csv";
    EventTimingRecorder master(output), worker(output), duplicate(output);
    ExpectMergeError([&] { worker.BeginRunFrom(master); });
    master.BeginRun();
    worker.BeginRunFrom(master);
    duplicate.BeginRunFrom(master);
    ExpectMergeError([&] { master.Merge(master); });
    worker.BeginEvent(0);
    ExpectMergeError([&] { master.Merge(worker); });
    worker.SubmitGpu(1, 100);
    worker.BeginGpu();
    worker.EndGpu();
    worker.ReleaseGpu();
    worker.EndEvent(25, 7);
    master.Merge(worker);
    FinishWorkerEvent(duplicate, 0);
    ExpectMergeError([&] { master.Merge(duplicate); });

    EventTimingRecorder foreign(output, &NextSample);
    SetSequence({TimedSample(1, 0, 0)});
    foreign.BeginRun();
    ExpectMergeError([&] { master.Merge(foreign); });
    ExpectMergeError([&] { master.SetWorkerId(3); });

    // Reusing the same worker for a later run must clear its merged marker.
    master.BeginRun();
    worker.BeginRunFrom(master);
    FinishWorkerEvent(worker, 0);
    master.Merge(worker);
    master.Write(Metadata());
    assert(ReadSingleRow(output).at("event_id") == "0");
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
    TestAbsoluteTimestampsRoundTripAtLongUptime(directory);
    TestInvalidTransitions(directory);
    TestClockInversion(directory);
    TestIoFailure(directory);
    TestGpuReleaseBoundary(directory);
    TestGpuReleaseClockInversion(directory);
    TestSharedOriginWorkerMerge(directory);
    TestWorkerMergeGuards(directory);

    fs::remove_all(directory);
    return 0;
}
