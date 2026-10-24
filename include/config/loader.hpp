// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <config/config.hpp>

namespace consola {

// Outcome of loading a configuration. Warnings are non-fatal (unknown keys and
// similar); `error` is set when the file is malformed or a value is invalid, in
// which case `config` holds the built-in defaults and no partial data is
// applied (DIP 5.3).
struct ConfigLoadResult {
    ApplicationConfig config;
    std::vector<std::string> warnings;
    std::optional<std::string> error;

    [[nodiscard]] bool ok() const noexcept
    {
        return !error.has_value();
    }
};

// Load configuration from `path`. A missing file is not an error: the defaults
// are returned. Malformed YAML and invalid values set `error`.
[[nodiscard]] ConfigLoadResult load_config(const std::filesystem::path &path);

// Parse configuration from an in-memory YAML document. Every returned string
// owns its data, so `yaml` need not outlive the call. Used by load_config and
// the tests.
[[nodiscard]] ConfigLoadResult parse_config(std::string_view yaml);

// The default configuration file: `$XDG_CONFIG_HOME/consola/config.yml`, or
// `~/.config/consola/config.yml` when XDG_CONFIG_HOME is unset/empty.
[[nodiscard]] std::filesystem::path default_config_path();

} // namespace consola
