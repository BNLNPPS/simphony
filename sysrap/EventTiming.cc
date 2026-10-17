/**
 * @file EventTiming.cc
 * Implements timing capture, lifecycle-profile CSV I/O, and blocking event
 * timeline serialization.
 */

#include "EventTiming.hh"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "NPU.hh"
#include "SMeta.hh"
#include "sproc.h"

namespace
{
std::atomic_flag           memory_warning_emitted = ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> temporary_sequence{0};
thread_local std::string   profile_tag;
std::atomic<bool>          profile_enabled{false};
std::filesystem::path      profile_output{"event_timing_profile.csv"};
int                        profile_path_index{0};

/**
 * Returns the mutex that protects the process-wide profile record collection.
 *
 * @return mutex shared by all record readers and writers
 */
std::mutex& ProfileMutex()
{
    static std::mutex mutex;
    return mutex;
}

/**
 * Returns the process-wide lifecycle record collection.
 *
 * Access must be protected by ProfileMutex(). Function-local construction
 * avoids static-initialization-order dependencies with instrumented callers.
 *
 * @return mutable profile record collection
 */
std::vector<EventTimingProfileRecord>& ProfileRecords()
{
    static std::vector<EventTimingProfileRecord> records;
    return records;
}

std::int64_t TimespecNs(const timespec& value)
{
    return static_cast<std::int64_t>(value.tv_sec) * 1'000'000'000ll + static_cast<std::int64_t>(value.tv_nsec);
}

/**
 * Parses one required signed-integer CSV field.
 *
 * @param field field contents without CSV quoting
 * @param name logical field name included in diagnostics
 * @return parsed signed integer
 * @throws std::invalid_argument if field is empty or not an integer
 */
std::int64_t ParseInteger(std::string_view field, std::string_view name)
{
    if (field.empty())
        throw std::invalid_argument("EventTimingSample missing " + std::string(name));

    std::int64_t value = 0;
    const char*  begin = field.data();
    const char*  end = begin + field.size();
    const auto   result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end)
        throw std::invalid_argument("EventTimingSample invalid " + std::string(name));
    return value;
}

std::string OptionalValue(
    const EventTimingSample& sample,
    EventTimingCapture       field,
    std::int64_t             value)
{
    return sample.has(field) ? std::to_string(value) : std::string{};
}

void Require(const EventTimingSample& sample, EventTimingCapture field, const char* operation)
{
    if (!sample.has(field))
        throw std::logic_error(std::string("EventTimingSample ") + operation + " requires captured fields");
}

std::string PadInteger(int index, int width, int precision, bool zero_fill, char conversion)
{
    if (conversion == 'u' && index < 0)
        throw std::invalid_argument("EventTiming unsigned index cannot be negative");

    const bool          negative = index < 0;
    const std::uint64_t magnitude = negative
                                        ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(index))
                                        : static_cast<std::uint64_t>(index);
    std::string         digits = std::to_string(magnitude);

    const int numeric_width = precision >= 0 ? precision : (zero_fill ? width : 0);
    if (numeric_width > static_cast<int>(digits.size()))
        digits.insert(0, static_cast<std::size_t>(numeric_width - digits.size()), '0');
    if (negative)
        digits.insert(digits.begin(), '-');

    if (precision < 0 && !zero_fill && width > static_cast<int>(digits.size()))
        digits.insert(0, static_cast<std::size_t>(width - digits.size()), ' ');
    return digits;
}

/**
 * Expands the supported printf-style integer conversion in a path or tag.
 *
 * The accepted subset is `%[0][width][.precision][d|i|u]`; `%%` emits a
 * literal percent sign. Exactly one integer conversion is required, and very
 * large widths and precisions are rejected before arithmetic can overflow.
 *
 * @param pattern path or tag pattern to expand
 * @param index integer substituted for the conversion
 * @return expanded text
 * @throws std::invalid_argument if the pattern is unsupported or ambiguous
 */
std::string FormatIndex(std::string_view pattern, int index)
{
    std::string result;
    int         conversions = 0;

    for (std::size_t cursor = 0; cursor < pattern.size();)
    {
        if (pattern[cursor] != '%')
        {
            result.push_back(pattern[cursor++]);
            continue;
        }

        ++cursor;
        if (cursor < pattern.size() && pattern[cursor] == '%')
        {
            result.push_back('%');
            ++cursor;
            continue;
        }
        if (conversions != 0)
            throw std::invalid_argument("EventTiming format must contain exactly one integer conversion");

        bool zero_fill = false;
        if (cursor < pattern.size() && pattern[cursor] == '0')
        {
            zero_fill = true;
            ++cursor;
        }

        int width = 0;
        while (cursor < pattern.size() && pattern[cursor] >= '0' && pattern[cursor] <= '9')
        {
            if (width > 10000)
                throw std::invalid_argument("EventTiming format width is too large");
            width = width * 10 + (pattern[cursor++] - '0');
        }

        int precision = -1;
        if (cursor < pattern.size() && pattern[cursor] == '.')
        {
            ++cursor;
            precision = 0;
            const std::size_t precision_start = cursor;
            while (cursor < pattern.size() && pattern[cursor] >= '0' && pattern[cursor] <= '9')
            {
                if (precision > 10000)
                    throw std::invalid_argument("EventTiming format precision is too large");
                precision = precision * 10 + (pattern[cursor++] - '0');
            }
            if (cursor == precision_start)
                throw std::invalid_argument("EventTiming format precision requires digits");
        }

        if (cursor >= pattern.size() || (pattern[cursor] != 'd' && pattern[cursor] != 'i' && pattern[cursor] != 'u'))
            throw std::invalid_argument("EventTiming format contains an unsupported conversion");

        const char conversion = pattern[cursor++];
        result += PadInteger(index, width, precision, zero_fill, conversion);
        ++conversions;
    }

    if (conversions != 1)
        throw std::invalid_argument("EventTiming format must contain exactly one integer conversion");
    return result;
}

/**
 * Escapes one value for RFC 4180-compatible CSV output.
 *
 * Fields containing a comma, quote, carriage return, or newline are quoted;
 * embedded quotes are doubled.
 *
 * @param value unescaped field value
 * @return CSV field suitable for direct insertion into a row
 */
std::string CsvField(std::string_view value)
{
    if (value.find_first_of(",\"\r\n") == std::string_view::npos)
        return std::string(value);

    std::string quoted;
    quoted.push_back('"');
    for (const char c : value)
    {
        if (c == '"')
            quoted.push_back('"');
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

/** Stores one parsed CSV record and its first physical source line. */
struct CsvRow
{
    /** Unescaped field values in source order. */
    std::vector<std::string> fields;
    /** One-based source line on which this logical record begins. */
    std::size_t line{1};
};

/**
 * Reports a CSV error with its source path and physical line number.
 *
 * @param source source name used in the diagnostic
 * @param line one-based physical line number
 * @param message description of the malformed input
 * @throws std::runtime_error unconditionally
 */
[[noreturn]] void CsvError(
    const std::filesystem::path& source,
    std::size_t                  line,
    std::string_view             message)
{
    throw std::runtime_error(
        source.string() + ": line " + std::to_string(line) + ": " + std::string(message));
}

/**
 * Parses RFC 4180-style records while preserving embedded quoted newlines.
 *
 * Both LF and CRLF record endings are accepted. Quotes may only begin an
 * empty field, doubled quotes are unescaped, and characters following a
 * closing quote are limited to a delimiter or record ending.
 *
 * @param input stream to consume through end-of-file
 * @param source source name used in diagnostics
 * @return parsed records with their starting line numbers
 * @throws std::runtime_error if quoting or field termination is malformed
 */
std::vector<CsvRow> ParseRows(
    std::istream&                input,
    const std::filesystem::path& source)
{
    std::vector<CsvRow>      rows;
    std::vector<std::string> fields;
    std::string              field;
    std::size_t              line = 1;
    std::size_t              record_line = 1;
    bool                     in_quotes = false;
    bool                     after_quote = false;
    bool                     row_has_data = false;

    auto finish_row = [&] {
        fields.push_back(field);
        field.clear();
        rows.push_back({fields, record_line});
        fields.clear();
        after_quote = false;
        row_has_data = false;
    };

    char c = '\0';
    while (input.get(c))
    {
        if (in_quotes)
        {
            if (c == '"')
            {
                if (input.peek() == '"')
                {
                    (void)input.get(c);
                    field.push_back('"');
                }
                else
                {
                    in_quotes = false;
                    after_quote = true;
                }
            }
            else
            {
                field.push_back(c);
                if (c == '\n')
                    ++line;
            }
            continue;
        }

        if (after_quote && c != ',' && c != '\r' && c != '\n')
            CsvError(source, record_line, "unexpected character after closing quote");

        if (c == '"')
        {
            if (!field.empty() || after_quote)
                CsvError(source, record_line, "quote must begin a field");
            in_quotes = true;
            row_has_data = true;
        }
        else if (c == ',')
        {
            fields.push_back(field);
            field.clear();
            after_quote = false;
            row_has_data = true;
        }
        else if (c == '\n' || c == '\r')
        {
            if (c == '\r' && input.peek() == '\n')
                (void)input.get(c);
            finish_row();
            ++line;
            record_line = line;
        }
        else
        {
            field.push_back(c);
            row_has_data = true;
        }
    }

    if (in_quotes)
        CsvError(source, record_line, "unterminated quoted field");
    if (after_quote || row_has_data || !fields.empty() || !field.empty())
        finish_row();
    return rows;
}

/**
 * Creates a process-locally unique sibling path for staged output.
 *
 * The path combines the destination, process identifier, and a process-local
 * atomic sequence number to distinguish concurrent writes in this process.
 *
 * @param destination final output path
 * @return temporary path in the same directory as destination
 */
std::filesystem::path TemporaryPath(const std::filesystem::path& destination)
{
    const std::uint64_t sequence = temporary_sequence.fetch_add(1);
    return destination.string() + ".tmp." + std::to_string(static_cast<long long>(::getpid())) + "." + std::to_string(sequence);
}

/**
 * Formats the current wall-clock time for the timeline manifest.
 *
 * @return UTC timestamp in `YYYY-MM-DDTHH:MM:SSZ` form
 */
std::string UtcNow()
{
    const std::time_t now = std::time(nullptr);
    std::tm           utc{};
    gmtime_r(&now, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

double Seconds(std::int64_t nanoseconds)
{
    return static_cast<double>(nanoseconds) * 1e-9;
}

/**
 * Computes a process or thread CPU-time interval in seconds.
 *
 * @param begin interval start
 * @param end interval end
 * @param field ProcessCpu or ThreadCpu
 * @return signed interval in seconds, or zero when either sample lacks field
 */
double CpuSeconds(
    const EventTimingSample& begin,
    const EventTimingSample& end,
    EventTimingCapture       field)
{
    if (!begin.has(field) || !end.has(field))
        return 0.0;
    const std::int64_t delta = field == EventTimingCapture::ProcessCpu
                                   ? end.process_cpu_ns - begin.process_cpu_ns
                                   : end.thread_cpu_ns - begin.thread_cpu_ns;
    return Seconds(delta);
}

/**
 * Writes, flushes, and closes one complete output file.
 *
 * @param path destination to truncate
 * @param content complete file contents
 * @throws std::runtime_error if opening, writing, flushing, or closing fails
 */
void WriteChecked(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Unable to open timing output: " + path.string());
    output << content;
    output.flush();
    if (!output)
        throw std::runtime_error("Unable to write timing output: " + path.string());
    output.close();
    if (!output)
        throw std::runtime_error("Unable to close timing output: " + path.string());
}

/**
 * Publishes a staged file by renaming it over its destination.
 *
 * @param temporary fully written temporary path
 * @param destination final output path
 * @throws std::runtime_error if the rename fails
 */
void ReplaceFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination)
{
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error)
        throw std::runtime_error(
            "Unable to replace timing output " + destination.string() + ": " + error.message());
}

} // namespace

EventTimingSample EventTimingSample::Capture(EventTimingCapture mask)
{
    return Capture(mask, &sproc::Query);
}

EventTimingSample EventTimingSample::Capture(EventTimingCapture mask, MemoryQuery query)
{
    EventTimingSample sample{};

    const auto steady_now = std::chrono::steady_clock::now().time_since_epoch();
    sample.steady_time_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(steady_now).count();
    sample.valid_mask |= bit(EventTimingCapture::Monotonic);

    if ((bit(mask) & bit(EventTimingCapture::Wall)) != 0u)
    {
        const auto wall_now = std::chrono::system_clock::now().time_since_epoch();
        sample.wall_time_us =
            std::chrono::duration_cast<std::chrono::microseconds>(wall_now).count();
        sample.valid_mask |= bit(EventTimingCapture::Wall);
    }

    if ((bit(mask) & bit(EventTimingCapture::ProcessCpu)) != 0u)
    {
        timespec value{};
        if (::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) == 0)
        {
            sample.process_cpu_ns = TimespecNs(value);
            sample.valid_mask |= bit(EventTimingCapture::ProcessCpu);
        }
    }

    if ((bit(mask) & bit(EventTimingCapture::ThreadCpu)) != 0u)
    {
        timespec value{};
        if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0)
        {
            sample.thread_cpu_ns = TimespecNs(value);
            sample.valid_mask |= bit(EventTimingCapture::ThreadCpu);
        }
    }

    if ((bit(mask) & bit(EventTimingCapture::Memory)) != 0u)
    {
        std::int32_t vm_kb = 0;
        std::int32_t rss_kb = 0;
        if (query != nullptr && query(vm_kb, rss_kb) == 0)
        {
            sample.vm_kb = vm_kb;
            sample.rss_kb = rss_kb;
            sample.valid_mask |= bit(EventTimingCapture::Memory);
        }
        else if (!memory_warning_emitted.test_and_set())
        {
            std::cerr << "EventTimingSample memory sample failed; continuing without VM/RSS\n";
        }
    }

    return sample;
}

bool EventTimingSample::has(EventTimingCapture field) const
{
    return (valid_mask & bit(field)) != 0u;
}

std::string EventTimingSample::serialize() const
{
    Require(*this, EventTimingCapture::Monotonic, "serialization");
    std::ostringstream out;
    out << OptionalValue(*this, EventTimingCapture::Wall, wall_time_us) << ','
        << steady_time_ns << ','
        << OptionalValue(*this, EventTimingCapture::ProcessCpu, process_cpu_ns) << ','
        << OptionalValue(*this, EventTimingCapture::ThreadCpu, thread_cpu_ns) << ','
        << OptionalValue(*this, EventTimingCapture::Memory, vm_kb) << ','
        << OptionalValue(*this, EventTimingCapture::Memory, rss_kb);
    return out.str();
}

EventTimingSample EventTimingSample::parse(std::string_view text)
{
    std::array<int64_t, 6> fields{};
    uint32_t               field_mask = 0;
    U::ParseEventTimingSample(fields, field_mask, text);

    EventTimingSample sample{};
    sample.steady_time_ns = fields[1];
    sample.valid_mask |= bit(EventTimingCapture::Monotonic);

    if ((field_mask & (1u << 0)) != 0u)
    {
        sample.wall_time_us = fields[0];
        sample.valid_mask |= bit(EventTimingCapture::Wall);
    }
    if ((field_mask & (1u << 2)) != 0u)
    {
        sample.process_cpu_ns = fields[2];
        sample.valid_mask |= bit(EventTimingCapture::ProcessCpu);
    }
    if ((field_mask & (1u << 3)) != 0u)
    {
        sample.thread_cpu_ns = fields[3];
        sample.valid_mask |= bit(EventTimingCapture::ThreadCpu);
    }
    if ((field_mask & (1u << 4)) != 0u)
    {
        sample.vm_kb = fields[4];
        sample.rss_kb = fields[5];
        sample.valid_mask |= bit(EventTimingCapture::Memory);
    }
    return sample;
}

bool EventTimingSample::looksLikeSerialized(std::string_view text)
{
    try
    {
        (void)parse(text);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::string EventTimingSample::desc() const
{
    std::ostringstream out;
    out << "EventTimingSample("
        << "wall_time_us=" << OptionalValue(*this, EventTimingCapture::Wall, wall_time_us)
        << ", steady_time_ns=" << steady_time_ns
        << ", process_cpu_ns=" << OptionalValue(*this, EventTimingCapture::ProcessCpu, process_cpu_ns)
        << ", thread_cpu_ns=" << OptionalValue(*this, EventTimingCapture::ThreadCpu, thread_cpu_ns)
        << ", vm_kb=" << OptionalValue(*this, EventTimingCapture::Memory, vm_kb)
        << ", rss_kb=" << OptionalValue(*this, EventTimingCapture::Memory, rss_kb)
        << ')';
    return out.str();
}

std::int64_t EventTimingSample::elapsedNs(
    const EventTimingSample& begin,
    const EventTimingSample& end)
{
    Require(begin, EventTimingCapture::Monotonic, "elapsedNs");
    Require(end, EventTimingCapture::Monotonic, "elapsedNs");
    return end.steady_time_ns - begin.steady_time_ns;
}

std::int64_t EventTimingSample::deltaVmKb(
    const EventTimingSample& begin,
    const EventTimingSample& end)
{
    Require(begin, EventTimingCapture::Memory, "deltaVmKb");
    Require(end, EventTimingCapture::Memory, "deltaVmKb");
    return end.vm_kb - begin.vm_kb;
}

std::int64_t EventTimingSample::deltaRssKb(
    const EventTimingSample& begin,
    const EventTimingSample& end)
{
    Require(begin, EventTimingCapture::Memory, "deltaRssKb");
    Require(end, EventTimingCapture::Memory, "deltaRssKb");
    return end.rss_kb - begin.rss_kb;
}

void EventTimingProfile::Configure(
    bool                         enabled,
    const std::filesystem::path& output,
    int                          path_index)
{
    profile_output = output;
    profile_path_index = path_index;
    profile_enabled.store(enabled, std::memory_order_release);
}

bool EventTimingProfile::Enabled()
{
    return profile_enabled.load(std::memory_order_acquire);
}

EventTimingSample EventTimingProfile::Mark(
    std::string_view name,
    std::string_view metadata)
{
    if (!Enabled())
        return EventTimingSample::Capture(EventTimingCapture::Monotonic);

    const EventTimingSample sample = EventTimingSample::Capture(
        EventTimingCapture::Monotonic | EventTimingCapture::Wall | EventTimingCapture::Memory);
    Add(name, sample, metadata);
    return sample;
}

EventTimingSample EventTimingProfile::MarkUntagged(
    std::string_view name,
    std::string_view metadata)
{
    std::string tag = std::move(profile_tag);
    profile_tag.clear();
    const EventTimingSample sample = Mark(name, metadata);
    profile_tag = std::move(tag);
    return sample;
}

void EventTimingProfile::Add(
    std::string_view         name,
    const EventTimingSample& sample,
    std::string_view         metadata)
{
    if (!Enabled())
        return;

    EventTimingProfileRecord record{
        profile_tag + std::string(name),
        sample,
        std::string(metadata),
    };
    std::lock_guard<std::mutex> lock(ProfileMutex());
    auto&                       records = ProfileRecords();
    const auto                  position = std::upper_bound(
        records.begin(),
        records.end(),
        record.sample.steady_time_ns,
        [](std::int64_t timestamp, const EventTimingProfileRecord& existing) {
            return timestamp < existing.sample.steady_time_ns;
        });
    records.insert(position, std::move(record));
}

void EventTimingProfile::SetTag(int index, std::string_view format)
{
    profile_tag = FormatIndex(format, index);
}

bool EventTimingProfile::HasTag()
{
    return !profile_tag.empty();
}

std::string EventTimingProfile::Tag()
{
    return profile_tag;
}

void EventTimingProfile::UnsetTag()
{
    profile_tag.clear();
}

std::string EventTimingProfile::Annotation(
    std::initializer_list<std::pair<std::string_view, std::uint64_t>> values)
{
    std::ostringstream out;
    bool               first = true;
    for (const auto& [name, value] : values)
    {
        if (!first)
            out << ',';
        out << name << '=' << value;
        first = false;
    }
    return out.str();
}

std::int64_t EventTimingProfile::DeltaRssKb()
{
    std::lock_guard<std::mutex> lock(ProfileMutex());
    const auto&                 records = ProfileRecords();
    if (records.size() < 2u)
        return -1;
    const auto& begin = records[records.size() - 2u].sample;
    const auto& end = records.back().sample;
    if (!begin.has(EventTimingCapture::Memory) || !end.has(EventTimingCapture::Memory))
        return -1;
    return EventTimingSample::deltaRssKb(begin, end);
}

std::int64_t EventTimingProfile::RangeRssKb()
{
    std::lock_guard<std::mutex> lock(ProfileMutex());
    const auto&                 records = ProfileRecords();
    if (records.empty())
        return -1;
    const auto& begin = records.front().sample;
    const auto& end = records.back().sample;
    if (!begin.has(EventTimingCapture::Memory) || !end.has(EventTimingCapture::Memory))
        return -1;
    return EventTimingSample::deltaRssKb(begin, end);
}

void EventTimingProfile::Clear()
{
    std::lock_guard<std::mutex> lock(ProfileMutex());
    ProfileRecords().clear();
}

std::size_t EventTimingProfile::Size()
{
    std::lock_guard<std::mutex> lock(ProfileMutex());
    return ProfileRecords().size();
}

std::vector<EventTimingProfileRecord> EventTimingProfile::Records()
{
    std::lock_guard<std::mutex> lock(ProfileMutex());
    return ProfileRecords();
}

std::string EventTimingProfile::Describe()
{
    std::ostringstream out;
    for (const EventTimingProfileRecord& record : Records())
    {
        out << record.name << ':' << record.sample.desc();
        if (!record.metadata.empty())
            out << " # " << record.metadata;
        out << '\n';
    }
    return out.str();
}

std::string EventTimingProfile::SerializeCsv(
    const std::vector<EventTimingProfileRecord>& records)
{
    std::ostringstream out;
    out << "name,wall_time_us,steady_time_ns,vm_kb,rss_kb,metadata\n";
    for (const EventTimingProfileRecord& record : records)
    {
        Require(record.sample, EventTimingCapture::Monotonic, "profile serialization");
        out << CsvField(record.name) << ','
            << OptionalValue(record.sample, EventTimingCapture::Wall, record.sample.wall_time_us) << ','
            << record.sample.steady_time_ns << ','
            << OptionalValue(record.sample, EventTimingCapture::Memory, record.sample.vm_kb) << ','
            << OptionalValue(record.sample, EventTimingCapture::Memory, record.sample.rss_kb) << ','
            << CsvField(record.metadata) << '\n';
    }
    return out.str();
}

std::vector<EventTimingProfileRecord> EventTimingProfile::ParseCsv(
    std::istream&                input,
    const std::filesystem::path& source)
{
    const std::vector<CsvRow>      rows = ParseRows(input, source);
    const std::vector<std::string> header = {
        "name", "wall_time_us", "steady_time_ns", "vm_kb", "rss_kb", "metadata"};
    if (rows.empty() || rows.front().fields != header)
        CsvError(source, 1, "expected EventTiming profile CSV header");

    std::vector<EventTimingProfileRecord> records;
    records.reserve(rows.size() - 1u);
    for (std::size_t row_index = 1; row_index < rows.size(); ++row_index)
    {
        const CsvRow& row = rows[row_index];
        if (row.fields.size() != header.size())
            CsvError(source, row.line, "expected six columns");

        try
        {
            EventTimingSample sample{};
            sample.steady_time_ns = ParseInteger(row.fields[2], "steady_time_ns");
            sample.valid_mask |= EventTimingSample::bit(EventTimingCapture::Monotonic);
            if (!row.fields[1].empty())
            {
                sample.wall_time_us = ParseInteger(row.fields[1], "wall_time_us");
                sample.valid_mask |= EventTimingSample::bit(EventTimingCapture::Wall);
            }
            if (row.fields[3].empty() != row.fields[4].empty())
                throw std::invalid_argument("both VM and RSS must be present");
            if (!row.fields[3].empty())
            {
                sample.vm_kb = ParseInteger(row.fields[3], "vm_kb");
                sample.rss_kb = ParseInteger(row.fields[4], "rss_kb");
                sample.valid_mask |= EventTimingSample::bit(EventTimingCapture::Memory);
            }
            records.push_back({row.fields[0], sample, row.fields[5]});
        }
        catch (const std::exception& error)
        {
            CsvError(source, row.line, error.what());
        }
    }
    return records;
}

std::vector<EventTimingProfileRecord> EventTimingProfile::ReadFile(
    const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Unable to open EventTiming profile: " + path.string());
    return ParseCsv(input, path);
}

std::filesystem::path EventTimingProfile::Path()
{
    const std::string pattern = profile_output.string();
    if (pattern.empty())
        throw std::invalid_argument("EventTiming profile path is empty");
    if (pattern.find('%') == std::string::npos)
        return pattern;

    return FormatIndex(pattern, profile_path_index);
}

void EventTimingProfile::Write(EventTimingWriteMode mode)
{
    if (!Enabled())
        return;

    const std::filesystem::path           destination = Path();
    std::vector<EventTimingProfileRecord> records = Records();
    if (mode == EventTimingWriteMode::Append && std::filesystem::exists(destination))
    {
        std::vector<EventTimingProfileRecord> existing = ReadFile(destination);
        existing.insert(existing.end(), records.begin(), records.end());
        records = std::move(existing);
    }

    const std::filesystem::path parent = destination.parent_path();
    if (!parent.empty())
    {
        std::error_code directory_error;
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error)
            throw std::runtime_error(
                "Unable to create EventTiming profile directory for " + destination.string() + ": " + directory_error.message());
    }

    const std::filesystem::path temporary = TemporaryPath(destination);
    try
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Unable to open EventTiming profile: " + temporary.string());
        output << SerializeCsv(records);
        output.flush();
        if (!output)
            throw std::runtime_error("Unable to write EventTiming profile: " + temporary.string());
        output.close();
        if (!output)
            throw std::runtime_error("Unable to close EventTiming profile: " + temporary.string());

        std::error_code rename_error;
        std::filesystem::rename(temporary, destination, rename_error);
        if (rename_error)
            throw std::runtime_error(
                "Unable to replace EventTiming profile " + destination.string() + ": " + rename_error.message());
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

EventTimingRecorder::EventTimingRecorder(
    std::filesystem::path output,
    SampleProvider        provider) :
    output_(std::move(output)),
    provider_(provider)
{
}

bool EventTimingRecorder::enabled() const
{
    return !output_.empty();
}

const std::filesystem::path& EventTimingRecorder::output() const
{
    return output_;
}

const char* EventTimingRecorder::StateName(State state)
{
    switch (state)
    {
    case State::Idle:
        return "idle";
    case State::CpuPre:
        return "cpu-pre";
    case State::GpuQueued:
        return "gpu-queued";
    case State::GpuRunning:
        return "gpu-running";
    case State::CpuPost:
        return "cpu-post";
    }
    return "unknown";
}

EventTimingSample EventTimingRecorder::Capture() const
{
    constexpr EventTimingCapture mask =
        EventTimingCapture::Monotonic | EventTimingCapture::ProcessCpu | EventTimingCapture::ThreadCpu;
    return provider_ == nullptr ? EventTimingSample::Capture(mask) : provider_(mask);
}

EventTimingRecorder::Row& EventTimingRecorder::ActiveRow()
{
    if (rows_.empty() || active_event_id_ < 0)
        throw std::logic_error("EventTimingRecorder has no active event");
    return rows_.back();
}

const EventTimingRecorder::Row& EventTimingRecorder::ActiveRow() const
{
    if (rows_.empty() || active_event_id_ < 0)
        throw std::logic_error("EventTimingRecorder has no active event");
    return rows_.back();
}

void EventTimingRecorder::RequireState(State expected, std::string_view operation) const
{
    if (state_ == expected)
        return;
    throw std::logic_error(
        "EventTimingRecorder event " + std::to_string(active_event_id_) + " cannot " + std::string(operation) + " in state " + StateName(state_) + "; expected " + StateName(expected));
}

void EventTimingRecorder::RequireOrdered(
    const EventTimingSample& begin,
    const EventTimingSample& end,
    std::string_view         operation) const
{
    if (EventTimingSample::elapsedNs(begin, end) >= 0)
        return;
    throw std::logic_error(
        "EventTimingRecorder event " + std::to_string(active_event_id_) + " monotonic clock moved backwards during " + std::string(operation));
}

void EventTimingRecorder::BeginRun()
{
    if (!enabled())
        return;
    RequireState(State::Idle, "begin run");
    if (active_event_id_ >= 0)
        throw std::logic_error("EventTimingRecorder cannot begin run with an active event");
    rows_.clear();
    run_origin_ = Capture();
    run_started_ = true;
}

void EventTimingRecorder::BeginEvent(int event_id)
{
    if (!enabled())
        return;
    RequireState(State::Idle, "begin event");
    if (!run_started_)
        throw std::logic_error("EventTimingRecorder BeginEvent requires BeginRun");

    Row row{};
    row.event_id = event_id;
    row.event_start = Capture();
    active_event_id_ = event_id;
    RequireOrdered(run_origin_, row.event_start, "event start");
    rows_.push_back(row);
    state_ = State::CpuPre;
}

void EventTimingRecorder::SubmitGpu(
    std::int64_t num_gensteps,
    std::int64_t num_photons)
{
    if (!enabled())
        return;
    RequireState(State::CpuPre, "submit GPU work");
    const EventTimingSample sample = Capture();
    Row&                    row = ActiveRow();
    RequireOrdered(row.event_start, sample, "GPU submission");
    row.num_gensteps = num_gensteps;
    row.num_photons = num_photons;
    row.gpu_submit = sample;
    state_ = State::GpuQueued;
}

void EventTimingRecorder::BeginGpu()
{
    if (!enabled())
        return;
    RequireState(State::GpuQueued, "begin GPU work");
    const EventTimingSample sample = Capture();
    Row&                    row = ActiveRow();
    RequireOrdered(row.gpu_submit, sample, "GPU start");
    row.gpu_start = sample;
    state_ = State::GpuRunning;
}

void EventTimingRecorder::EndGpu()
{
    if (!enabled())
        return;
    RequireState(State::GpuRunning, "end GPU work");
    const EventTimingSample sample = Capture();
    Row&                    row = ActiveRow();
    RequireOrdered(row.gpu_start, sample, "GPU end");
    row.gpu_end = sample;
    state_ = State::CpuPost;
}

void EventTimingRecorder::EndEvent(
    std::size_t num_gpu_hits,
    std::size_t num_g4_hits)
{
    if (!enabled())
        return;
    RequireState(State::CpuPost, "end event");
    const EventTimingSample sample = Capture();
    Row&                    row = ActiveRow();
    RequireOrdered(row.gpu_end, sample, "event end");
    row.num_gpu_hits = num_gpu_hits;
    row.num_g4_hits = num_g4_hits;
    row.event_end = sample;
    state_ = State::Idle;
    active_event_id_ = -1;
}

std::string EventTimingRecorder::SerializeCsv(const EventTimingMetadata& metadata) const
{
    std::ostringstream csv;
    csv << "scenario,dispatch_mode,event_id,"
           "start_time,end_time,cpu_start_time,cpu_end_time,cpu_pre_start_time,cpu_pre_end_time,"
           "gpu_submit_time,gpu_start_time,gpu_end_time,gpu_wait_start_time,gpu_wait_end_time,"
           "cpu_post_start_time,cpu_post_end_time,"
           "start_offset_s,end_offset_s,cpu_start_time_offset_s,cpu_end_time_offset_s,"
           "cpu_pre_start_time_offset_s,cpu_pre_end_time_offset_s,gpu_submit_time_offset_s,"
           "gpu_start_time_offset_s,gpu_end_time_offset_s,gpu_wait_start_time_offset_s,"
           "gpu_wait_end_time_offset_s,cpu_post_start_time_offset_s,"
           "cpu_post_end_time_offset_s,runtime_s,cpu_pre_runtime_s,cpu_post_runtime_s,cpu_runtime_s,"
           "gpu_queue_delay_s,gpu_runtime_s,gpu_wait_runtime_s,"
           "primary_particle,primary_momentum_gev_c,primary_multiplicity,"
           "num_gensteps,num_photons,num_gpu_hits,num_g4_hits,process_cpu_pre_s,process_cpu_post_s,"
           "process_cpu_s,thread_cpu_pre_s,thread_cpu_post_s,thread_cpu_s\n";

    csv << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const Row& row : rows_)
    {
        const auto absolute = [](const EventTimingSample& sample) {
            return Seconds(sample.steady_time_ns);
        };
        const auto offset = [this](const EventTimingSample& sample) {
            return Seconds(EventTimingSample::elapsedNs(run_origin_, sample));
        };
        const auto duration = [](const EventTimingSample& begin, const EventTimingSample& end) {
            return Seconds(EventTimingSample::elapsedNs(begin, end));
        };

        const double cpu_pre_runtime = duration(row.event_start, row.gpu_submit);
        const double cpu_post_runtime = duration(row.gpu_end, row.event_end);
        const double gpu_queue = duration(row.gpu_submit, row.gpu_start);
        const double gpu_runtime = duration(row.gpu_start, row.gpu_end);
        const double gpu_wait = duration(row.gpu_submit, row.gpu_end);
        const double process_pre = CpuSeconds(
            row.event_start, row.gpu_submit, EventTimingCapture::ProcessCpu);
        const double process_post = CpuSeconds(
            row.gpu_end, row.event_end, EventTimingCapture::ProcessCpu);
        const double thread_pre = CpuSeconds(
            row.event_start, row.gpu_submit, EventTimingCapture::ThreadCpu);
        const double thread_post = CpuSeconds(
            row.gpu_end, row.event_end, EventTimingCapture::ThreadCpu);

        csv << "simg4ox_blocking,blocking," << row.event_id << ','
            << absolute(row.event_start) << ',' << absolute(row.event_end) << ','
            << absolute(row.event_start) << ',' << absolute(row.event_end) << ','
            << absolute(row.event_start) << ',' << absolute(row.gpu_submit) << ','
            << absolute(row.gpu_submit) << ',' << absolute(row.gpu_start) << ','
            << absolute(row.gpu_end) << ',' << absolute(row.gpu_submit) << ','
            << absolute(row.gpu_end) << ',' << absolute(row.gpu_end) << ','
            << absolute(row.event_end) << ','
            << offset(row.event_start) << ',' << offset(row.event_end) << ','
            << offset(row.event_start) << ',' << offset(row.event_end) << ','
            << offset(row.event_start) << ',' << offset(row.gpu_submit) << ','
            << offset(row.gpu_submit) << ',' << offset(row.gpu_start) << ','
            << offset(row.gpu_end) << ',' << offset(row.gpu_submit) << ','
            << offset(row.gpu_end) << ',' << offset(row.gpu_end) << ','
            << offset(row.event_end) << ','
            << duration(row.event_start, row.event_end) << ','
            << cpu_pre_runtime << ',' << cpu_post_runtime << ','
            << cpu_pre_runtime + cpu_post_runtime << ','
            << gpu_queue << ',' << gpu_runtime << ',' << gpu_wait << ','
            << CsvField(metadata.primary_particle) << ','
            << metadata.primary_momentum_gev_c << ',' << metadata.primary_multiplicity << ','
            << row.num_gensteps << ',' << row.num_photons << ','
            << row.num_gpu_hits << ',' << row.num_g4_hits << ','
            << process_pre << ',' << process_post << ',' << process_pre + process_post << ','
            << thread_pre << ',' << thread_post << ',' << thread_pre + thread_post << '\n';
    }
    return csv.str();
}

std::string EventTimingRecorder::SerializeManifest(const EventTimingMetadata& metadata) const
{
    SMeta manifest;
    manifest.js["schema_version"] = 2;
    manifest.js["created_utc"] = UtcNow();
    manifest.js["application"] = "simg4ox";
    manifest.js["dispatch_mode"] = "blocking";
    manifest.js["clock"] = "std::chrono::steady_clock";
    manifest.js["geometry"] = metadata.geometry;
    manifest.js["config"] = metadata.config;
    manifest.js["macro"] = metadata.macro;
    manifest.js["simphony_version"] = metadata.simphony_version;
    manifest.js["geant4_version"] = metadata.geant4_version;
    manifest.js["primary_particle"] = metadata.primary_particle;
    manifest.js["primary_momentum_gev_c"] = metadata.primary_momentum_gev_c;
    manifest.js["primary_multiplicity"] = metadata.primary_multiplicity;
    manifest.js["random_seed"] = metadata.random_seed;
    manifest.js["gpu_name"] = metadata.gpu_name;
    manifest.js["gpu_device_id"] = metadata.gpu_device_id;
    manifest.js["gpu_memory_bytes"] = metadata.gpu_memory_bytes;
    manifest.js["cuda_driver_version"] = metadata.cuda_driver_version;
    manifest.js["cuda_runtime_version"] = metadata.cuda_runtime_version;
    manifest.js["event_count"] = rows_.size();
    return manifest.js.dump(2) + '\n';
}

std::filesystem::path EventTimingRecorder::ManifestPath() const
{
    std::filesystem::path manifest = output_;
    manifest.replace_extension(".manifest.json");
    return manifest;
}

void EventTimingRecorder::Write(const EventTimingMetadata& metadata) const
{
    if (!enabled())
        return;
    RequireState(State::Idle, "write output");
    if (active_event_id_ >= 0)
        throw std::logic_error("EventTimingRecorder cannot write with an active event");

    const std::filesystem::path manifest = ManifestPath();
    const std::filesystem::path parent = output_.parent_path();
    if (!parent.empty())
    {
        std::error_code directory_error;
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error)
            throw std::runtime_error(
                "Unable to create timing output directory for " + output_.string() + ": " + directory_error.message());
    }

    const std::filesystem::path csv_temporary = TemporaryPath(output_);
    const std::filesystem::path manifest_temporary = TemporaryPath(manifest);
    try
    {
        WriteChecked(csv_temporary, SerializeCsv(metadata));
        WriteChecked(manifest_temporary, SerializeManifest(metadata));
        ReplaceFile(csv_temporary, output_);
        ReplaceFile(manifest_temporary, manifest);
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(csv_temporary, ignored);
        std::filesystem::remove(manifest_temporary, ignored);
        throw;
    }
}
