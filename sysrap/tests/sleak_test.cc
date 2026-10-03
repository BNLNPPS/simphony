#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "NPFold.h"

namespace
{
namespace fs = std::filesystem;

fs::path TestDirectory()
{
    return fs::temp_directory_path() /
           ("sleak_test-" + std::to_string(static_cast<long long>(::getpid())));
}

void SaveInputFold(const fs::path& directory)
{
    NPFold fold;
    fold.add("run", NP::Make<std::int64_t>(1, 1));
    fold.save(directory.c_str());
}

void RunSleak(
    const fs::path& executable,
    const fs::path& input,
    const fs::path& output)
{
    const std::string command =
        "SLEAK_FOLD='" + output.string() + "' '" + executable.string() +
        "' '" + input.string() + "' >/dev/null 2>&1";
    const int status = std::system(command.c_str());
    assert(status != -1);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
}

void TestRealProfileIsSerialized(
    const fs::path& executable,
    const fs::path& fixture,
    const fs::path& directory)
{
    const fs::path input = directory / "profiled-input";
    const fs::path output = directory / "profiled-output";
    SaveInputFold(input);
    fs::copy_file(fixture, input / "event_timing_profile.csv");

    RunSleak(executable, input, output);

    NPFold* result = NPFold::Load(output.c_str());
    assert(result);
    const NP* profile = result->get("runprof");
    assert(profile);
    assert(profile->shape.size() == 2u);
    assert(profile->shape[0] == 15);
    assert(profile->shape[1] == 4);
    assert(profile->names.size() == 15u);
    assert(profile->names.front() == "EventTimingProfile__Configure");
    assert(profile->cvalues<std::int64_t>()[1] == 900'000'000);
    delete result;
}

void TestMissingProfileIsOmitted(
    const fs::path& executable,
    const fs::path& directory)
{
    const fs::path input = directory / "unprofiled-input";
    const fs::path output = directory / "unprofiled-output";
    SaveInputFold(input);

    RunSleak(executable, input, output);

    NPFold* result = NPFold::Load(output.c_str());
    assert(result);
    assert(result->get("run"));
    assert(result->get("runprof") == nullptr);
    delete result;
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    const fs::path executable = argv[1];
    const fs::path fixture = argv[2];
    const fs::path directory = TestDirectory();
    fs::remove_all(directory);
    fs::create_directories(directory);

    TestRealProfileIsSerialized(executable, fixture, directory);
    TestMissingProfileIsOmitted(executable, directory);

    fs::remove_all(directory);
    return 0;
}
