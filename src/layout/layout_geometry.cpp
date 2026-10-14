// SPDX-License-Identifier: MIT

#include <layout/layout_geometry.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace consola {
namespace {

constexpr int kIntMax = std::numeric_limits<int>::max();

// Geometry is computed with 64-bit intermediates so large metrics or deeply
// nested trees cannot overflow the int rectangles. Unrepresentable results are
// clamped to the int range; real content areas are far below it.
int clamp_size(long long value)
{
    if (value < 0) {
        return 0;
    }
    return value > kIntMax ? kIntMax : static_cast<int>(value);
}

int non_negative(int value)
{
    return value < 0 ? 0 : value;
}

Size leaf_minimum(const LayoutMetrics &metrics)
{
    const long long cell_width = std::max<long long>(1, metrics.cell_width);
    const long long cell_height = std::max<long long>(1, metrics.cell_height);
    const long long border = non_negative(metrics.pane_border);
    const long long scrollbar = non_negative(metrics.scrollbar_width);
    return Size{clamp_size((2 * cell_width) + (2 * border) + scrollbar),
                clamp_size(cell_height + (2 * border))};
}

// Bottom-up subtree minimums, computed once per layout call and cached by node.
Size build_minimums(const LayoutTree &tree, NodeId id, const LayoutMetrics &metrics,
                    std::unordered_map<std::uint64_t, Size> &cache)
{
    if (tree.terminal(id) != nullptr) {
        const Size size = leaf_minimum(metrics);
        cache.emplace(id.value, size);
        return size;
    }
    const SplitNode *split = tree.split(id);
    if (split == nullptr) {
        const Size size{};
        cache.emplace(id.value, size);
        return size;
    }

    const Size first = build_minimums(tree, split->first, metrics, cache);
    const Size second = build_minimums(tree, split->second, metrics, cache);
    const long long divider = non_negative(metrics.divider_thickness);

    Size size;
    if (split->orientation == Orientation::horizontal) {
        size = Size{std::max(first.width, second.width),
                    clamp_size(static_cast<long long>(first.height) + divider + second.height)};
    } else {
        size = Size{clamp_size(static_cast<long long>(first.width) + divider + second.width),
                    std::max(first.height, second.height)};
    }
    cache.emplace(id.value, size);
    return size;
}

Size node_minimum(const LayoutTree &tree, NodeId id, const LayoutMetrics &metrics)
{
    std::unordered_map<std::uint64_t, Size> cache;
    return build_minimums(tree, id, metrics, cache);
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
           const std::unordered_map<std::uint64_t, Size> &minimums, LayoutResult &out)
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

    const Size first_min = minimums.at(split->first.value);
    const Size second_min = minimums.at(split->second.value);
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
            out.dividers.push_back(DividerRect{.split = id,
                                               .orientation = split->orientation,
                                               .rect = divider_rect,
                                               .area = rect,
                                               .visible = Rect{}});
        }
        place(tree, split->first, first_rect, metrics, minimums, out);
        place(tree, split->second, second_rect, metrics, minimums, out);
    } else {
        const int available = non_negative(rect.width - divider);
        const int first = split_extent(available, split->ratio, first_min.width, second_min.width);
        const Rect first_rect{rect.x, rect.y, first, rect.height};
        const Rect divider_rect{rect.x + first, rect.y,
                                std::min(divider, non_negative(rect.width - first)), rect.height};
        const Rect second_rect{rect.x + first + divider, rect.y,
                               non_negative(rect.width - first - divider), rect.height};
        if (divider > 0) {
            out.dividers.push_back(DividerRect{.split = id,
                                               .orientation = split->orientation,
                                               .rect = divider_rect,
                                               .area = rect,
                                               .visible = Rect{}});
        }
        place(tree, split->first, first_rect, metrics, minimums, out);
        place(tree, split->second, second_rect, metrics, minimums, out);
    }
}

} // namespace

Size minimum_size(const LayoutTree &tree, const LayoutMetrics &metrics)
{
    if (tree.empty()) {
        return Size{};
    }
    std::unordered_map<std::uint64_t, Size> cache;
    return build_minimums(tree, tree.root(), metrics, cache);
}

LayoutResult compute_layout(const LayoutTree &tree, const Rect &content,
                            const LayoutMetrics &metrics)
{
    LayoutResult result;

    // Normalize the content area and keep origin + extent representable in int
    // so every endpoint computed below stays overflow-free.
    long long origin_x = content.x;
    long long origin_y = content.y;
    const long long width = non_negative(content.width);
    const long long height = non_negative(content.height);
    origin_x = std::min(origin_x, static_cast<long long>(kIntMax) - width);
    origin_y = std::min(origin_y, static_cast<long long>(kIntMax) - height);
    result.content = Rect{static_cast<int>(origin_x), static_cast<int>(origin_y),
                          static_cast<int>(width), static_cast<int>(height)};
    if (tree.empty()) {
        result.canvas = result.content;
        return result;
    }

    std::unordered_map<std::uint64_t, Size> minimums;
    const Size minimum = build_minimums(tree, tree.root(), metrics, minimums);
    const long long canvas_width = std::min(std::max(width, static_cast<long long>(minimum.width)),
                                            static_cast<long long>(kIntMax) - origin_x);
    const long long canvas_height =
        std::min(std::max(height, static_cast<long long>(minimum.height)),
                 static_cast<long long>(kIntMax) - origin_y);
    result.canvas = Rect{static_cast<int>(origin_x), static_cast<int>(origin_y),
                         static_cast<int>(canvas_width), static_cast<int>(canvas_height)};

    place(tree, tree.root(), result.canvas, metrics, minimums, result);
    for (PaneRect &pane : result.panes) {
        pane.visible = intersect(pane.logical, result.content);
    }
    for (DividerRect &divider : result.dividers) {
        divider.visible = intersect(divider.rect, result.content);
    }
    return result;
}

std::optional<TerminalId> focus_neighbor(const LayoutResult &layout, TerminalId current,
                                         FocusDirection direction)
{
    const PaneRect *from = nullptr;
    for (const PaneRect &pane : layout.panes) {
        if (pane.terminal == current) {
            from = &pane;
            break;
        }
    }
    if (from == nullptr) {
        return std::nullopt;
    }

    const int from_left = from->logical.x;
    const int from_top = from->logical.y;
    const int from_right = from->logical.x + from->logical.width;
    const int from_bottom = from->logical.y + from->logical.height;
    const int from_cx = from_left + (from->logical.width / 2);
    const int from_cy = from_top + (from->logical.height / 2);

    const PaneRect *best = nullptr;
    long long best_primary = 0;
    long long best_secondary = 0;

    for (const PaneRect &pane : layout.panes) {
        if (pane.terminal == current) {
            continue;
        }
        const int left = pane.logical.x;
        const int top = pane.logical.y;
        const int right = left + pane.logical.width;
        const int bottom = top + pane.logical.height;
        const int cx = left + (pane.logical.width / 2);
        const int cy = top + (pane.logical.height / 2);

        bool candidate = false;
        long long primary = 0;
        long long secondary = 0;
        switch (direction) {
        case FocusDirection::right:
            candidate = cx > from_cx && std::min(bottom, from_bottom) > std::max(top, from_top);
            primary = static_cast<long long>(left) - from_right;
            secondary = std::abs(cy - from_cy);
            break;
        case FocusDirection::left:
            candidate = cx < from_cx && std::min(bottom, from_bottom) > std::max(top, from_top);
            primary = static_cast<long long>(from_left) - right;
            secondary = std::abs(cy - from_cy);
            break;
        case FocusDirection::down:
            candidate = cy > from_cy && std::min(right, from_right) > std::max(left, from_left);
            primary = static_cast<long long>(top) - from_bottom;
            secondary = std::abs(cx - from_cx);
            break;
        case FocusDirection::up:
            candidate = cy < from_cy && std::min(right, from_right) > std::max(left, from_left);
            primary = static_cast<long long>(from_top) - bottom;
            secondary = std::abs(cx - from_cx);
            break;
        }
        if (!candidate) {
            continue;
        }
        primary = std::max<long long>(primary, 0);
        if (best == nullptr || primary < best_primary ||
            (primary == best_primary && secondary < best_secondary)) {
            best = &pane;
            best_primary = primary;
            best_secondary = secondary;
        }
    }

    if (best == nullptr) {
        return std::nullopt;
    }
    return best->terminal;
}

float split_ratio_for_position(const LayoutTree &tree, NodeId split_id, const Rect &area,
                               int position, const LayoutMetrics &metrics)
{
    const SplitNode *split = tree.split(split_id);
    if (split == nullptr) {
        return 0.5F;
    }
    const int divider = non_negative(metrics.divider_thickness);
    const bool horizontal = split->orientation == Orientation::horizontal;
    const int origin = horizontal ? area.y : area.x;
    const int extent = horizontal ? area.height : area.width;
    const int available = non_negative(extent - divider);
    if (available <= 0) {
        return 0.5F;
    }

    const Size first_min = node_minimum(tree, split->first, metrics);
    const Size second_min = node_minimum(tree, split->second, metrics);
    const int min_first = horizontal ? first_min.height : first_min.width;
    const int min_second = horizontal ? second_min.height : second_min.width;

    const int lower = std::clamp(min_first, 0, available);
    const int upper = available - std::clamp(min_second, 0, available);
    if (upper < lower) {
        return 0.5F;
    }
    const int local = std::clamp(position - origin, lower, upper);
    return static_cast<float>(local) / static_cast<float>(available);
}

} // namespace consola
