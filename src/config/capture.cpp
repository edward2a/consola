// SPDX-License-Identifier: MIT

#include <config/capture.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <app/tab.hpp>
#include <app/tab_manager.hpp>
#include <app/terminal_instance.hpp>
#include <layout/layout_tree.hpp>
#include <platform/process.hpp>

namespace consola {
namespace {

std::string working_directory_of(const TerminalInstance *instance)
{
    if (instance == nullptr) {
        return {};
    }
    return process_working_directory(instance->process_id()).value_or(std::string{});
}

// Append the subtree in depth-first order and return its node index. Pane ids
// are layout-local ("p1", "p2", ...) in that order.
std::size_t capture_node(const Tab &tab, const LayoutTree &layout, NodeId node_id,
                         const std::string &profile, const ProfileLookup &lookup,
                         std::optional<TerminalId> focused, std::string &focused_pane,
                         std::string &first_pane, LayoutTabConfig &out, std::size_t &next_pane)
{
    const std::size_t index = out.nodes.size();
    out.nodes.emplace_back();

    if (const TerminalNode *terminal = layout.terminal(node_id)) {
        LayoutPaneConfig pane;
        pane.id = "p" + std::to_string(next_pane++);
        const std::string assigned = lookup ? lookup(terminal->terminal) : std::string{};
        pane.profile = assigned.empty() ? profile : assigned;
        pane.working_directory = working_directory_of(tab.find(terminal->terminal));
        if (first_pane.empty()) {
            first_pane = pane.id;
        }
        if (focused.has_value() && focused.value() == terminal->terminal) {
            focused_pane = pane.id;
        }
        out.nodes[index].pane = std::move(pane);
        return index;
    }

    if (const SplitNode *split = layout.split(node_id)) {
        const std::size_t first = capture_node(tab, layout, split->first, profile, lookup, focused,
                                               focused_pane, first_pane, out, next_pane);
        const std::size_t second = capture_node(tab, layout, split->second, profile, lookup,
                                                focused, focused_pane, first_pane, out, next_pane);
        // Re-index: out.nodes may have reallocated during recursion.
        LayoutNodeConfig &node = out.nodes[index];
        node.orientation = split->orientation;
        node.ratio = split->ratio;
        node.children = {first, second};
        return index;
    }
    return index;
}

} // namespace

std::optional<LayoutConfig> capture_layout(const TabManager &tabs, const std::string &profile,
                                           const ProfileLookup &lookup)
{
    LayoutConfig config;
    const TabId active = tabs.active_id();
    for (const TabId id : tabs.ids()) {
        const Tab *tab = tabs.find(id);
        if (tab == nullptr || tab->layout().empty()) {
            continue;
        }
        const LayoutTree &layout = tab->layout();
        LayoutTabConfig tab_config;
        std::size_t next_pane = 1;
        std::string focused_pane;
        std::string first_pane;
        tab_config.root =
            capture_node(*tab, layout, layout.root(), profile, lookup, layout.focused(),
                         focused_pane, first_pane, tab_config, next_pane);
        tab_config.focused = focused_pane.empty() ? first_pane : focused_pane;
        config.tabs.push_back(std::move(tab_config));
        if (id == active) {
            config.active_tab = config.tabs.size() - 1;
        }
    }
    if (config.tabs.empty()) {
        return std::nullopt;
    }
    return config;
}

} // namespace consola
