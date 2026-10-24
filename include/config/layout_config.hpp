// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <config/config.hpp>
#include <layout/layout_tree.hpp>

namespace consola {

// Saved layout data (DIP 5.1, 5.6). These are configuration values only: they
// never hold live application objects or TerminalId values. Capture,
// validation and serialization are implemented in the Stage 5 layout change
// set; this header fixes the data shape.

// One saved pane: a layout-local id (unique within its tab), the profile to
// launch it with, and an optional start directory. `profile` must name an
// entry in ApplicationConfig::profiles.
struct LayoutPaneConfig {
    std::string id;
    std::string profile{kDefaultProfileName};
    std::string working_directory;
};

// One node of a saved tab. A leaf sets `pane`; a split sets `children` (indices
// into LayoutTabConfig::nodes) and a ratio in (0, 1). The two forms are
// mutually exclusive.
struct LayoutNodeConfig {
    Orientation orientation = Orientation::horizontal;
    float ratio = 0.5F;
    std::vector<std::size_t> children;
    std::optional<LayoutPaneConfig> pane;
};

// One saved tab: its node list (nodes[root] is the root, with the remaining
// nodes referenced by index), and the layout-local id of the focused pane.
struct LayoutTabConfig {
    std::vector<LayoutNodeConfig> nodes;
    std::size_t root = 0;
    std::string focused;
};

// A saved layout: the tab order and the active tab index.
struct LayoutConfig {
    std::vector<LayoutTabConfig> tabs;
    std::size_t active_tab = 0;
};

} // namespace consola
