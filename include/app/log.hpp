// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string_view>

namespace consola {

enum class LogLevel : std::uint8_t {
    Debug,
    Info,
    Warning,
    Error,
};

void log(LogLevel level, std::string_view message);

LogLevel log_level_from_libtsm_severity(int severity);

} // namespace consola
