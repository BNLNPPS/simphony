#pragma once

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace simphony
{
inline std::filesystem::path CreateUniqueDirectory(const std::filesystem::path& path_template)
{
    auto path = path_template.string();
    if (!::mkdtemp(path.data()))
    {
        const int error = errno;
        throw std::filesystem::filesystem_error("mkdtemp", path_template, std::error_code(error, std::generic_category()));
    }
    return path;
}
} // namespace simphony
