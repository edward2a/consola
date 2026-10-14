// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <optional>

#include <layout/layout_geometry.hpp>

namespace consola {

// Application-level pane and tab commands (DIP 3.6, 4.2). Shortcuts are mapped
// to these separately by command_for_key().
enum class Command : std::uint8_t {
    SplitHorizontal,
    SplitVertical,
    ClosePane,
    FocusLeft,
    FocusRight,
    FocusUp,
    FocusDown,
    ResizePane,
    NewTab,
    CloseTab,
    NextTab,
    PreviousTab,
};

// A resolved shortcut: the command plus the direction it applies to. The
// direction is used by ResizePane (and is the same as the focus command's
// direction for the Focus* commands).
struct CommandRequest {
    Command command = Command::ClosePane;
    FocusDirection direction = FocusDirection::left;
};

// Map an FLTK key code and modifier state (FL_CTRL/FL_SHIFT/FL_ALT) to a
// command, or nullopt when the key is not a shortcut.
[[nodiscard]] std::optional<CommandRequest> command_for_key(int key, int state);

} // namespace consola
