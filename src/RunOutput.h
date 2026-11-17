#pragma once

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "GpuDirectory.h"
#include "sysrap/NPU.hh"

namespace simphony
{
inline std::filesystem::path ResolveRunOutputPath(const std::filesystem::path& path)
{
    if (path.native().find('$') == std::string::npos)
        return path;
    const char* expanded = U::Resolve(path.c_str());
    if (!expanded)
        throw std::runtime_error("Cannot resolve run output: " + path.string());
    std::filesystem::path result(expanded);
    std::free(const_cast<char*>(expanded));
    return result;
}

// A run may place its hit arrays, timeline and profile on different filesystems.
// Stage each file beside its destination so publication and rollback use rename.
class RunOutput
{
    struct File
    {
        std::filesystem::path destination;
        std::filesystem::path staging;
        bool                  backed_up{false};
        bool                  published{false};
    };
    std::vector<File> files_;

  public:
    RunOutput() = default;
    RunOutput(const RunOutput&) = delete;
    RunOutput& operator=(const RunOutput&) = delete;

    ~RunOutput()
    {
        for (const File& file : files_)
        {
            // Keep a backup recoverable if rollback itself encountered an I/O error.
            if (!file.backed_up)
            {
                std::error_code ignored;
                std::filesystem::remove_all(file.staging, ignored);
            }
        }
    }

    std::filesystem::path Stage(const std::filesystem::path& destination)
    {
        const auto resolved = ResolveRunOutputPath(destination);
        const auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(resolved).parent_path()) / resolved.filename();
        for (const File& file : files_)
            if (path == file.destination)
                throw std::runtime_error("Duplicate run output: " + path.string());

        std::filesystem::create_directories(path.parent_path());
        files_.push_back({path, {}});
        auto& file = files_.back();
        file.staging = CreateUniqueDirectory(path.parent_path() / ".simg4ox-output-XXXXXX");
        return file.staging / path.filename();
    }

    void Publish()
    {
        try
        {
            for (File& file : files_)
            {
                const auto status = std::filesystem::symlink_status(file.destination);
                if (std::filesystem::exists(status))
                {
                    if (!std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status))
                        throw std::runtime_error("Cannot replace run output: " + file.destination.string());
                    std::filesystem::rename(file.destination, Backup(file));
                    file.backed_up = true;
                }
                std::filesystem::rename(file.staging / file.destination.filename(), file.destination);
                file.published = true;
            }
        }
        catch (...)
        {
            for (auto it = files_.rbegin(); it != files_.rend(); ++it)
            {
                std::error_code error;
                if (it->backed_up)
                {
                    std::filesystem::rename(Backup(*it), it->destination, error);
                    it->backed_up = bool(error);
                }
                else if (it->published)
                    std::filesystem::remove(it->destination, error);
                if (error)
                    std::cerr << "Unable to roll back " << it->destination << ": " << error.message() << '\n';
            }
            throw;
        }

        // All replacements succeeded. The destructor can discard the previous run.
        for (File& file : files_)
            file.backed_up = false;
    }

  private:
    static std::filesystem::path Backup(const File& file)
    {
        return file.staging / ("previous-" + file.destination.filename().string());
    }
};
} // namespace simphony
