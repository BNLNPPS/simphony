#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
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

    const std::filesystem::path input = directory / "profiled-input";
    const std::filesystem::path output = directory / "profiled-output";
    const std::filesystem::path profile = directory / "custom-profile.csv";
    NPFold                      input_fold;
    NP*                         input_run = NP::Make<std::int64_t>(1, 1);
    NP::SetMeta<std::string>(input_run->meta,
                             "EventTimingProfile__Configure",
                             "1760000000000000,900000000,,,1234,567");
    input_fold.add("run", input_run);
    input_fold.save(input.c_str());
    std::filesystem::copy_file(EVENT_TIMING_PROFILE_FIXTURE, profile);

    const std::string command =
        "SREPORT_FOLD='" + output.string() + "' '" + SREPORT_EXECUTABLE +
        "' '" + input.string() + "' --event-timing-profile '" +
        profile.string() + "' >/dev/null 2>&1";
    const int status = std::system(command.c_str());
    assert(status != -1);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    sreport* generated = sreport::Load(output.c_str());
    assert(generated);
    assert(generated->runprof);
    assert(generated->runprof->shape.size() == 2u);
    assert(generated->runprof->shape[0] == 15);
    assert(generated->runprof->shape[1] == 4);
    assert(generated->ranges);
    std::filesystem::remove_all(directory);

    return 0;
}
