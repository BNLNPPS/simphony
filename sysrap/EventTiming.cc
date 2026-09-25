#include "EventTiming.hh"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <time.h>

#include "sproc.h"

namespace
{
std::atomic_flag memory_warning_emitted = ATOMIC_FLAG_INIT;

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
