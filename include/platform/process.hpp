// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace consola {

// Read a process's current working directory. Returns nullopt when it cannot be
// determined (unsupported platform, already-exited process, permission). This
// is the only place layout capture touches OS process state (DIP 5.6).
[[nodiscard]] std::optional<std::string> process_working_directory(std::int64_t pid);

} // namespace consola
