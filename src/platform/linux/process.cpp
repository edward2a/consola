// SPDX-License-Identifier: MIT

#include <platform/process.hpp>

#include <filesystem>
#include <system_error>

namespace consola {

std::optional<std::string> process_working_directory(std::int64_t pid)
{
    if (pid <= 0) {
        return std::nullopt;
    }
    std::error_code error;
    const std::filesystem::path target = std::filesystem::read_symlink(
        std::filesystem::path("/proc") / std::to_string(pid) / "cwd", error);
    if (error) {
        return std::nullopt;
    }
    return target.string();
}

} // namespace consola
