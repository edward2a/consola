// SPDX-License-Identifier: MIT

#include <app/log.hpp>

#include <cstdio>
#include <mutex>

namespace consola {
namespace {

std::mutex &log_mutex()
{
    static std::mutex mutex;
    return mutex;
}

const char *level_name(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    }
    return "unknown";
}

} // namespace

void log(LogLevel level, std::string_view message)
{
    std::scoped_lock lock(log_mutex());
    std::fprintf(stderr, "[%s] ", level_name(level));
    if (!message.empty()) {
        std::fwrite(message.data(), 1, message.size(), stderr);
    }
    std::fputc('\n', stderr);
}

LogLevel log_level_from_libtsm_severity(int severity)
{
    if (severity <= 3) {
        return LogLevel::Error;
    }
    if (severity == 4) {
        return LogLevel::Warning;
    }
    if (severity <= 6) {
        return LogLevel::Info;
    }
    return LogLevel::Debug;
}

} // namespace consola
