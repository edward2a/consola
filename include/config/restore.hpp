// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <app/session.hpp>
#include <app/terminal_style.hpp>
#include <config/config.hpp>
#include <config/layout_config.hpp>

namespace consola {

// Resolution of one saved pane for a restore (DIP 6.5). This is configuration
// policy only; the UI builds the live tree from it.
struct RestorePaneOptions {
    SessionOptions session;
    TerminalStyle style;
    std::string profile;
    // The configured and default shells are both unavailable: create the pane
    // anyway and keep it visible after the launch failure.
    bool retain_on_exit = false;
    // If the configured shell passes the preflight but fails asynchronously,
    // retry once with this session (the default shell, same directory/env).
    std::optional<SessionOptions> fallback_session;
    std::vector<std::string> warnings;
};

// Resolve a saved pane: profile (falling back to the default), working
// directory (falling back when unavailable) and shell (falling back to the
// default shell once). Runs no processes and touches no live objects.
[[nodiscard]] RestorePaneOptions resolve_restore_pane(const ApplicationConfig &config,
                                                      const LayoutPaneConfig &pane);

// True when `executable` names an executable file (absolute/relative path) or
// resolves on PATH.
[[nodiscard]] bool executable_available(std::string_view executable);

} // namespace consola
