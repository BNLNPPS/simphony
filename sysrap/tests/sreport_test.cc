#include <cassert>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "sreport.h"

int main()
{
    sreport non_profiled;
    non_profiled.run = NP::Make<float>(1);

    NPFold* serialized = non_profiled.serialize();
    assert(serialized->get("run"));
    assert(serialized->get("runprof") == nullptr);
    assert(serialized->get("ranges") == nullptr);

    sreport restored_non_profiled;
    restored_non_profiled.import(serialized);
    assert(restored_non_profiled.run);
    assert(restored_non_profiled.run != non_profiled.run);
    assert(restored_non_profiled.runprof == nullptr);
    assert(restored_non_profiled.ranges == nullptr);

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("sreport_test-" + std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::remove_all(directory);
    non_profiled.save(directory.c_str());

    sreport* reloaded_non_profiled = sreport::Load(directory.c_str());
    assert(reloaded_non_profiled->run);
    assert(reloaded_non_profiled->runprof == nullptr);
    assert(reloaded_non_profiled->ranges == nullptr);
    std::filesystem::remove_all(directory);

    sreport profiled;
    profiled.run = NP::Make<float>(1);
    profiled.runprof = NP::Make<std::int64_t>(2, 4);
    profiled.ranges = NP::Make<std::int64_t>(1, 5);

    NPFold* serialized_profiled = profiled.serialize();
    sreport restored_profiled;
    restored_profiled.import(serialized_profiled);
    assert(restored_profiled.run);
    assert(restored_profiled.runprof);
    assert(restored_profiled.runprof != profiled.runprof);
    assert(restored_profiled.ranges);
    assert(restored_profiled.ranges != profiled.ranges);

    bool rejected_missing_run_on_serialize = false;
    try
    {
        sreport invalid;
        (void)invalid.serialize();
    }
    catch (const std::runtime_error& error)
    {
        rejected_missing_run_on_serialize =
            std::string(error.what()).find("required 'run' array") != std::string::npos;
    }
    assert(rejected_missing_run_on_serialize);

    NPFold missing_run;
    bool   rejected_missing_run = false;
    try
    {
        sreport invalid;
        invalid.import(&missing_run);
    }
    catch (const std::runtime_error& error)
    {
        rejected_missing_run =
            std::string(error.what()).find("required 'run' array") != std::string::npos;
    }
    assert(rejected_missing_run);

    return 0;
}
