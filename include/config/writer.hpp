// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <config/config.hpp>

namespace consola {

// Serialize a configuration as YAML in the versioned schema (DIP 5.4). The
// output is deterministic and re-parses to an equivalent configuration. String
// scalars are always double-quoted so values with YAML punctuation are safe.
[[nodiscard]] std::string serialize_config(const ApplicationConfig &config);

// Write the configuration to `path`, creating parent directories as needed.
// This is an explicit save: the application never calls it on startup, so a
// user's file is never rewritten implicitly (DIP 5.4). Returns nullopt on
// success or a human-readable error message.
[[nodiscard]] std::optional<std::string> save_config(const std::filesystem::path &path,
                                                     const ApplicationConfig &config);

} // namespace consola
