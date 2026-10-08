// SPDX-License-Identifier: MIT

#include <cstdlib>

#include <app/log.hpp>

namespace {

bool maps_to(int severity, consola::LogLevel expected)
{
    return consola::log_level_from_libtsm_severity(severity) == expected;
}

} // namespace

int main()
{
    using consola::LogLevel;

    if (!maps_to(0, LogLevel::Error)) {
        return EXIT_FAILURE;
    }
    if (!maps_to(1, LogLevel::Error)) {
        return EXIT_FAILURE;
    }
    if (!maps_to(2, LogLevel::Error)) {
        return EXIT_FAILURE;
    }
    if (!maps_to(3, LogLevel::Error)) {
        return EXIT_FAILURE;
    }
    if (!maps_to(4, LogLevel::Warning)) {
        return EXIT_FAILURE;
    }
    if (!maps_to(5, LogLevel::Info)) {
        return EXIT_FAILURE;
    }
    if (!maps_to(6, LogLevel::Info)) {
        return EXIT_FAILURE;
    }
    if (!maps_to(7, LogLevel::Debug)) {
        return EXIT_FAILURE;
    }
    if (!maps_to(8, LogLevel::Debug)) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
