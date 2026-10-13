// SPDX-License-Identifier: MIT

#include <layout/layout_geometry.hpp>

#include <algorithm>
#include <cmath>

namespace consola {
namespace {

int non_negative(int value)
{
    return value < 0 ? 0 : value;
}

Size leaf_minimum(const LayoutMetrics &metrics)
{
    const int cell_width = std::max(1, metrics.cell_width);
    const int cell_height = std::max(1, metrics.cell_height);
    const int border = non_negative(metrics.pane_border);
    const int scrollbar = non_negative(metrics.scrollbar_width);
    return Size{(2 * cell_width) + (2 * border) + scrollbar, cell_height + (2 * border)};
}

Size node_minimum(const LayoutTree &tree, NodeId id, const LayoutMetrics &metrics)
{
    if (tree.terminal(id) != nullptr) {
        return leaf_minimum(metrics);
    }
    const SplitNode *split = tree.split(id);
    if (split == nullptr) {
        return Size{};
    }
    const Size first = node_minimum(tree, split->first, metrics);
    const Size second = node_minimum(tree, split->second, metrics);
    const int divider = non_negative(metrics.divider_thickness);
    if (split->orientation == Orientation::horizontal) {
        return Size{std::max(first.width, second.width), first.height + divider + second.height};
    }
    return Size{first.width + divider + second.width, std::max(first.height, second.height)};
}

int split_extent(int available, float ratio, int first_minimum, int second_minimum)
{
    if (available <= 0) {
        return 0;
    }
    const int lower = std::clamp(first_minimum, 0, available);
    const int upper = available - std::clamp(second_minimum, 0, available);
    if (upper < lower) {
        return lower;
    }
    const double fraction = std::isfinite(ratio) ? static_cast<double>(ratio) : 0.5;
    const int ideal = static_cast<int>(std::lround(fraction * static_cast<double>(available)));
    return std::clamp(ideal, lower, upper);
}

Rect intersect(const Rect &a, const Rect &b)
{
    const int left = std::max(a.x, b.x);
    const int top = std::max(a.y, b.y);
    const int right = std::min(a.x + a.width, b.x + b.width);
    const int bottom = std::min(a.y + a.height, b.y + b.height);
    if (right <= left || bottom <= top) {
        return Rect{left, top, 0, 0};
    }
    return Rect{left, top, right - left, bottom - top};
}

void place(const LayoutTree &tree, NodeId id, const Rect &rect, const LayoutMetrics &metrics,
           LayoutResult &out)
{
    if (const TerminalNode *leaf = tree.terminal(id)) {
        out.panes.push_back(
            PaneRect{.terminal = leaf->terminal, .logical = rect, .visible = Rect{}});
        return;
    }
    const SplitNode *split = tree.split(id);
    if (split == nullptr) {
        return;
    }

    const Size first_min = node_minimum(tree, split->first, metrics);
    const Size second_min = node_minimum(tree, split->second, metrics);
    const int divider = non_negative(metrics.divider_thickness);

    if (split->orientation == Orientation::horizontal) {
        const int available = non_negative(rect.height - divider);
        const int first =
            split_extent(available, split->ratio, first_min.height, second_min.height);
        const Rect first_rect{rect.x, rect.y, rect.width, first};
        const Rect divider_rect{rect.x, rect.y + first, rect.width,
                                std::min(divider, non_negative(rect.height - first))};
        const Rect second_rect{rect.x, rect.y + first + divider, rect.width,
                               non_negative(rect.height - first - divider)};
        if (divider > 0) {
            out.dividers.push_back(
                DividerRect{.split = id, .orientation = split->orientation, .rect = divider_rect});
        }
        place(tree, split->first, first_rect, metrics, out);
        place(tree, split->second, second_rect, metrics, out);
    } else {
        const int available = non_negative(rect.width - divider);
        const int first = split_extent(available, split->ratio, first_min.width, second_min.width);
        const Rect first_rect{rect.x, rect.y, first, rect.height};
        const Rect divider_rect{rect.x + first, rect.y,
                                std::min(divider, non_negative(rect.width - first)), rect.height};
        const Rect second_rect{rect.x + first + divider, rect.y,
                               non_negative(rect.width - first - divider), rect.height};
        if (divider > 0) {
            out.dividers.push_back(
                DividerRect{.split = id, .orientation = split->orientation, .rect = divider_rect});
        }
        place(tree, split->first, first_rect, metrics, out);
        place(tree, split->second, second_rect, metrics, out);
    }
}

} // namespace

Size minimum_size(const LayoutTree &tree, const LayoutMetrics &metrics)
{
    if (tree.empty()) {
        return Size{};
    }
    return node_minimum(tree, tree.root(), metrics);
}

LayoutResult compute_layout(const LayoutTree &tree, const Rect &content,
                            const LayoutMetrics &metrics)
{
    LayoutResult result;
    result.content =
        Rect{content.x, content.y, non_negative(content.width), non_negative(content.height)};
    if (tree.empty()) {
        result.canvas = result.content;
        return result;
    }

    const Size minimum = minimum_size(tree, metrics);
    result.canvas =
        Rect{result.content.x, result.content.y, std::max(result.content.width, minimum.width),
             std::max(result.content.height, minimum.height)};

    place(tree, tree.root(), result.canvas, metrics, result);
    for (PaneRect &pane : result.panes) {
        pane.visible = intersect(pane.logical, result.content);
    }
    return result;
}

} // namespace consola
