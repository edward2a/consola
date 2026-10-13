// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace consola {

enum class MouseButton : std::uint8_t { left, middle, right, wheel_up, wheel_down };

enum MouseModifier : std::uint8_t {
    MouseModifierShift = 1U << 0,
    MouseModifierCtrl = 1U << 1,
    MouseModifierAlt = 1U << 2,
    MouseModifierMeta = 1U << 3,
};

enum MouseAction : std::uint8_t {
    MouseActionPressed = 1U << 0,
    MouseActionReleased = 1U << 1,
    MouseActionMoved = 1U << 2,
};

// Terminal mouse reporting state (DIP 2.7), mapped from libtsm's tracking mode.
enum class MouseTracking : std::uint8_t {
    disabled,
    click, // report press/release
    any,   // also report motion
};

// Platform-neutral mouse event; the emulator encodes it with libtsm.
struct MouseEvent {
    int cell_x = 0;
    int cell_y = 0;
    int pixel_x = 0;
    int pixel_y = 0;
    MouseButton button = MouseButton::left;
    unsigned action = 0; // MouseAction bits
    unsigned modifiers = 0;
    // True when a button is held during a move (a drag rather than a hover).
    bool dragging = false;
};

} // namespace consola
