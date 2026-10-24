// SPDX-License-Identifier: MIT

#pragma once

#include <string>

#include <config/layout_config.hpp>

namespace consola {

class TabManager;

// Capture the current tabs as plain LayoutConfig data (DIP 5.6). Tabs without
// terminals are skipped; `profile` is assigned to every pane because per-pane
// profile selection is Stage 6.4. This never creates or modifies tabs, widgets
// or processes; reconstruction is Stage 6.5.
[[nodiscard]] LayoutConfig capture_layout(const TabManager &tabs, const std::string &profile);

} // namespace consola
