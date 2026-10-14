// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <app/session.hpp>
#include <app/terminal_instance.hpp>
#include <layout/layout_tree.hpp>

namespace consola {

// A tab owns one layout tree and its terminal instances (DIP 4.1). The layout
// refers to terminals only by TerminalId. Stage 3 uses a single tab;
// TabManager is added in Stage 4.
class Tab {
public:
    Tab() = default;

    [[nodiscard]] LayoutTree &layout() noexcept
    {
        return layout_;
    }
    [[nodiscard]] const LayoutTree &layout() const noexcept
    {
        return layout_;
    }

    [[nodiscard]] std::size_t terminal_count() const noexcept
    {
        return terminals_.size();
    }

    [[nodiscard]] TerminalInstance *find(TerminalId id) noexcept;
    [[nodiscard]] const TerminalInstance *find(TerminalId id) const noexcept;

    // Insert an instance and make it the layout root. Returns null when the tab
    // is not empty or the id is already present; the instance is then dropped.
    TerminalInstance *add_root(std::unique_ptr<TerminalInstance> instance);

    // Insert an instance and split `target`'s leaf, placing the new terminal in
    // the second half. Returns the new layout leaf, or nullopt when `target` is
    // not a leaf, `instance`'s id is already present, or the ratio is invalid.
    std::optional<NodeId> split_terminal(TerminalId target, Orientation orientation,
                                         std::unique_ptr<TerminalInstance> instance,
                                         float ratio = 0.5F);

    // Remove the instance from the map and its node from the layout, returning
    // ownership. Returns null when the id is unknown.
    std::unique_ptr<TerminalInstance> remove_terminal(TerminalId id);

    // All instances (unordered).
    [[nodiscard]] std::vector<TerminalInstance *> terminals() noexcept;

private:
    LayoutTree layout_;
    std::unordered_map<TerminalId, std::unique_ptr<TerminalInstance>> terminals_;
};

} // namespace consola
