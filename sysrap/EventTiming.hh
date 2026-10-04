/**
 * @file EventTiming.hh
 * Defines timestamp samples, process-wide lifecycle profiling, and per-event
 * CPU/GPU timeline recording.
 */

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

/**
 * Selects the measurements requested from EventTimingSample::Capture().
 *
 * Values may be combined with operator|(). A monotonic timestamp is always
 * captured, even when Monotonic is absent from the requested mask. Other
 * measurements are optional and their availability is recorded in
 * EventTimingSample::valid_mask.
 */
enum class EventTimingCapture : std::uint32_t
{
    /** Capture the monotonic timestamp in nanoseconds. */
    Monotonic = 1u << 0,
    /** Capture the Unix wall-clock timestamp in microseconds. */
    Wall = 1u << 1,
    /** Capture process CPU time in nanoseconds. */
    ProcessCpu = 1u << 2,
    /** Capture calling-thread CPU time in nanoseconds. */
    ThreadCpu = 1u << 3,
    /** Capture virtual-memory and resident-set sizes in KiB. */
    Memory = 1u << 4
};

/**
 * Combines two EventTimingCapture values into one capture mask.
 *
 * @param a first capture selection
 * @param b second capture selection
 * @return mask containing every selection present in either operand
 */
constexpr EventTimingCapture operator|(EventTimingCapture a, EventTimingCapture b)
{
    return static_cast<EventTimingCapture>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

/**
 * Stores one timestamp together with optional CPU and memory measurements.
 *
 * All clocks use signed integer counts to support difference calculations.
 * The valid_mask determines which values are usable; a stored zero does not
 * by itself indicate that a field was captured. Memory values use KiB, wall
 * time uses microseconds since the Unix epoch, and all other times use
 * nanoseconds.
 */
struct SYSRAP_API EventTimingSample
{
    /**
     * Memory-query callback used to make memory capture replaceable in tests.
     *
     * The callback writes virtual-memory and resident-set sizes in KiB and
     * returns zero on success.
     */
    using MemoryQuery = int (*)(std::int32_t&, std::int32_t&);

    /** Bit set containing the EventTimingCapture values available below. */
    std::uint32_t valid_mask{0};
    /** Monotonic timestamp in nanoseconds. */
    std::int64_t steady_time_ns{0};
    /** Unix wall-clock timestamp in microseconds. */
    std::int64_t wall_time_us{0};
    /** CPU time consumed by the process in nanoseconds. */
    std::int64_t process_cpu_ns{0};
    /** CPU time consumed by the calling thread in nanoseconds. */
    std::int64_t thread_cpu_ns{0};
    /** Process virtual-memory size in KiB. */
    std::int64_t vm_kb{0};
    /** Process resident-set size in KiB. */
    std::int64_t rss_kb{0};

    /**
     * Returns the valid-mask bit associated with a capture field.
     *
     * @param field capture field to convert
     * @return unsigned bit value for field
     */
    static constexpr std::uint32_t bit(EventTimingCapture field)
    {
        return static_cast<std::uint32_t>(field);
    }

    /**
     * Captures a sample using the process memory query supplied by SysRap.
     *
     * Monotonic time is unconditional. A failed optional clock or memory
     * query leaves the corresponding validity bit clear.
     *
     * @param mask optional measurements to request
     * @return captured sample and its validity mask
     */
    static EventTimingSample Capture(EventTimingCapture mask);
    /**
     * Captures a sample using a caller-provided memory query.
     *
     * This overload is primarily useful for deterministic testing. The query
     * is consulted only when Memory is requested. A null or failing query
     * leaves both memory fields invalid.
     *
     * @param mask optional measurements to request
     * @param query callback that obtains VM and RSS values in KiB
     * @return captured sample and its validity mask
     */
    static EventTimingSample Capture(EventTimingCapture mask, MemoryQuery query);

    /**
     * Tests whether a measurement is valid in this sample.
     *
     * @param field measurement to test
     * @return true when field was captured successfully
     */
    bool has(EventTimingCapture field) const;
    /**
     * Serializes the sample as six comma-separated fields.
     *
     * The field order is wall time, monotonic time, process CPU time, thread
     * CPU time, VM, and RSS. Invalid optional fields are emitted empty.
     *
     * @return stable metadata representation of this sample
     * @throws std::logic_error if the monotonic timestamp is invalid
     */
    std::string serialize() const;
    /**
     * Parses the six-field representation produced by serialize().
     *
     * Wall, CPU, and memory fields may be empty. VM and RSS must either both
     * be present or both be empty.
     *
     * @param text serialized timing sample
     * @return parsed sample with validity bits reconstructed
     * @throws std::invalid_argument if the field count, an integer, or the
     * paired memory fields are invalid
     */
    static EventTimingSample parse(std::string_view text);
    /**
     * Tests whether text is a valid serialized timing sample.
     *
     * @param text candidate six-field representation
     * @return true when parse() accepts text
     */
    static bool looksLikeSerialized(std::string_view text);
    /** @return human-readable representation including field names */
    std::string desc() const;

    /**
     * Computes the signed monotonic interval from begin to end.
     *
     * @return end minus begin in nanoseconds
     * @throws std::logic_error if either sample lacks monotonic time
     */
    static std::int64_t elapsedNs(
        const EventTimingSample& begin,
        const EventTimingSample& end);
    /**
     * Computes the signed virtual-memory change from begin to end.
     *
     * @return end minus begin in KiB
     * @throws std::logic_error if either sample lacks memory measurements
     */
    static std::int64_t deltaVmKb(
        const EventTimingSample& begin,
        const EventTimingSample& end);
    /**
     * Computes the signed resident-set change from begin to end.
     *
     * @return end minus begin in KiB
     * @throws std::logic_error if either sample lacks memory measurements
     */
    static std::int64_t deltaRssKb(
        const EventTimingSample& begin,
        const EventTimingSample& end);

    /** @return true when all stored fields and validity bits are equal */
    bool operator==(const EventTimingSample&) const = default;
};

/** Represents one named record in the process-wide lifecycle profile. */
struct EventTimingProfileRecord
{
    /** Record name, optionally prefixed with the thread-local tag from Add(). */
    std::string name;
    /** Measurements associated with the named point. */
    EventTimingSample sample;
    /** Optional caller-defined annotation stored as one CSV field. */
    std::string metadata;
};

/** Selects whether profile output replaces or extends an existing CSV file. */
enum class EventTimingWriteMode
{
    /** Replace the destination with the current in-memory records. */
    Replace,
    /** Preserve existing file records, then append the in-memory records. */
    Append
};

/**
 * Collects named lifecycle samples in a process-wide profile.
 *
 * Profiling is explicitly enabled through Configure(). When disabled, Mark()
 * returns a monotonic-only sample without recording it, allowing callers to
 * retain inexpensive local duration measurements. Record insertion and
 * snapshots are synchronized. Tags are thread-local, while configuration and
 * the record collection are process-wide; Configure() is therefore expected
 * to run before worker threads begin recording.
 */
class SYSRAP_API EventTimingProfile
{
  public:
    /**
     * Configures process-wide lifecycle profiling.
     *
     * The output may contain exactly one printf-style integer conversion
     * (`%d`, `%i`, or `%u`) that Path() expands using path_index. Configuration
     * does not clear records already in memory.
     *
     * @param enabled whether subsequent marks are recorded
     * @param output destination path or indexed path pattern
     * @param path_index integer used to expand an indexed output path
     */
    static void Configure(
        bool                         enabled,
        const std::filesystem::path& output = "event_timing_profile.csv",
        int                          path_index = 0);
    /** @return true when process-wide profile recording is enabled */
    static bool Enabled();
    /**
     * Captures and optionally records a named lifecycle point.
     *
     * Enabled marks contain monotonic time, wall time, and process memory.
     * Disabled marks contain only monotonic time and are not added to the
     * process-wide record collection.
     *
     * @param name lifecycle-point name without the current thread tag
     * @param metadata optional annotation stored with the record
     * @return sample captured for this mark
     */
    static EventTimingSample Mark(
        std::string_view name,
        std::string_view metadata = {});
    /**
     * Captures and records a lifecycle point without the calling thread's tag.
     *
     * Any current tag is restored before returning so surrounding event marks
     * retain their prefix.
     *
     * @param name lifecycle-point name
     * @param metadata optional annotation stored with the record
     * @return sample captured for this mark
     */
    static EventTimingSample MarkUntagged(
        std::string_view name,
        std::string_view metadata = {});
    /**
     * Adds a caller-provided sample when profiling is enabled.
     *
     * @param name record name without the current thread tag
     * @param sample measurements to store
     * @param metadata optional annotation stored with the record
     */
    static void Add(
        std::string_view         name,
        const EventTimingSample& sample,
        std::string_view         metadata = {});

    /**
     * Sets the calling thread's prefix for subsequently added record names.
     *
     * The format must contain exactly one `%d`, `%i`, or `%u` conversion and
     * may specify zero padding, width, and integer precision.
     *
     * @param index value substituted into format
     * @param format printf-style integer format for the tag
     * @throws std::invalid_argument if format is unsupported or ambiguous
     */
    static void SetTag(int index, std::string_view format = "A%0.3d_");
    /** @return true when the calling thread has a nonempty tag */
    static bool HasTag();
    /** @return copy of the calling thread's current tag */
    static std::string Tag();
    /** Clears the calling thread's current tag. */
    static void UnsetTag();

    /**
     * Formats unsigned metadata values as a comma-separated key-value list.
     *
     * @param values ordered name/value pairs
     * @return text in `name=value,name=value` form
     */
    static std::string Annotation(
        std::initializer_list<std::pair<std::string_view, std::uint64_t>> values);
    /**
     * Returns the RSS change between the two most recent records.
     *
     * @return signed RSS change in KiB, or -1 when unavailable
     */
    static std::int64_t DeltaRssKb();
    /**
     * Returns the RSS change between the first and most recent records.
     *
     * @return signed RSS change in KiB, or -1 when unavailable
     */
    static std::int64_t RangeRssKb();

    /** Removes every in-memory profile record. */
    static void Clear();
    /** @return number of in-memory profile records */
    static std::size_t Size();
    /** @return synchronized copy of the in-memory profile records */
    static std::vector<EventTimingProfileRecord> Records();
    /** @return newline-delimited human-readable description of all records */
    static std::string Describe();

    /**
     * Serializes records as RFC 4180-compatible profile CSV.
     *
     * @param records ordered records to serialize
     * @return CSV text including the six-column header
     * @throws std::logic_error if a record lacks monotonic time
     */
    static std::string SerializeCsv(
        const std::vector<EventTimingProfileRecord>& records);
    /**
     * Parses profile CSV from an input stream.
     *
     * @param input stream positioned at the CSV header
     * @param source path reported in parse diagnostics
     * @return records in file order
     * @throws std::runtime_error if the header, quoting, or a row is invalid
     */
    static std::vector<EventTimingProfileRecord> ParseCsv(
        std::istream&                input,
        const std::filesystem::path& source);
    /**
     * Reads and parses a profile CSV file.
     *
     * @param path input profile path
     * @return records in file order
     * @throws std::runtime_error if the file cannot be opened or parsed
     */
    static std::vector<EventTimingProfileRecord> ReadFile(
        const std::filesystem::path& path);

    /**
     * Resolves the configured profile output path.
     *
     * @return literal configured path or indexed path expansion
     * @throws std::invalid_argument if the path is empty or its format is invalid
     */
    static std::filesystem::path Path();
    /**
     * Writes a snapshot of the in-memory records when profiling is enabled.
     *
     * Parent directories are created as needed. Replacement uses a uniquely
     * named temporary file followed by rename so readers never observe a
     * partially written CSV. Append mode first parses the existing file and
     * rewrites the combined record sequence.
     *
     * @param mode whether to replace or extend an existing profile
     * @throws std::invalid_argument if the configured path pattern is invalid
     * @throws std::runtime_error if directory creation, I/O, parsing, or rename fails
     */
    static void Write(EventTimingWriteMode mode = EventTimingWriteMode::Replace);
};

/**
 * Describes the run recorded by EventTimingRecorder.
 *
 * These values are written to the timeline manifest; primary-particle values
 * are also repeated in every event CSV row. Momentum is measured in GeV/c and
 * GPU memory in bytes.
 */
struct EventTimingMetadata
{
    /** Geometry selection used by the run. */
    std::string geometry;
    /** JSON configuration selection used by the run. */
    std::string config;
    /** Geant4 macro path or identifier used by the run. */
    std::string macro;
    /** Simphony version string. */
    std::string simphony_version;
    /** Geant4 version string. */
    std::string geant4_version;
    /** Name of the generated primary particle. */
    std::string primary_particle{"opticalphoton"};
    /** Primary momentum in GeV/c. */
    double primary_momentum_gev_c{0.0};
    /** Number of primary particles generated per event. */
    int primary_multiplicity{0};
    /** Random seed, or -1 when not reported. */
    long random_seed{-1};
    /** CUDA device name. */
    std::string gpu_name;
    /** CUDA device ordinal, or -1 when not reported. */
    int gpu_device_id{-1};
    /** Total device memory in bytes. */
    std::uint64_t gpu_memory_bytes{0};
    /** CUDA driver version encoded by the CUDA runtime API. */
    int cuda_driver_version{0};
    /** CUDA runtime version encoded by the CUDA runtime API. */
    int cuda_runtime_version{0};
};

/**
 * Records the blocking CPU/GPU timeline of a serial event stream.
 *
 * An empty output path disables the recorder and makes lifecycle methods
 * no-ops. An enabled recorder accepts one active event at a time in this
 * sequence:
 *
 * @code
 * BeginRun()
 * BeginEvent() -> SubmitGpu() -> BeginGpu() -> EndGpu() -> EndEvent()
 * @endcode
 *
 * The event sequence may repeat before Write(). Each boundary captures
 * monotonic, process-CPU, and thread-CPU time. This class is intentionally
 * single-threaded and validates both lifecycle order and nondecreasing
 * monotonic timestamps.
 */
class SYSRAP_API EventTimingRecorder
{
  public:
    /** Callback used to inject deterministic timing samples in tests. */
    using SampleProvider = EventTimingSample (*)(EventTimingCapture);

    /**
     * Constructs a recorder for one timeline output.
     *
     * @param output event CSV path; an empty path disables the recorder
     * @param provider optional replacement for EventTimingSample::Capture()
     */
    explicit EventTimingRecorder(
        std::filesystem::path output = {},
        SampleProvider        provider = nullptr);

    /** @return true when a nonempty output path enables recording */
    bool enabled() const;
    /** @return configured event CSV output path */
    const std::filesystem::path& output() const;
    /**
     * Starts a run, discarding any previously buffered event rows.
     *
     * @throws std::logic_error if an event is active
     */
    void BeginRun();
    /**
     * Starts CPU pre-processing for one event.
     *
     * @param event_id identifier written to the event CSV
     * @throws std::logic_error if BeginRun() was not called, another event is
     * active, or the monotonic sample precedes the run origin
     */
    void BeginEvent(int event_id);
    /**
     * Marks GPU submission and records the generated workload size.
     *
     * @param num_gensteps number of gensteps submitted for transport
     * @param num_photons number of optical photons submitted for transport
     * @throws std::logic_error unless CPU pre-processing is active or when the
     * timestamp precedes the event start
     */
    void SubmitGpu(std::int64_t num_gensteps, std::int64_t num_photons);
    /**
     * Marks the beginning of GPU execution after any queue delay.
     *
     * @throws std::logic_error unless GPU work has been submitted or when the
     * timestamp precedes submission
     */
    void BeginGpu();
    /**
     * Marks the end of GPU execution and beginning of CPU post-processing.
     *
     * @throws std::logic_error unless GPU execution is active or when the
     * timestamp precedes its start
     */
    void EndGpu();
    /**
     * Completes the active event and records output hit counts.
     *
     * @param num_gpu_hits number of hits returned by GPU optical transport
     * @param num_g4_hits number of hits recorded by Geant4
     * @throws std::logic_error unless CPU post-processing is active or when
     * the timestamp precedes GPU completion
     */
    void EndEvent(std::size_t num_gpu_hits, std::size_t num_g4_hits);
    /**
     * Writes all completed rows and the adjacent JSON manifest.
     *
     * The manifest path is derived by replacing the CSV extension with
     * `.manifest.json`. Parent directories are created as needed, and each
     * destination is prepared in a temporary file before replacement.
     *
     * @param metadata run metadata written to the manifest and event rows
     * @throws std::logic_error if an event is active
     * @throws std::runtime_error if directory creation or output I/O fails
     */
    void Write(const EventTimingMetadata& metadata) const;

  private:
    /** Internal lifecycle states for the single active event. */
    enum class State
    {
        /** No event is active. */
        Idle,
        /** Geant4 and CPU pre-processing are active. */
        CpuPre,
        /** GPU work is submitted but execution has not begun. */
        GpuQueued,
        /** GPU execution is active. */
        GpuRunning,
        /** GPU execution ended and CPU post-processing is active. */
        CpuPost
    };

    /** Captured boundaries and workload counts for one completed event. */
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
