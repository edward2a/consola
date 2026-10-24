// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <optional>

namespace consola {

// Run the application. `config_path` overrides the default configuration file
// location; when absent, `$XDG_CONFIG_HOME/consola/config.yml` (or the HOME
// fallback) is used. A missing file keeps the defaults.
int run_application(int argc, char **argv, bool smoke_test,
                    const std::optional<std::filesystem::path> &config_path = std::nullopt);

} // namespace consola
