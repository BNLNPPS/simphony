#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "EventTiming.hh"

namespace
{
namespace fs = std::filesystem;

EventTimingSample KnownSample(
    std::int64_t steady_ns,
    std::int64_t wall_us,
    std::int64_t vm_kb,
    std::int64_t rss_kb)
{
    EventTimingSample sample{};
    sample.valid_mask = EventTimingSample::bit(EventTimingCapture::Monotonic) | EventTimingSample::bit(EventTimingCapture::Wall) | EventTimingSample::bit(EventTimingCapture::Memory);
    sample.steady_time_ns = steady_ns;
    sample.wall_time_us = wall_us;
    sample.vm_kb = vm_kb;
    sample.rss_kb = rss_kb;
    return sample;
}

fs::path TestDirectory()
{
    return fs::temp_directory_path() /
           ("EventTimingProfileTest-" + std::to_string(static_cast<long long>(::getpid())));
}

void WriteText(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << text;
    output.close();
    assert(output);
}

void ExpectCsvError(const fs::path& path, const std::string& text, std::size_t line)
{
    WriteText(path, text);
    bool threw = false;
    try
    {
        (void)EventTimingProfile::ReadFile(path);
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        threw = message.find(path.string()) != std::string::npos && message.find("line " + std::to_string(line)) != std::string::npos;
    }
    assert(threw);
}

void ExpectInvalidPath(const char* pattern)
{
    setenv("EventTiming__PROFILE_PATH", pattern, 1);
    setenv("EventTiming__PROFILE_PATH_INDEX", "12", 1);
    bool threw = false;
    try
    {
        (void)EventTimingProfile::Path();
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);
}

void TestDisabledFastPath(const fs::path& directory)
{
    unsetenv("EventTiming__PROFILE");
    unsetenv("EventTiming__PROFILE_PATH_INDEX");
    const fs::path output = directory / "disabled.csv";
    setenv("EventTiming__PROFILE_PATH", output.c_str(), 1);

    EventTimingProfile::Clear();
    EventTimingProfile::SetTag(4);
    const EventTimingSample sample = EventTimingProfile::Mark("disabled");
    EventTimingProfile::Write();
    EventTimingProfile::UnsetTag();

    assert(sample.has(EventTimingCapture::Monotonic));
    assert(!sample.has(EventTimingCapture::Wall));
    assert(!sample.has(EventTimingCapture::Memory));
    assert(EventTimingProfile::Size() == 0u);
    assert(!fs::exists(output));
}

void TestCollectionTagsAndRss()
{
    setenv("EventTiming__PROFILE", "1", 1);
    EventTimingProfile::Clear();
    EventTimingProfile::UnsetTag();

    const EventTimingSample first = KnownSample(100, 1000, 10000, 5000);
    const EventTimingSample second = KnownSample(200, 1100, 10030, 5020);

    EventTimingProfile::SetTag(7, "A%0.3d_");
    assert(EventTimingProfile::HasTag());
    assert(EventTimingProfile::Tag() == "A007_");
    EventTimingProfile::Add("head", first, "first");
    EventTimingProfile::Add("tail", second, "second");

    const auto records = EventTimingProfile::Records();
    assert(records.size() == 2u);
    assert(records[0].name == "A007_head");
    assert(records[0].metadata == "first");
    assert(records[1].name == "A007_tail");
    assert(EventTimingProfile::DeltaRssKb() == 20);
    assert(EventTimingProfile::RangeRssKb() == 20);
    assert(EventTimingProfile::Describe().find("A007_tail") != std::string::npos);
    assert(EventTimingProfile::Annotation({{"slice", 2}, {"max_slot_M", 16}}) == "slice=2,max_slot_M=16");

    std::atomic<bool> child_has_tag{false};
    std::thread       child([&child_has_tag] {
        child_has_tag = EventTimingProfile::HasTag();
        EventTimingProfile::SetTag(3, "T%02d_");
        assert(EventTimingProfile::Tag() == "T03_");
        EventTimingProfile::UnsetTag();
    });
    child.join();
    assert(!child_has_tag);
    assert(EventTimingProfile::Tag() == "A007_");
    EventTimingProfile::UnsetTag();
}

void TestCsvRoundTripAndErrors(const fs::path& directory)
{
    const std::string              annotation = "slice=2,\"quoted\"\nsecond line";
    const EventTimingProfileRecord record{
        "A000_QSim__simulate_LEND",
        KnownSample(1'234'567, 1'760'000'000'000'000, 4000, 2000),
        annotation,
    };

    const std::string  csv = EventTimingProfile::SerializeCsv({record});
    std::istringstream input(csv);
    const auto         parsed = EventTimingProfile::ParseCsv(input, "round-trip.csv");
    assert(parsed.size() == 1u);
    assert(parsed[0].name == record.name);
    assert(parsed[0].sample == record.sample);
    assert(parsed[0].metadata == annotation);

    ExpectCsvError(
        directory / "unterminated.csv",
        "name,wall_time_us,steady_time_ns,vm_kb,rss_kb,metadata\n"
        "mark,1,2,3,4,\"unterminated\n",
        2);
    ExpectCsvError(
        directory / "non-integer.csv",
        "name,wall_time_us,steady_time_ns,vm_kb,rss_kb,metadata\n"
        "mark,1,nope,3,4,\n",
        2);
    ExpectCsvError(
        directory / "bad-header.csv",
        "wrong,wall_time_us,steady_time_ns,vm_kb,rss_kb,metadata\n",
        1);
    ExpectCsvError(
        directory / "wrong-columns.csv",
        "name,wall_time_us,steady_time_ns,vm_kb,rss_kb,metadata\n"
        "mark,1,2,3,4\n",
        2);
}

void TestSnapshotsAndPaths(const fs::path& directory)
{
    setenv("EventTiming__PROFILE", "1", 1);
    unsetenv("EventTiming__PROFILE_PATH_INDEX");
    const fs::path output = directory / "profile.csv";
    setenv("EventTiming__PROFILE_PATH", output.c_str(), 1);

    EventTimingProfile::Clear();
    EventTimingProfile::Add("first", KnownSample(10, 100, 1000, 500));
    EventTimingProfile::Write(EventTimingWriteMode::Replace);

    EventTimingProfile::Clear();
    EventTimingProfile::Add("second", KnownSample(20, 200, 1100, 510));
    EventTimingProfile::Write(EventTimingWriteMode::Replace);
    auto records = EventTimingProfile::ReadFile(output);
    assert(records.size() == 1u);
    assert(records[0].name == "second");

    EventTimingProfile::Clear();
    EventTimingProfile::Add("third", KnownSample(30, 300, 1200, 520));
    EventTimingProfile::Write(EventTimingWriteMode::Append);
    records = EventTimingProfile::ReadFile(output);
    assert(records.size() == 2u);
    assert(records[0].name == "second");
    assert(records[1].name == "third");

    for (const fs::directory_entry& entry : fs::directory_iterator(directory))
        assert(entry.path().filename().string().find(".tmp.") == std::string::npos);

    const fs::path indexed = directory / "profile_%05d.csv";
    setenv("EventTiming__PROFILE_PATH", indexed.c_str(), 1);
    setenv("EventTiming__PROFILE_PATH_INDEX", "12", 1);
    assert(EventTimingProfile::Path() == directory / "profile_00012.csv");

    ExpectInvalidPath("profile_%s.csv");
    ExpectInvalidPath("profile_%n.csv");
    ExpectInvalidPath("profile_%d_%d.csv");
    ExpectInvalidPath("profile_%");
    ExpectInvalidPath("profile_%*d.csv");
    setenv("EventTiming__PROFILE_PATH_INDEX", "not-an-integer", 1);
    setenv("EventTiming__PROFILE_PATH", "profile_%d.csv", 1);
    bool bad_index_threw = false;
    try
    {
        (void)EventTimingProfile::Path();
    }
    catch (const std::invalid_argument&)
    {
        bad_index_threw = true;
    }
    assert(bad_index_threw);
}

void TestConcurrentMarksAndSnapshots()
{
    setenv("EventTiming__PROFILE", "1", 1);
    EventTimingProfile::Clear();
    EventTimingProfile::UnsetTag();

    std::atomic<bool>        start{false};
    std::atomic<int>         complete{0};
    std::vector<std::thread> threads;
    for (int thread_index = 0; thread_index < 4; ++thread_index)
    {
        threads.emplace_back([thread_index, &start, &complete] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            EventTimingProfile::SetTag(thread_index, "T%02d_");
            for (int record_index = 0; record_index < 100; ++record_index)
            {
                EventTimingProfile::Add(
                    "item",
                    KnownSample(
                        thread_index * 1000 + record_index,
                        10000 + thread_index * 1000 + record_index,
                        2000,
                        1000));
            }
            EventTimingProfile::UnsetTag();
            complete.fetch_add(1, std::memory_order_release);
        });
    }

    start.store(true, std::memory_order_release);
    while (complete.load(std::memory_order_acquire) != 4)
    {
        const auto         snapshot = EventTimingProfile::Records();
        const std::string  csv = EventTimingProfile::SerializeCsv(snapshot);
        std::istringstream input(csv);
        const auto         parsed = EventTimingProfile::ParseCsv(input, "snapshot.csv");
        assert(parsed.size() == snapshot.size());
    }
    for (std::thread& thread : threads)
        thread.join();

    const auto records = EventTimingProfile::Records();
    assert(records.size() == 400u);
    std::set<std::string> prefixes;
    for (const EventTimingProfileRecord& record : records)
    {
        assert(record.name.size() == 8u);
        assert(record.name.substr(0, 1) == "T");
        assert(record.name.substr(4) == "item");
        prefixes.insert(record.name.substr(0, 4));
    }
    assert(prefixes == std::set<std::string>({"T00_", "T01_", "T02_", "T03_"}));
    assert(!EventTimingProfile::HasTag());
}
} // namespace

int main()
{
    const fs::path directory = TestDirectory();
    fs::remove_all(directory);
    fs::create_directories(directory);

    TestDisabledFastPath(directory);
    TestCollectionTagsAndRss();
    TestCsvRoundTripAndErrors(directory);
    TestSnapshotsAndPaths(directory);
    TestConcurrentMarksAndSnapshots();

    EventTimingProfile::Clear();
    EventTimingProfile::UnsetTag();
    unsetenv("EventTiming__PROFILE");
    unsetenv("EventTiming__PROFILE_PATH");
    unsetenv("EventTiming__PROFILE_PATH_INDEX");
    fs::remove_all(directory);
    return 0;
}
