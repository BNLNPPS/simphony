#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "EventTiming.hh"

namespace
{
int SuccessfulMemoryQuery(std::int32_t& vm_kb, std::int32_t& rss_kb)
{
    vm_kb = 1234;
    rss_kb = 567;
    return 0;
}

int FailingMemoryQuery(std::int32_t&, std::int32_t&)
{
    return 1;
}

std::size_t CountOccurrences(const std::string& text, const std::string& needle)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos)
    {
        ++count;
        offset += needle.size();
    }
    return count;
}

void ExpectInvalid(const std::string& text)
{
    bool threw = false;
    try
    {
        (void)EventTimingSample::parse(text);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);
    assert(!EventTimingSample::looksLikeSerialized(text));
}

void TestCaptureMasks()
{
    const std::vector<EventTimingCapture> optional_fields = {
        EventTimingCapture::Wall,
        EventTimingCapture::ProcessCpu,
        EventTimingCapture::ThreadCpu,
        EventTimingCapture::Memory,
    };

    for (const EventTimingCapture requested : optional_fields)
    {
        const EventTimingSample sample = requested == EventTimingCapture::Memory
                                             ? EventTimingSample::Capture(requested, SuccessfulMemoryQuery)
                                             : EventTimingSample::Capture(requested);

        assert(sample.has(EventTimingCapture::Monotonic));
        for (const EventTimingCapture field : optional_fields)
            assert(sample.has(field) == (field == requested));
    }

    const EventTimingSample monotonic_only =
        EventTimingSample::Capture(static_cast<EventTimingCapture>(0));
    assert(monotonic_only.has(EventTimingCapture::Monotonic));
    assert(!monotonic_only.has(EventTimingCapture::Wall));
    assert(!monotonic_only.has(EventTimingCapture::Memory));
}

void TestSerializationRoundTrip()
{
    EventTimingSample sample{};
    sample.valid_mask = EventTimingSample::bit(EventTimingCapture::Monotonic) | EventTimingSample::bit(EventTimingCapture::Wall) | EventTimingSample::bit(EventTimingCapture::Memory);
    sample.wall_time_us = 1'760'000'000'000'000;
    sample.steady_time_ns = 42'000;
    sample.vm_kb = 1234;
    sample.rss_kb = 567;

    const std::string encoded = sample.serialize();
    assert(encoded == "1760000000000000,42000,,,1234,567");
    assert(EventTimingSample::looksLikeSerialized(encoded));
    assert(EventTimingSample::parse(encoded) == sample);

    const std::string description = sample.desc();
    assert(description.find("steady_time_ns=42000") != std::string::npos);
    assert(description.find("rss_kb=567") != std::string::npos);
}

void TestMalformedSerialization()
{
    ExpectInvalid("1,2,3,4,5");
    ExpectInvalid("1,2,3,4,5,6,7");
    ExpectInvalid("1,not-an-integer,3,4,5,6");
    ExpectInvalid("1,9223372036854775808,3,4,5,6");
    ExpectInvalid("1,2, ,4,5,6");
    ExpectInvalid(",,,,,");
}

void TestDeltas()
{
    EventTimingSample begin{};
    begin.valid_mask = EventTimingSample::bit(EventTimingCapture::Monotonic) | EventTimingSample::bit(EventTimingCapture::Memory);
    begin.steady_time_ns = 100;
    begin.vm_kb = 1000;
    begin.rss_kb = 500;

    EventTimingSample end = begin;
    end.steady_time_ns = 350;
    end.vm_kb = 1012;
    end.rss_kb = 507;

    assert(EventTimingSample::elapsedNs(begin, end) == 250);
    assert(EventTimingSample::deltaVmKb(begin, end) == 12);
    assert(EventTimingSample::deltaRssKb(begin, end) == 7);

    EventTimingSample missing{};
    bool              elapsed_threw = false;
    bool              memory_threw = false;
    try
    {
        (void)EventTimingSample::elapsedNs(missing, end);
    }
    catch (const std::logic_error&)
    {
        elapsed_threw = true;
    }
    try
    {
        (void)EventTimingSample::deltaRssKb(missing, end);
    }
    catch (const std::logic_error&)
    {
        memory_threw = true;
    }
    assert(elapsed_threw);
    assert(memory_threw);
}

void TestMemoryFailureWarnsOnce()
{
    std::ostringstream captured;
    std::streambuf*    original = std::cerr.rdbuf(captured.rdbuf());

    const EventTimingCapture mask =
        EventTimingCapture::Monotonic | EventTimingCapture::Memory;
    const EventTimingSample first = EventTimingSample::Capture(mask, FailingMemoryQuery);
    const EventTimingSample second = EventTimingSample::Capture(mask, FailingMemoryQuery);

    std::cerr.rdbuf(original);
    assert(!first.has(EventTimingCapture::Memory));
    assert(!second.has(EventTimingCapture::Memory));
    assert(CountOccurrences(captured.str(), "memory sample failed") == 1u);
}
} // namespace

int main()
{
    TestCaptureMasks();
    TestSerializationRoundTrip();
    TestMalformedSerialization();
    TestDeltas();
    TestMemoryFailureWarnsOnce();
    return 0;
}
