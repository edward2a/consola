// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include <app/session.hpp>
#include <layout/orientation.hpp>

namespace consola {

// Stable identifier for a node in a LayoutTree. Stable IDs, never raw
// pointers, so references survive tree mutation (DIP 3.1).
struct NodeId {
    std::uint64_t value = 0;

    friend bool operator==(NodeId, NodeId) = default;
};

struct SplitNode {
    Orientation orientation = Orientation::horizontal;
    // Fraction of the split's extent given to `first` in (0, 1); the remainder
    // goes to `second`. The stored ratio is preserved even when a temporary
    // size constraint clamps its effective position (DIP 3.3).
    float ratio = 0.5F;
    NodeId first;
    NodeId second;
};

struct TerminalNode {
    TerminalId terminal;
};

using LayoutNode = std::variant<SplitNode, TerminalNode>;

// A binary split tree whose leaves are terminals (DIP 3.1). The tree is
// independent of FLTK: it stores structure, ratios and focus, never geometry.
// Terminals are referenced only by TerminalId; callers own the terminal
// instances and their sessions.
class LayoutTree {
public:
    LayoutTree() = default;
    // A tree holding a single terminal leaf.
    explicit LayoutTree(TerminalId terminal);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t terminal_count() const noexcept;

    // Root node. Only meaningful when !empty(); returns the zero NodeId
    // otherwise.
    [[nodiscard]] NodeId root() const noexcept;

    [[nodiscard]] const LayoutNode *node(NodeId id) const noexcept;
    [[nodiscard]] const SplitNode *split(NodeId id) const noexcept;
    [[nodiscard]] const TerminalNode *terminal(NodeId id) const noexcept;

    [[nodiscard]] bool contains(TerminalId terminal) const noexcept;
    [[nodiscard]] std::optional<NodeId> find_terminal(TerminalId terminal) const noexcept;
    // Parent split of `id`, or nullopt when `id` is the root (or absent).
    [[nodiscard]] std::optional<NodeId> find_parent(NodeId id) const noexcept;

    // Terminal leaves in depth-first order (first child before second).
    [[nodiscard]] std::vector<TerminalId> terminals() const;

    // Focus (DIP 3.2). focus() ignores a terminal that is not in the tree.
    void focus(TerminalId terminal) noexcept;
    [[nodiscard]] std::optional<TerminalId> focused() const noexcept;

    // Split the terminal leaf `target` in two: `target` keeps the `first`
    // child and `added` becomes the `second` child. `ratio` is the fraction
    // given to `first`. Returns the new leaf's NodeId, or nullopt when `target`
    // is not a leaf, `added` already exists, or `ratio` is outside (0, 1).
    std::optional<NodeId> split_terminal(TerminalId target, Orientation orientation,
                                         TerminalId added, float ratio = 0.5F);

    // Remove the terminal leaf `target`, collapsing its parent so the sibling
    // takes the freed space (rebalance after removal, DIP 3.2). Returns the
    // removed leaf's NodeId, or nullopt when absent. Removing the last terminal
    // empties the tree; if the removed terminal was focused, focus moves to the
    // sibling.
    std::optional<NodeId> remove(TerminalId target);

    // Set every split ratio to 0.5 (equalize splits, DIP 3.5).
    void equalize() noexcept;

    // Set one split's stored ratio (drag-resize, DIP 3.5). Returns false when
    // `split` is not a split node or `ratio` is not in (0, 1).
    bool set_split_ratio(NodeId split, float ratio) noexcept;

    // Swap the terminals of two leaves (pane movement, DIP 6.1). Focus follows
    // the terminal, so the moved pane keeps focus. Returns false when either
    // terminal is absent or both refer to the same terminal.
    bool swap_terminals(TerminalId first, TerminalId second) noexcept;

private:
    struct Entry {
        NodeId parent; // zero for the root
        LayoutNode node;
    };

    [[nodiscard]] NodeId allocate() noexcept;
    void collect_terminals(NodeId id, std::vector<TerminalId> &out) const;

    std::unordered_map<std::uint64_t, Entry> nodes_;
    NodeId root_;
    std::uint64_t next_id_ = 1;
    std::optional<TerminalId> focused_;
};

} // namespace consola
