// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace consola {

// Neutral value types and defaults shared by the configuration model and the
// presentation layer. Keeping them here lets config.hpp avoid depending on the
// widget's TerminalStyle header (DIP 5.1 decoupling).

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
// model (DIP 5.5).
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

// Bell behaviour (DIP 6.2); "none" matches the v1 no-op.
enum class BellMode : std::uint8_t {
    none,
    audible,
    visual,
    both,
};

} // namespace consola
