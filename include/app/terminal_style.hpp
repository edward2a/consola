// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <app/visual.hpp>

namespace consola {

// Plain styling and interaction data with C++ defaults centralized from day one
// (DIP 2.3, 5.5). Stage 5 only changes where this is filled from, not the
// widget code.
struct TerminalStyle {
    std::array<Rgb, 16> ansi = kDefaultAnsi; // 0-7 normal, 8-15 bright
    Rgb foreground{0xd8, 0xd8, 0xd8};
    Rgb background{0x18, 0x18, 0x18};
    // Cursor colour; the renderer currently derives cursor colours from the
    // cell (watchlist #18).
    Rgb cursor{0xff, 0xff, 0xff};
    std::string font_family{kDefaultFontFamily};
    int font_size = kDefaultFontSize;
    // Characters that form a word for double-click selection (DIP 2.7).
    std::string word_chars{kDefaultWordChars};
    // Maximum encoded paste size including framing (DIP 1.7/2.8).
    std::size_t max_paste_bytes = kDefaultMaxPasteBytes;

    // Interaction settings (DIP 2.6, 2.9, 6.2).
    int scrollback = kDefaultScrollback;
    bool scrollbar = true;
    int wheel_lines = kDefaultWheelLines;
    bool alternate_scroll = true;
    bool backspace_sends_delete = true;
    CursorShape cursor_shape = CursorShape::block;
    bool cursor_blink = true;
    // Carried for the Stage 6 bell implementation; the v1 handler is a no-op.
    BellMode bell = BellMode::none;
};

inline const TerminalStyle &default_terminal_style()
{
    static const TerminalStyle style{};
    return style;
}

} // namespace consola
