// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <optional>
#include <string>

#include <config/layout_config.hpp>

namespace consola {

class TabManager;
struct TerminalId;

// Maps a live terminal to its assigned profile; returns empty to use the
// fallback profile (DIP 6.4).
using ProfileLookup = std::function<std::string(TerminalId)>;

// Capture the current tabs as plain LayoutConfig data (DIP 5.6). Tabs without
// terminals are skipped; `profile` is assigned to a pane unless `lookup`
// supplies a per-pane profile (DIP 6.4). Returns nullopt when there is nothing
// to capture (no tab has a terminal), so the caller never saves an empty
// layout. This never creates or modifies tabs, widgets or processes;
// reconstruction is Stage 6.5.
[[nodiscard]] std::optional<LayoutConfig> capture_layout(const TabManager &tabs,
                                                         const std::string &profile,
                                                         const ProfileLookup &lookup = {});

} // namespace consola
