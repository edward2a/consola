// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace consola {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    constexpr Rgb() = default;
    constexpr Rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
        : r(red), g(green), b(blue)
    {}
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

// Plain styling data with C++ defaults centralized from day one (DIP 2.3).
// Stage 5 only changes where this is filled from, not the widget code.
struct TerminalStyle {
    std::array<Rgb, 16> ansi = kDefaultAnsi; // 0-7 normal, 8-15 bright
    Rgb foreground{0xd8, 0xd8, 0xd8};
    Rgb background{0x18, 0x18, 0x18};
    // Reserved for cursor styling (watchlist #18); the renderer currently
    // derives cursor colours from the cell (DIP 2.4).
    Rgb cursor{0xff, 0xff, 0xff};
    std::string font_family{"Inconsolata Condensed"};
    int font_size = 14;
    // Characters that form a word for double-click selection (DIP 2.7).
    std::string word_chars{"-A-Za-z0-9,./?%&#:_=+@~"};
};

inline const TerminalStyle &default_terminal_style()
{
    static const TerminalStyle style{};
    return style;
}

} // namespace consola
