// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <config/defaults.hpp>
#include <layout/orientation.hpp>

namespace consola {

// Saved layout data (DIP 5.1, 5.6). These are configuration values only: they
// never hold live application objects or TerminalId values. Runtime
// reconstruction is Stage 6.5 and is not performed here.

// One saved pane: a layout-local id (unique within its tab), the profile to
// launch it with, and an optional start directory. `profile` must name an
// entry in ApplicationConfig::profiles.
struct LayoutPaneConfig {
    std::string id;
    std::string profile{kDefaultProfileName};
    std::string working_directory;

    friend bool operator==(const LayoutPaneConfig &, const LayoutPaneConfig &) = default;
};

// One node of a saved tab. A leaf sets `pane`; a split sets `children` (indices
// into LayoutTabConfig::nodes) and a ratio strictly inside (0, 1). The two forms
// are mutually exclusive.
struct LayoutNodeConfig {
    Orientation orientation = Orientation::horizontal;
    float ratio = 0.5F;
    std::vector<std::size_t> children;
    std::optional<LayoutPaneConfig> pane;

    friend bool operator==(const LayoutNodeConfig &, const LayoutNodeConfig &) = default;
};

// One saved tab: its node list (nodes[root] is the root, with the remaining
// nodes referenced by index), and the layout-local id of the focused pane.
struct LayoutTabConfig {
    std::vector<LayoutNodeConfig> nodes;
    std::size_t root = 0;
    std::string focused;

    friend bool operator==(const LayoutTabConfig &, const LayoutTabConfig &) = default;
};

// A saved layout: the tab order and the active tab index.
struct LayoutConfig {
    std::vector<LayoutTabConfig> tabs;
    std::size_t active_tab = 0;

    friend bool operator==(const LayoutConfig &, const LayoutConfig &) = default;
};

} // namespace consola
