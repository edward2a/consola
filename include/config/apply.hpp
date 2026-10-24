// SPDX-License-Identifier: MIT

#pragma once

#include <string>

#include <app/session.hpp>
#include <app/terminal_style.hpp>
#include <config/config.hpp>

namespace consola {

// Resolve the effective terminal presentation for a profile (DIP 5.5). An empty
// name selects `config.default_profile`. Profile overrides are applied over the
// general/font/colours layer; absent fields inherit.
[[nodiscard]] TerminalStyle resolve_terminal_style(const ApplicationConfig &config,
                                                   const std::string &profile = {});

// Resolve the effective session options for a profile (shell, arguments,
// working directory, TERM/COLORTERM, login shell, close policy and limits).
[[nodiscard]] SessionOptions resolve_session_options(const ApplicationConfig &config,
                                                     const std::string &profile = {});

} // namespace consola
