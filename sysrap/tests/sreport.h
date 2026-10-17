#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "EventTimingProfileReport.hh"
#include "NPFold.h"

struct sreport
{
    static constexpr const char* JUNCTURE = EventTimingProfileReport::JUNCTURE;
    static constexpr const char* RANGES = EventTimingProfileReport::RANGES;

    bool VERBOSE;

    NP* run; // dummy array that exists just to hold metadata
    NP* runprof;
    NP* ranges;

    NPFold* substamp;
    NPFold* subprofile;
    NPFold* submeta;
    NPFold* submeta_NumPhotonCollected;
    NPFold* subcount;

    sreport();

    NPFold* serialize() const;
    void import(const NPFold* fold);
    void save(const char* dir) const;
    static sreport* Load(const char* dir);

    std::string desc() const;
    std::string desc_run() const;
    std::string desc_runprof() const;
    std::string desc_ranges() const;
    std::string desc_substamp() const;
    std::string desc_subprofile() const;
    std::string desc_submeta() const;
    std::string desc_subcount() const;
};

inline sreport::sreport() :
    VERBOSE(std::getenv("sreport__VERBOSE") != nullptr),
    run(nullptr),
    runprof(nullptr),
    ranges(nullptr),
    substamp(nullptr),
    subprofile(nullptr),
    submeta(nullptr),
    submeta_NumPhotonCollected(nullptr),
    subcount(nullptr)
{
}

inline NPFold* sreport::serialize() const
{
    if (run == nullptr)
        throw std::runtime_error("sreport::serialize missing required 'run' array");

    NPFold* smry = new NPFold;
    smry->add("run", run);
    smry->add("runprof", runprof);
    smry->add("ranges", ranges);
    smry->add_subfold("substamp", substamp);
    smry->add_subfold("subprofile", subprofile);
    smry->add_subfold("submeta", submeta);
    smry->add_subfold("submeta_NumPhotonCollected", submeta_NumPhotonCollected);
    smry->add_subfold("subcount", subcount);
    return smry;
}

inline NP* sreport_copy_array(const NPFold* smry, const char* key, bool required)
{
    if (smry == nullptr)
        throw std::runtime_error("sreport::import requires a summary fold");

    const NP* array = smry->get(key);
    if (required && array == nullptr)
        throw std::runtime_error(
            std::string("sreport::import missing required '") + key + "' array");

    return array ? array->copy() : nullptr;
}

inline void sreport::import(const NPFold* smry)
{
    run = sreport_copy_array(smry, "run", true);
    runprof = sreport_copy_array(smry, "runprof", false);
    ranges = sreport_copy_array(smry, "ranges", false);
    substamp = smry->get_subfold("substamp");
    subprofile = smry->get_subfold("subprofile");
    submeta = smry->get_subfold("submeta");
    submeta_NumPhotonCollected = smry->get_subfold("submeta_NumPhotonCollected");
    subcount = smry->get_subfold("subcount");
}

inline void sreport::save(const char* dir) const
{
    NPFold* smry = serialize();
    smry->save_verbose(dir);
}

inline sreport* sreport::Load(const char* dir)
{
    NPFold*  smry = NPFold::Load(dir);
    sreport* report = new sreport;
    report->import(smry);
    return report;
}
