#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <istream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SYSRAP_API_EXPORT.hh"

enum class EventTimingCapture : std::uint32_t
{
    Monotonic  = 1u << 0,
    Wall       = 1u << 1,
    ProcessCpu = 1u << 2,
    ThreadCpu  = 1u << 3,
    Memory     = 1u << 4
};

constexpr EventTimingCapture operator|(EventTimingCapture a, EventTimingCapture b)
{
    return static_cast<EventTimingCapture>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

struct SYSRAP_API EventTimingSample
{
    using MemoryQuery = int (*)(std::int32_t&, std::int32_t&);

    std::uint32_t valid_mask{0};
    std::int64_t steady_time_ns{0};
    std::int64_t wall_time_us{0};
    std::int64_t process_cpu_ns{0};
    std::int64_t thread_cpu_ns{0};
    std::int64_t vm_kb{0};
    std::int64_t rss_kb{0};

    static constexpr std::uint32_t bit(EventTimingCapture field)
    {
        return static_cast<std::uint32_t>(field);
    }

    static EventTimingSample Capture(EventTimingCapture mask);
    static EventTimingSample Capture(EventTimingCapture mask, MemoryQuery query);

    bool has(EventTimingCapture field) const;
    std::string serialize() const;
    static EventTimingSample parse(std::string_view text);
    static bool looksLikeSerialized(std::string_view text);
    std::string desc() const;

    static std::int64_t elapsedNs(
        const EventTimingSample& begin,
        const EventTimingSample& end);
    static std::int64_t deltaVmKb(
        const EventTimingSample& begin,
        const EventTimingSample& end);
    static std::int64_t deltaRssKb(
        const EventTimingSample& begin,
        const EventTimingSample& end);

    bool operator==(const EventTimingSample&) const = default;
};

struct EventTimingProfileRecord
{
    std::string name;
    EventTimingSample sample;
    std::string metadata;
};

enum class EventTimingWriteMode
{
    Replace,
    Append
};

class SYSRAP_API EventTimingProfile
{
  public:
    static bool Enabled();
    static EventTimingSample Mark(
        std::string_view name,
        std::string_view metadata = {});
    static void Add(
        std::string_view name,
        const EventTimingSample& sample,
        std::string_view metadata = {});

    static void SetTag(int index, std::string_view format = "A%0.3d_");
    static bool HasTag();
    static std::string Tag();
    static void UnsetTag();

    static std::string Annotation(
        std::initializer_list<std::pair<std::string_view, std::uint64_t>> values);
    static std::int64_t DeltaRssKb();
    static std::int64_t RangeRssKb();

    static void Clear();
    static std::size_t Size();
    static std::vector<EventTimingProfileRecord> Records();
    static std::string Describe();

    static std::string SerializeCsv(
        const std::vector<EventTimingProfileRecord>& records);
    static std::vector<EventTimingProfileRecord> ParseCsv(
        std::istream& input,
        const std::filesystem::path& source);
    static std::vector<EventTimingProfileRecord> ReadFile(
        const std::filesystem::path& path);

    static std::filesystem::path Path();
    static void Write(EventTimingWriteMode mode = EventTimingWriteMode::Replace);
};
