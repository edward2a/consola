// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace consola {

// Run the application. `config_path` overrides the default configuration file
// location; when absent, `$XDG_CONFIG_HOME/consola/config.yml` (or the HOME
// fallback) is used. A missing file keeps the defaults. `layout_name` requests
// an explicit restore of a saved layout after loading (DIP 6.5).
int run_application(int argc, char **argv, bool smoke_test,
                    const std::optional<std::filesystem::path> &config_path = std::nullopt,
                    const std::optional<std::string> &layout_name = std::nullopt);

} // namespace consola
