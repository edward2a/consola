// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace consola {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    constexpr Rgb() = default;
    constexpr Rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
        : r(red), g(green), b(blue)
    {}

    friend bool operator==(Rgb, Rgb) = default;
};

inline constexpr std::array<Rgb, 16> kDefaultAnsi = {{
    {0x00, 0x00, 0x00},
    {0xab, 0x46, 0x42},
    {0xa1, 0xb5, 0x6c},
    {0xf7, 0xca, 0x88},
    {0x7c, 0xaf, 0xc2},
    {0xba, 0x8b, 0xaf},
    {0x86, 0xc1, 0xb9},
    {0xaa, 0xaa, 0xaa},
    {0x55, 0x55, 0x55},
    {0xab, 0x46, 0x42},
    {0xa1, 0xb5, 0x6c},
    {0xf7, 0xca, 0x88},
    {0x7c, 0xaf, 0xc2},
    {0xba, 0x8b, 0xaf},
    {0x86, 0xc1, 0xb9},
    {0xff, 0xff, 0xff},
}};

// Presentation/interaction defaults shared by the widget and the configuration
// model (DIP 5.5). They live here because config.hpp includes this header, not
// the other way round.
inline constexpr std::string_view kDefaultFontFamily = "Inconsolata Condensed";
inline constexpr int kDefaultFontSize = 14;
inline constexpr std::string_view kDefaultWordChars = "-A-Za-z0-9,./?%&#:_=+@~";
inline constexpr std::size_t kDefaultMaxPasteBytes = std::size_t{16} << 20; // 16 MiB
inline constexpr int kDefaultScrollback = 10000;
inline constexpr int kDefaultWheelLines = 3;

// Cursor presentation (DIP 2.4, watchlist #18).
enum class CursorShape : std::uint8_t {
    block,
    underline,
    bar,
};

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

    // Interaction settings (DIP 2.6, 2.9).
    int scrollback = kDefaultScrollback;
    bool scrollbar = true;
    int wheel_lines = kDefaultWheelLines;
    bool alternate_scroll = true;
    bool backspace_sends_delete = true;
    CursorShape cursor_shape = CursorShape::block;
    bool cursor_blink = true;
};

inline const TerminalStyle &default_terminal_style()
{
    static const TerminalStyle style{};
    return style;
}

} // namespace consola
