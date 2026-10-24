// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace consola {

// Direction of a split as presented to the user. This follows Terminator's
// long-standing convention, matching Command::SplitHorizontal/SplitVertical
// (DIP 3.6):
//   horizontal: a horizontal divider; children are stacked top to bottom.
//   vertical:   a vertical divider; children are placed left to right.
// Kept in its own header so saved-layout configuration does not need the whole
// runtime LayoutTree.
enum class Orientation : std::uint8_t {
    horizontal,
    vertical,
};

} // namespace consola
