// SPDX-License-Identifier: MIT

#pragma once

#include <vector>

#include <layout/layout_tree.hpp>

namespace consola {

// Integer pixel rectangle in a top-left-anchored coordinate space.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    Rect() = default;
    Rect(int origin_x, int origin_y, int extent_width, int extent_height)
        : x(origin_x), y(origin_y), width(extent_width), height(extent_height)
    {}

    friend bool operator==(const Rect &, const Rect &) = default;
};

struct Size {
    int width = 0;
    int height = 0;

    Size() = default;
    Size(int extent_width, int extent_height) : width(extent_width), height(extent_height)
    {}

    friend bool operator==(const Size &, const Size &) = default;
};

// Pane chrome supplied by the UI from the current cell metrics. The layout
// engine never queries FLTK (DIP 3.3). `pane_border` is the padding/border on
// each side of a pane's terminal grid; `scrollbar_width` is the scrollbar
// reserved on the pane's right edge.
struct LayoutMetrics {
    int cell_width = 1;
    int cell_height = 1;
    int pane_border = 0;
    int scrollbar_width = 0;
    int divider_thickness = 0;
};

// One terminal leaf's placement. `logical` is the pane rectangle on the logical
// canvas used to derive rows/columns (DIP 2.11); `visible` is its intersection
// with the actual content area, used to clip drawing and hit-testing (DIP 3.3).
struct PaneRect {
    TerminalId terminal;
    Rect logical;
    Rect visible;
};

// A split's divider strip, for hit-testing and drag-resize (DIP 3.5).
struct DividerRect {
    NodeId split;
    Orientation orientation;
    Rect rect;
};

struct LayoutResult {
    std::vector<PaneRect> panes;       // depth-first order
    std::vector<DividerRect> dividers; // depth-first order, thickness > 0 only
    Rect content;                      // requested content area (non-negative)
    Rect canvas;                       // logical canvas actually laid out
};

// Minimum content size the tree needs: two columns by one row per leaf plus
// pane chrome, combined bottom-up along each split axis (DIP 3.3). The caller
// adds window/tab chrome before advertising the window minimum.
[[nodiscard]] Size minimum_size(const LayoutTree &tree, const LayoutMetrics &metrics);

// Tile `content` into one rectangle per terminal leaf, clamping divider
// positions to both subtree minimums while leaving the stored ratios unchanged.
// When `content` is smaller than the tree minimum, layout at max(actual,
// minimum) per axis and clip each pane to the content area, anchored at the
// top-left (DIP 3.3).
[[nodiscard]] LayoutResult compute_layout(const LayoutTree &tree, const Rect &content,
                                          const LayoutMetrics &metrics);

} // namespace consola
