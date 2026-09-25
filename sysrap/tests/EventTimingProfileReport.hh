#pragma once

#include <vector>

#include "EventTiming.hh"
#include "NP.hh"

namespace EventTimingProfileReport
{
inline constexpr const char* JUNCTURE =
    "SEvt__Init_RUN_META,SEvt__BeginOfRun,SEvt__EndOfRun,SEvt__Init_RUN_META";

inline constexpr const char* RANGES = R"(
    SEvt__Init_RUN_META:CSGFoundry__Load_HEAD                     ## init
    CSGFoundry__Load_HEAD:CSGFoundry__Load_TAIL                   ## load_geom
    CSGOptiX__Create_HEAD:CSGOptiX__Create_TAIL                   ## upload_geom
    A%0.3d_QSim__simulate_HEAD:A%0.3d_QSim__simulate_LBEG         ## slice_genstep
    A%0.3d_QSim__simulate_PRUP:A%0.3d_QSim__simulate_PREL         ## upload genstep slice
    A%0.3d_QSim__simulate_PREL:A%0.3d_QSim__simulate_POST         ## simulate slice
    A%0.3d_QSim__simulate_POST:A%0.3d_QSim__simulate_DOWN         ## download slice
    A%0.3d_QSim__simulate_LEND:A%0.3d_QSim__simulate_PCAT         ## concat slices
    A%0.3d_QSim__simulate_BRES:A%0.3d_QSim__simulate_TAIL         ## save arrays
   )";

inline NP* MakeEventTimingProfileArray(
    const std::vector<EventTimingProfileRecord>& records)
{
    NP* profile = NP::Make<std::int64_t>(records.size(), 4);
    profile->labels = new std::vector<std::string>{
        "wall_time_us", "steady_time_ns", "vm_kb", "rss_kb"};
    std::int64_t* values = profile->values<std::int64_t>();
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        const EventTimingProfileRecord& record = records[index];
        values[4 * index + 0] = record.sample.wall_time_us;
        values[4 * index + 1] = record.sample.steady_time_ns;
        values[4 * index + 2] = record.sample.vm_kb;
        values[4 * index + 3] = record.sample.rss_kb;
        profile->names.push_back(record.name);
    }
    return profile;
}

inline NP* MakeEventTimingRanges(
    const std::vector<EventTimingProfileRecord>& records)
{
    std::vector<std::string>  keys;
    std::vector<std::int64_t> steady_time_us;
    keys.reserve(records.size());
    steady_time_us.reserve(records.size());
    for (const EventTimingProfileRecord& record : records)
    {
        keys.push_back(record.name);
        steady_time_us.push_back(record.sample.steady_time_ns / 1000);
    }
    return NP::MakeMetaKVS_ranges2(keys, steady_time_us, RANGES);
}
} // namespace EventTimingProfileReport
