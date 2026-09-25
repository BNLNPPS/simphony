#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "EventTiming.hh"
#include "EventTimingProfileReport.hh"
#include "NP.hh"
#include "NPFold.h"

namespace
{
int FindRange(const NP* ranges, const char* prefix)
{
    assert(ranges);
    for (std::size_t index = 0; index < ranges->names.size(); ++index)
        if (ranges->names[index].find(prefix) == 0)
            return static_cast<int>(index);
    return -1;
}
}

int main()
{
    std::vector<EventTimingProfileRecord> records =
        EventTimingProfile::ReadFile(EVENT_TIMING_PROFILE_FIXTURE);
    assert(records.size() == 15u);

    std::vector<std::string> keys;
    std::vector<std::int64_t> timestamps;
    for (const EventTimingProfileRecord& record : records)
    {
        keys.push_back(record.name);
        timestamps.push_back(record.sample.steady_time_ns);
    }

    NP* profile = EventTimingProfileReport::MakeEventTimingProfileArray(records);
    NP* ranges = EventTimingProfileReport::MakeEventTimingRanges(records);
    assert(profile);
    assert(profile->shape.size() == 2u);
    assert(profile->shape[0] == 15);
    assert(profile->shape[1] == 4);
    assert(profile->names == keys);
    assert(profile->values<std::int64_t>()[1] == timestamps[0]);

    const int load = FindRange(ranges, "CSGFoundry__Load_HEAD");
    const int launch = FindRange(ranges, "A000_QSim__simulate_PREL");
    assert(load >= 0);
    assert(launch >= 0);
    assert(ranges->values<std::int64_t>()[5*load + 2] == 300'000);
    assert(ranges->values<std::int64_t>()[5*launch + 2] == 250'000);

    for (EventTimingProfileRecord& record : records)
        record.sample.wall_time_us += 987'654'321;
    NP* shifted = EventTimingProfileReport::MakeEventTimingRanges(records);
    assert(shifted->arr_bytes() == ranges->arr_bytes());
    assert(std::memcmp(shifted->bytes(), ranges->bytes(), ranges->arr_bytes()) == 0);

    NPFold fold;
    NP::SetMeta<std::string>(
        fold.meta, "timing", records.front().sample.serialize());
    NP::SetMeta<std::string>(fold.meta, "ordinary", "value");
    std::vector<std::string> profile_keys;
    std::vector<std::string> profile_values;
    fold.getMetaKV(&profile_keys, &profile_values, true);
    assert(profile_keys == std::vector<std::string>{"timing"});
    assert(profile_values.size() == 1u);
    assert(EventTimingSample::looksLikeSerialized(profile_values.front()));

    delete shifted;
    delete ranges;
    delete profile;
    return 0;
}
