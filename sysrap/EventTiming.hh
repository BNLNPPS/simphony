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
    Monotonic = 1u << 0,
    Wall = 1u << 1,
    ProcessCpu = 1u << 2,
    ThreadCpu = 1u << 3,
    Memory = 1u << 4
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
    std::int64_t  steady_time_ns{0};
    std::int64_t  wall_time_us{0};
    std::int64_t  process_cpu_ns{0};
    std::int64_t  thread_cpu_ns{0};
    std::int64_t  vm_kb{0};
    std::int64_t  rss_kb{0};

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
    std::string       name;
    EventTimingSample sample;
    std::string       metadata;
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
        std::string_view         name,
        const EventTimingSample& sample,
        std::string_view         metadata = {});

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
        std::istream&                input,
        const std::filesystem::path& source);
    static std::vector<EventTimingProfileRecord> ReadFile(
        const std::filesystem::path& path);

    static std::filesystem::path Path();
    static void Write(EventTimingWriteMode mode = EventTimingWriteMode::Replace);
};

struct EventTimingMetadata
{
    std::string   geometry;
    std::string   config;
    std::string   macro;
    std::string   simphony_version;
    std::string   geant4_version;
    std::string   primary_particle{"opticalphoton"};
    double        primary_momentum_gev_c{0.0};
    int           primary_multiplicity{0};
    long          random_seed{-1};
    std::string   gpu_name;
    int           gpu_device_id{-1};
    std::uint64_t gpu_memory_bytes{0};
    int           cuda_driver_version{0};
    int           cuda_runtime_version{0};
};

class SYSRAP_API EventTimingRecorder
{
  public:
    using SampleProvider = EventTimingSample (*)(EventTimingCapture);

    explicit EventTimingRecorder(
        std::filesystem::path output = {},
        SampleProvider        provider = nullptr);

    bool enabled() const;
    const std::filesystem::path& output() const;
    void BeginRun();
    void BeginEvent(int event_id);
    void SubmitGpu(std::int64_t num_gensteps, std::int64_t num_photons);
    void BeginGpu();
    void EndGpu();
    void EndEvent(std::size_t num_gpu_hits, std::size_t num_g4_hits);
    void Write(const EventTimingMetadata& metadata) const;

  private:
    enum class State
    {
        Idle,
        CpuPre,
        GpuQueued,
        GpuRunning,
        CpuPost
    };

    struct Row
    {
        int               event_id{-1};
        std::int64_t      num_gensteps{0};
        std::int64_t      num_photons{0};
        std::size_t       num_gpu_hits{0};
        std::size_t       num_g4_hits{0};
        EventTimingSample event_start;
        EventTimingSample gpu_submit;
        EventTimingSample gpu_start;
        EventTimingSample gpu_end;
        EventTimingSample event_end;
    };

    EventTimingSample Capture() const;
    static const char* StateName(State state);
    Row& ActiveRow();
    const Row& ActiveRow() const;
    void RequireState(State expected, std::string_view operation) const;
    void RequireOrdered(
        const EventTimingSample& begin,
        const EventTimingSample& end,
        std::string_view         operation) const;
    std::string SerializeCsv(const EventTimingMetadata& metadata) const;
    std::string SerializeManifest(const EventTimingMetadata& metadata) const;
    std::filesystem::path ManifestPath() const;

    std::filesystem::path output_;
    SampleProvider        provider_{nullptr};
    State                 state_{State::Idle};
    bool                  run_started_{false};
    int                   active_event_id_{-1};
    EventTimingSample     run_origin_;
    std::vector<Row>      rows_;
};
