#include "EventTiming.hh"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>
#include <time.h>
#include <unistd.h>

#include "sproc.h"

namespace
{
std::atomic_flag memory_warning_emitted = ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> temporary_sequence{0};
thread_local std::string profile_tag;

std::mutex& ProfileMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<EventTimingProfileRecord>& ProfileRecords()
{
    static std::vector<EventTimingProfileRecord> records;
    return records;
}

std::int64_t TimespecNs(const timespec& value)
{
    return static_cast<std::int64_t>(value.tv_sec) * 1'000'000'000ll
         + static_cast<std::int64_t>(value.tv_nsec);
}

std::int64_t ParseInteger(std::string_view field, std::string_view name)
{
    if (field.empty())
        throw std::invalid_argument("EventTimingSample missing " + std::string(name));

    std::int64_t value = 0;
    const char* begin = field.data();
    const char* end = begin + field.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end)
        throw std::invalid_argument("EventTimingSample invalid " + std::string(name));
    return value;
}

std::array<std::string_view, 6> SplitFields(std::string_view text)
{
    std::array<std::string_view, 6> fields{};
    std::size_t field_index = 0;
    std::size_t field_start = 0;

    for (std::size_t index = 0; index <= text.size(); ++index)
    {
        if (index != text.size() && text[index] != ',')
            continue;

        if (field_index >= fields.size())
            throw std::invalid_argument("EventTimingSample expected six fields");
        fields[field_index++] = text.substr(field_start, index - field_start);
        field_start = index + 1;
    }

    if (field_index != fields.size())
        throw std::invalid_argument("EventTimingSample expected six fields");
    return fields;
}

std::string OptionalValue(
    const EventTimingSample& sample,
    EventTimingCapture field,
    std::int64_t value)
{
    return sample.has(field) ? std::to_string(value) : std::string{};
}

void Require(const EventTimingSample& sample, EventTimingCapture field, const char* operation)
{
    if (!sample.has(field))
        throw std::logic_error(std::string("EventTimingSample ") + operation + " requires captured fields");
}

bool EnvironmentEnabled(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
        return false;

    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized == "1" || normalized == "true"
        || normalized == "yes" || normalized == "on";
}

int ParseIndex(const char* text)
{
    if (text == nullptr || *text == '\0')
        return 0;

    int value = 0;
    const char* end = text + std::char_traits<char>::length(text);
    const auto result = std::from_chars(text, end, value);
    if (result.ec != std::errc{} || result.ptr != end)
        throw std::invalid_argument("EventTiming profile path index is not an integer");
    return value;
}

std::string PadInteger(int index, int width, int precision, bool zero_fill, char conversion)
{
    if (conversion == 'u' && index < 0)
        throw std::invalid_argument("EventTiming unsigned index cannot be negative");

    const bool negative = index < 0;
    const std::uint64_t magnitude = negative
        ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(index))
        : static_cast<std::uint64_t>(index);
    std::string digits = std::to_string(magnitude);

    const int numeric_width = precision >= 0 ? precision : (zero_fill ? width : 0);
    if (numeric_width > static_cast<int>(digits.size()))
        digits.insert(0, static_cast<std::size_t>(numeric_width - digits.size()), '0');
    if (negative)
        digits.insert(digits.begin(), '-');

    if (precision < 0 && !zero_fill && width > static_cast<int>(digits.size()))
        digits.insert(0, static_cast<std::size_t>(width - digits.size()), ' ');
    return digits;
}

std::string FormatIndex(std::string_view pattern, int index)
{
    std::string result;
    int conversions = 0;

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
        while (cursor < pattern.size()
            && pattern[cursor] >= '0' && pattern[cursor] <= '9')
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
            while (cursor < pattern.size()
                && pattern[cursor] >= '0' && pattern[cursor] <= '9')
            {
                if (precision > 10000)
                    throw std::invalid_argument("EventTiming format precision is too large");
                precision = precision * 10 + (pattern[cursor++] - '0');
            }
            if (cursor == precision_start)
                throw std::invalid_argument("EventTiming format precision requires digits");
        }

        if (cursor >= pattern.size()
            || (pattern[cursor] != 'd' && pattern[cursor] != 'i' && pattern[cursor] != 'u'))
            throw std::invalid_argument("EventTiming format contains an unsupported conversion");

        const char conversion = pattern[cursor++];
        result += PadInteger(index, width, precision, zero_fill, conversion);
        ++conversions;
    }

    if (conversions != 1)
        throw std::invalid_argument("EventTiming format must contain exactly one integer conversion");
    return result;
}

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

struct CsvRow
{
    std::vector<std::string> fields;
    std::size_t line{1};
};

[[noreturn]] void CsvError(
    const std::filesystem::path& source,
    std::size_t line,
    std::string_view message)
{
    throw std::runtime_error(
        source.string() + ": line " + std::to_string(line) + ": " + std::string(message));
}

std::vector<CsvRow> ParseRows(
    std::istream& input,
    const std::filesystem::path& source)
{
    std::vector<CsvRow> rows;
    std::vector<std::string> fields;
    std::string field;
    std::size_t line = 1;
    std::size_t record_line = 1;
    bool in_quotes = false;
    bool after_quote = false;
    bool row_has_data = false;

    auto finish_row = [&]
    {
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

std::filesystem::path TemporaryPath(const std::filesystem::path& destination)
{
    const std::uint64_t sequence = temporary_sequence.fetch_add(1);
    return destination.string() + ".tmp." + std::to_string(static_cast<long long>(::getpid()))
         + "." + std::to_string(sequence);
}
}

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
    const auto fields = SplitFields(text);
    EventTimingSample sample{};

    sample.steady_time_ns = ParseInteger(fields[1], "steady_time_ns");
    sample.valid_mask |= bit(EventTimingCapture::Monotonic);

    if (!fields[0].empty())
    {
        sample.wall_time_us = ParseInteger(fields[0], "wall_time_us");
        sample.valid_mask |= bit(EventTimingCapture::Wall);
    }
    if (!fields[2].empty())
    {
        sample.process_cpu_ns = ParseInteger(fields[2], "process_cpu_ns");
        sample.valid_mask |= bit(EventTimingCapture::ProcessCpu);
    }
    if (!fields[3].empty())
    {
        sample.thread_cpu_ns = ParseInteger(fields[3], "thread_cpu_ns");
        sample.valid_mask |= bit(EventTimingCapture::ThreadCpu);
    }

    if (fields[4].empty() != fields[5].empty())
        throw std::invalid_argument("EventTimingSample requires both vm_kb and rss_kb");
    if (!fields[4].empty())
    {
        sample.vm_kb = ParseInteger(fields[4], "vm_kb");
        sample.rss_kb = ParseInteger(fields[5], "rss_kb");
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

bool EventTimingProfile::Enabled()
{
    return EnvironmentEnabled("EventTiming__PROFILE");
}

EventTimingSample EventTimingProfile::Mark(
    std::string_view name,
    std::string_view metadata)
{
    if (!Enabled())
        return EventTimingSample::Capture(EventTimingCapture::Monotonic);

    const EventTimingSample sample = EventTimingSample::Capture(
        EventTimingCapture::Monotonic
        | EventTimingCapture::Wall
        | EventTimingCapture::Memory);
    Add(name, sample, metadata);
    return sample;
}

void EventTimingProfile::Add(
    std::string_view name,
    const EventTimingSample& sample,
    std::string_view metadata)
{
    if (!Enabled())
        return;

    EventTimingProfileRecord record{
        profile_tag + std::string(name),
        sample,
        std::string(metadata),
    };
    std::lock_guard<std::mutex> lock(ProfileMutex());
    ProfileRecords().push_back(std::move(record));
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
    bool first = true;
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
    const auto& records = ProfileRecords();
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
    const auto& records = ProfileRecords();
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
    std::istream& input,
    const std::filesystem::path& source)
{
    const std::vector<CsvRow> rows = ParseRows(input, source);
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
    const char* configured = std::getenv("EventTiming__PROFILE_PATH");
    const std::string pattern = configured == nullptr
        ? std::string("EventTimingProfile.csv")
        : std::string(configured);
    if (pattern.empty())
        throw std::invalid_argument("EventTiming profile path is empty");
    if (pattern.find('%') == std::string::npos)
        return pattern;

    const int index = ParseIndex(std::getenv("EventTiming__PROFILE_PATH_INDEX"));
    return FormatIndex(pattern, index);
}

void EventTimingProfile::Write(EventTimingWriteMode mode)
{
    if (!Enabled())
        return;

    const std::filesystem::path destination = Path();
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
                "Unable to create EventTiming profile directory for "
                + destination.string() + ": " + directory_error.message());
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
                "Unable to replace EventTiming profile " + destination.string()
                + ": " + rename_error.message());
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
