// SPDX-License-Identifier: MIT

#include <layout/layout_tree.hpp>

#include <cmath>
#include <utility>

namespace consola {

LayoutTree::LayoutTree(TerminalId terminal)
{
    NodeId id = allocate();
    nodes_.emplace(id.value, Entry{.parent = NodeId{}, .node = TerminalNode{terminal}});
    root_ = id;
    focused_ = terminal;
}

bool LayoutTree::empty() const noexcept
{
    return root_.value == 0;
}

std::size_t LayoutTree::terminal_count() const noexcept
{
    std::size_t count = 0;
    for (const auto &entry : nodes_) {
        if (std::holds_alternative<TerminalNode>(entry.second.node)) {
            ++count;
        }
    }
    return count;
}

NodeId LayoutTree::root() const noexcept
{
    return root_;
}

const LayoutNode *LayoutTree::node(NodeId id) const noexcept
{
    auto it = nodes_.find(id.value);
    return it == nodes_.end() ? nullptr : &it->second.node;
}

const SplitNode *LayoutTree::split(NodeId id) const noexcept
{
    const LayoutNode *n = node(id);
    return n == nullptr ? nullptr : std::get_if<SplitNode>(n);
}

const TerminalNode *LayoutTree::terminal(NodeId id) const noexcept
{
    const LayoutNode *n = node(id);
    return n == nullptr ? nullptr : std::get_if<TerminalNode>(n);
}

bool LayoutTree::contains(TerminalId terminal) const noexcept
{
    return find_terminal(terminal).has_value();
}

std::optional<NodeId> LayoutTree::find_terminal(TerminalId terminal) const noexcept
{
    for (const auto &[key, entry] : nodes_) {
        if (const auto *tn = std::get_if<TerminalNode>(&entry.node)) {
            if (tn->terminal == terminal) {
                return NodeId{key};
            }
        }
    }
    return std::nullopt;
}

std::optional<NodeId> LayoutTree::find_parent(NodeId id) const noexcept
{
    auto it = nodes_.find(id.value);
    if (it == nodes_.end() || it->second.parent.value == 0) {
        return std::nullopt;
    }
    return it->second.parent;
}

std::vector<TerminalId> LayoutTree::terminals() const
{
    std::vector<TerminalId> out;
    if (!empty()) {
        collect_terminals(root_, out);
    }
    return out;
}

void LayoutTree::collect_terminals(NodeId id, std::vector<TerminalId> &out) const
{
    const LayoutNode *n = node(id);
    if (n == nullptr) {
        return;
    }
    if (const auto *tn = std::get_if<TerminalNode>(n)) {
        out.push_back(tn->terminal);
        return;
    }
    const auto *sn = std::get_if<SplitNode>(n);
    collect_terminals(sn->first, out);
    collect_terminals(sn->second, out);
}

void LayoutTree::focus(TerminalId terminal) noexcept
{
    if (contains(terminal)) {
        focused_ = terminal;
    }
}

std::optional<TerminalId> LayoutTree::focused() const noexcept
{
    return focused_;
}

std::optional<NodeId> LayoutTree::split_terminal(TerminalId target, Orientation orientation,
                                                 TerminalId added, float ratio)
{
    if (!std::isfinite(ratio) || ratio <= 0.0F || ratio >= 1.0F) {
        return std::nullopt;
    }
    if (contains(added)) {
        return std::nullopt;
    }
    auto leaf = find_terminal(target);
    if (!leaf) {
        return std::nullopt;
    }

    const NodeId leaf_id = *leaf;
    const NodeId parent = nodes_.at(leaf_id.value).parent;

    const NodeId split_id = allocate();
    const NodeId added_id = allocate();
    nodes_.emplace(split_id.value, Entry{.parent = parent,
                                         .node = SplitNode{.orientation = orientation,
                                                           .ratio = ratio,
                                                           .first = leaf_id,
                                                           .second = added_id}});
    nodes_.emplace(added_id.value, Entry{.parent = split_id, .node = TerminalNode{added}});
    nodes_.at(leaf_id.value).parent = split_id;

    if (parent.value == 0) {
        root_ = split_id;
    } else {
        auto *ps = std::get_if<SplitNode>(&nodes_.at(parent.value).node);
        if (ps->first == leaf_id) {
            ps->first = split_id;
        } else {
            ps->second = split_id;
        }
    }
    return added_id;
}

std::optional<NodeId> LayoutTree::remove(TerminalId target)
{
    auto leaf = find_terminal(target);
    if (!leaf) {
        return std::nullopt;
    }

    const NodeId leaf_id = *leaf;
    const NodeId parent_id = nodes_.at(leaf_id.value).parent;

    if (parent_id.value == 0) {
        nodes_.erase(leaf_id.value);
        root_ = NodeId{};
        focused_.reset();
        return leaf_id;
    }

    const SplitNode *parent = split(parent_id);
    const NodeId sibling = (parent->first == leaf_id) ? parent->second : parent->first;
    const NodeId grand = nodes_.at(parent_id.value).parent;

    if (grand.value == 0) {
        root_ = sibling;
    } else {
        auto *gs = std::get_if<SplitNode>(&nodes_.at(grand.value).node);
        if (gs->first == parent_id) {
            gs->first = sibling;
        } else {
            gs->second = sibling;
        }
    }
    nodes_.at(sibling.value).parent = grand;
    nodes_.erase(parent_id.value);
    nodes_.erase(leaf_id.value);

    if (focused_ && *focused_ == target) {
        std::vector<TerminalId> survivors;
        collect_terminals(sibling, survivors);
        focused_ = survivors.empty() ? std::nullopt : std::optional<TerminalId>(survivors.front());
    }
    return leaf_id;
}

void LayoutTree::equalize() noexcept
{
    for (auto &entry : nodes_) {
        if (auto *sn = std::get_if<SplitNode>(&entry.second.node)) {
            sn->ratio = 0.5F;
        }
    }
}

bool LayoutTree::set_split_ratio(NodeId split_id, float ratio) noexcept
{
    if (!std::isfinite(ratio) || ratio <= 0.0F || ratio >= 1.0F) {
        return false;
    }
    auto it = nodes_.find(split_id.value);
    if (it == nodes_.end()) {
        return false;
    }
    auto *sn = std::get_if<SplitNode>(&it->second.node);
    if (sn == nullptr) {
        return false;
    }
    sn->ratio = ratio;
    return true;
}

NodeId LayoutTree::allocate() noexcept
{
    return NodeId{next_id_++};
}

} // namespace consola
