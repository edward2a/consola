// SPDX-License-Identifier: MIT

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include <layout/layout_geometry.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::DividerRect;
using consola::LayoutMetrics;
using consola::LayoutResult;
using consola::LayoutTree;
using consola::Orientation;
using consola::PaneRect;
using consola::Rect;
using consola::Size;
using consola::TerminalId;

// cell 10x20, 1px border, 4px scrollbar, 2px divider.
// leaf minimum: 26 x 22.
const LayoutMetrics kMetrics{.cell_width = 10,
                             .cell_height = 20,
                             .pane_border = 1,
                             .scrollbar_width = 4,
                             .divider_thickness = 2};

TerminalId tid(std::uint64_t value)
{
    return TerminalId{value};
}

bool overlaps(const Rect &a, const Rect &b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

const PaneRect *find_pane(const LayoutResult &result, std::uint64_t terminal)
{
    for (const PaneRect &pane : result.panes) {
        if (pane.terminal == tid(terminal)) {
            return &pane;
        }
    }
    return nullptr;
}

void check_tiling(const LayoutResult &result)
{
    std::vector<Rect> rects;
    rects.reserve(result.panes.size() + result.dividers.size());
    long area = 0;
    for (const PaneRect &pane : result.panes) {
        rects.push_back(pane.logical);
    }
    for (const DividerRect &divider : result.dividers) {
        rects.push_back(divider.rect);
    }
    for (const Rect &rect : rects) {
        CHECK(rect.width >= 0);
        CHECK(rect.height >= 0);
        CHECK(rect.x >= result.canvas.x);
        CHECK(rect.y >= result.canvas.y);
        CHECK(rect.x + rect.width <= result.canvas.x + result.canvas.width);
        CHECK(rect.y + rect.height <= result.canvas.y + result.canvas.height);
        area += static_cast<long>(rect.width) * rect.height;
    }
    for (std::size_t i = 0; i < rects.size(); ++i) {
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            CHECK(!overlaps(rects[i], rects[j]));
        }
    }
    CHECK(area == static_cast<long>(result.canvas.width) * result.canvas.height);
}

void test_minimum_size()
{
    LayoutTree single(tid(1));
    CHECK(consola::minimum_size(single, kMetrics) == Size{26, 22});

    LayoutTree horizontal(tid(1));
    horizontal.split_terminal(tid(1), Orientation::horizontal, tid(2));
    CHECK(consola::minimum_size(horizontal, kMetrics) == Size{26, 46});

    LayoutTree vertical(tid(1));
    vertical.split_terminal(tid(1), Orientation::vertical, tid(2));
    CHECK(consola::minimum_size(vertical, kMetrics) == Size{54, 22});

    LayoutTree nested(tid(1));
    nested.split_terminal(tid(1), Orientation::horizontal, tid(2));
    nested.split_terminal(tid(2), Orientation::vertical, tid(3));
    CHECK(consola::minimum_size(nested, kMetrics) == Size{54, 46});

    LayoutTree empty;
    CHECK(consola::minimum_size(empty, kMetrics) == Size{0, 0});
}

void test_single_layout()
{
    LayoutTree tree(tid(1));
    const LayoutResult result = consola::compute_layout(tree, Rect{0, 0, 100, 50}, kMetrics);

    CHECK(result.canvas == Rect(0, 0, 100, 50));
    CHECK(result.content == Rect(0, 0, 100, 50));
    CHECK(result.panes.size() == 1);
    CHECK(result.dividers.empty());
    CHECK(result.panes[0].terminal == tid(1));
    CHECK(result.panes[0].logical == Rect(0, 0, 100, 50));
    CHECK(result.panes[0].visible == Rect(0, 0, 100, 50));
    check_tiling(result);
}

void test_vertical_split()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2), 0.5F);
    const LayoutResult result = consola::compute_layout(tree, Rect{0, 0, 100, 22}, kMetrics);

    CHECK(result.panes.size() == 2);
    CHECK(result.dividers.size() == 1);
    CHECK(find_pane(result, 1)->logical == Rect(0, 0, 49, 22));
    CHECK(find_pane(result, 2)->logical == Rect(51, 0, 49, 22));
    CHECK(result.dividers[0].rect == Rect(49, 0, 2, 22));
    CHECK(result.dividers[0].orientation == Orientation::vertical);
    CHECK(find_pane(result, 1)->visible == find_pane(result, 1)->logical);
    CHECK(find_pane(result, 2)->visible == find_pane(result, 2)->logical);
    check_tiling(result);
}

void test_horizontal_split()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::horizontal, tid(2), 0.5F);
    const LayoutResult result = consola::compute_layout(tree, Rect{0, 0, 40, 100}, kMetrics);

    CHECK(find_pane(result, 1)->logical == Rect(0, 0, 40, 49));
    CHECK(find_pane(result, 2)->logical == Rect(0, 51, 40, 49));
    CHECK(result.dividers[0].rect == Rect(0, 49, 40, 2));
    CHECK(result.dividers[0].orientation == Orientation::horizontal);
    check_tiling(result);
}

void test_nested_tiling()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2), 0.5F);
    tree.split_terminal(tid(2), Orientation::horizontal, tid(3), 0.5F);
    const LayoutResult result = consola::compute_layout(tree, Rect{0, 0, 200, 100}, kMetrics);

    CHECK(result.panes.size() == 3);
    CHECK(result.dividers.size() == 2);
    CHECK(find_pane(result, 1)->logical == Rect(0, 0, 99, 100));
    CHECK(find_pane(result, 2)->logical == Rect(101, 0, 99, 49));
    CHECK(find_pane(result, 3)->logical == Rect(101, 51, 99, 49));
    check_tiling(result);
}

void test_boundary_sized()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2), 0.5F);
    tree.split_terminal(tid(2), Orientation::horizontal, tid(3), 0.5F);
    const LayoutResult result = consola::compute_layout(tree, Rect{0, 0, 54, 46}, kMetrics);

    CHECK(result.canvas == Rect(0, 0, 54, 46));
    check_tiling(result);
    CHECK(find_pane(result, 1)->logical.width == 26);
    CHECK(find_pane(result, 2)->logical.height == 22);
    CHECK(find_pane(result, 3)->logical.height == 22);
}

void test_undersized_clipping()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2), 0.5F);
    const LayoutResult result = consola::compute_layout(tree, Rect{0, 0, 40, 22}, kMetrics);

    CHECK(result.content == Rect(0, 0, 40, 22));
    CHECK(result.canvas == Rect(0, 0, 54, 22));
    CHECK(find_pane(result, 1)->logical == Rect(0, 0, 26, 22));
    CHECK(find_pane(result, 2)->logical == Rect(28, 0, 26, 22));
    CHECK(find_pane(result, 1)->visible == Rect(0, 0, 26, 22));
    CHECK(find_pane(result, 2)->visible == Rect(28, 0, 12, 22));
    check_tiling(result);
}

void test_partial_clip_nonzero_origin()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2), 0.5F);
    const LayoutResult result = consola::compute_layout(tree, Rect{10, 5, 40, 22}, kMetrics);

    CHECK(result.content == Rect(10, 5, 40, 22));
    CHECK(result.canvas == Rect(10, 5, 54, 22));
    CHECK(find_pane(result, 1)->logical == Rect(10, 5, 26, 22));
    CHECK(find_pane(result, 1)->visible == Rect(10, 5, 26, 22));
    CHECK(find_pane(result, 2)->logical == Rect(38, 5, 26, 22));
    CHECK(find_pane(result, 2)->visible == Rect(38, 5, 12, 22));
    CHECK(result.dividers[0].rect == Rect(36, 5, 2, 22));
    CHECK(result.dividers[0].visible == Rect(36, 5, 2, 22));
    check_tiling(result);
}

void test_zero_area_clipping()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2), 0.5F);
    const LayoutResult result = consola::compute_layout(tree, Rect{5, 7, 0, 0}, kMetrics);

    CHECK(result.canvas == Rect(5, 7, 54, 22));
    CHECK(result.panes.size() == 2);
    for (const PaneRect &pane : result.panes) {
        CHECK(pane.logical.width > 0);
        CHECK(pane.logical.height > 0);
        CHECK(pane.visible.width == 0);
        CHECK(pane.visible.height == 0);
    }
    check_tiling(result);
}

void test_ratio_preserved_across_resize()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2), 0.3F);

    const LayoutResult wide = consola::compute_layout(tree, Rect{0, 0, 100, 22}, kMetrics);
    CHECK(find_pane(wide, 1)->logical.width == 29);

    const LayoutResult narrow = consola::compute_layout(tree, Rect{0, 0, 54, 22}, kMetrics);
    CHECK(find_pane(narrow, 1)->logical.width == 26);

    const LayoutResult wide_again = consola::compute_layout(tree, Rect{0, 0, 100, 22}, kMetrics);
    CHECK(find_pane(wide_again, 1)->logical.width == 29);

    CHECK(tree.split(tree.root())->ratio == 0.3F);
}

void test_divider_clamped_to_minimums()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2), 0.9F);
    const LayoutResult result = consola::compute_layout(tree, Rect{0, 0, 54, 22}, kMetrics);

    CHECK(find_pane(result, 1)->logical.width == 26);
    CHECK(find_pane(result, 2)->logical.width == 26);
    check_tiling(result);
}

void test_empty_tree()
{
    LayoutTree tree;
    const LayoutResult result = consola::compute_layout(tree, Rect{3, 4, 100, 50}, kMetrics);

    CHECK(result.panes.empty());
    CHECK(result.dividers.empty());
    CHECK(result.canvas == Rect(3, 4, 100, 50));
}

void test_negative_content_normalized()
{
    LayoutTree tree(tid(1));
    const LayoutResult result = consola::compute_layout(tree, Rect{10, 10, -5, -5}, kMetrics);

    CHECK(result.content == Rect(10, 10, 0, 0));
    CHECK(result.canvas == Rect(10, 10, 26, 22));
    CHECK(result.panes[0].logical == Rect(10, 10, 26, 22));
    CHECK(result.panes[0].visible == Rect(10, 10, 0, 0));
}

void test_extreme_metrics_clamped()
{
    const int maximum = std::numeric_limits<int>::max();
    const LayoutMetrics huge{.cell_width = maximum,
                             .cell_height = maximum,
                             .pane_border = maximum,
                             .scrollbar_width = maximum,
                             .divider_thickness = maximum};
    LayoutTree tree(tid(1));
    CHECK(consola::minimum_size(tree, huge) == Size{maximum, maximum});

    const LayoutResult result = consola::compute_layout(tree, Rect{0, 0, 10, 10}, huge);
    CHECK(result.canvas == Rect(0, 0, maximum, maximum));
    CHECK(result.panes[0].logical == Rect(0, 0, maximum, maximum));
    CHECK(result.panes[0].visible == Rect(0, 0, 10, 10));
}

void test_extreme_origin()
{
    const int minimum = std::numeric_limits<int>::min();
    LayoutTree tree(tid(1));
    const LayoutResult result =
        consola::compute_layout(tree, Rect{minimum, minimum, 10, 10}, kMetrics);

    CHECK(result.canvas == Rect(minimum, minimum, 26, 22));
    CHECK(result.panes[0].visible == Rect(minimum, minimum, 10, 10));
}

void test_focus_neighbor()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2));
    tree.split_terminal(tid(1), Orientation::horizontal, tid(3));
    tree.split_terminal(tid(2), Orientation::horizontal, tid(4));
    const LayoutResult layout = consola::compute_layout(tree, Rect{0, 0, 200, 100}, kMetrics);

    using consola::FocusDirection;
    auto neighbor = [&](std::uint64_t from, FocusDirection direction) {
        return consola::focus_neighbor(layout, tid(from), direction);
    };

    CHECK(neighbor(1, FocusDirection::right) == std::optional<TerminalId>(tid(2)));
    CHECK(neighbor(1, FocusDirection::down) == std::optional<TerminalId>(tid(3)));
    CHECK(!neighbor(1, FocusDirection::up).has_value());
    CHECK(!neighbor(1, FocusDirection::left).has_value());
    CHECK(neighbor(3, FocusDirection::up) == std::optional<TerminalId>(tid(1)));
    CHECK(neighbor(3, FocusDirection::right) == std::optional<TerminalId>(tid(4)));
    CHECK(neighbor(2, FocusDirection::left) == std::optional<TerminalId>(tid(1)));
    CHECK(neighbor(2, FocusDirection::down) == std::optional<TerminalId>(tid(4)));
    CHECK(neighbor(4, FocusDirection::left) == std::optional<TerminalId>(tid(3)));
    CHECK(neighbor(4, FocusDirection::up) == std::optional<TerminalId>(tid(2)));
    CHECK(!consola::focus_neighbor(layout, tid(99), FocusDirection::right).has_value());
}

void test_split_ratio_for_position()
{
    LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), Orientation::vertical, tid(2));
    const consola::NodeId root = tree.root();
    const Rect area{0, 0, 100, 22};

    auto ratio_at = [&](int position) {
        return consola::split_ratio_for_position(tree, root, area, position, kMetrics);
    };

    // available = 98; leaf minima are 26, so the divider spans [26, 72].
    CHECK(std::abs(ratio_at(49) - 0.5F) < 0.001F);
    CHECK(std::abs(ratio_at(10) - (26.0F / 98.0F)) < 0.001F);
    CHECK(std::abs(ratio_at(90) - (72.0F / 98.0F)) < 0.001F);
    CHECK(std::abs(ratio_at(0) - (26.0F / 98.0F)) < 0.001F);
    CHECK(std::abs(ratio_at(100) - (72.0F / 98.0F)) < 0.001F);

    CHECK(consola::split_ratio_for_position(tree, consola::NodeId{999}, area, 50, kMetrics) ==
          0.5F);
}

} // namespace

int main()
{
    test_minimum_size();
    test_single_layout();
    test_vertical_split();
    test_horizontal_split();
    test_nested_tiling();
    test_boundary_sized();
    test_undersized_clipping();
    test_partial_clip_nonzero_origin();
    test_zero_area_clipping();
    test_ratio_preserved_across_resize();
    test_divider_clamped_to_minimums();
    test_empty_tree();
    test_negative_content_normalized();
    test_extreme_metrics_clamped();
    test_extreme_origin();
    test_focus_neighbor();
    test_split_ratio_for_position();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d layout geometry check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
