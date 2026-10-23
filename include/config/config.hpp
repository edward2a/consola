// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <app/terminal_style.hpp>

namespace consola {

// Schema version (DIP 5.2). Bumped when the on-disk format changes
// incompatibly; the loader rejects versions it does not understand.
inline constexpr int kConfigVersion = 1;

// Centralized defaults (DIP 5.5). Runtime and UI code read these instead of
// repeating literals in the widget layer.
inline constexpr int kDefaultScrollback = 10000;
inline constexpr int kDefaultWheelLines = 3;
inline constexpr int kDefaultFontSize = 14;
inline constexpr int kMinFontSize = 1;
inline constexpr int kMaxFontSize = 512;
inline constexpr std::size_t kDefaultOutgoingLimitBytes = std::size_t{1} << 20; // 1 MiB
// The outgoing limit must stay comfortably above its internal control reserve
// (16 KiB, DIP 1.7) so normal payloads are not starved by framing.
inline constexpr std::size_t kMinOutgoingLimitBytes = std::size_t{32} << 10; // 32 KiB
inline constexpr std::size_t kDefaultMaxPasteBytes = std::size_t{16} << 20;  // 16 MiB
inline constexpr auto kDefaultKillTimeout = std::chrono::milliseconds{2000};
inline constexpr std::string_view kDefaultFontFamily = "Inconsolata Condensed";
inline constexpr std::string_view kDefaultWordChars = "-A-Za-z0-9,./?%&#:_=+@~";
inline constexpr std::string_view kDefaultTerm = "xterm-256color";
inline constexpr std::string_view kDefaultColorTerm = "truecolor";
inline constexpr std::string_view kDefaultProfileName = "default";

// Bell behaviour (DIP 6.2); "none" matches the v1 no-op.
enum class BellMode : std::uint8_t {
    none,
    audible,
    visual,
    both,
};

// Cursor presentation for a pane (DIP 2.4, watchlist #18).
enum class CursorShape : std::uint8_t {
    block,
    underline,
    bar,
};

struct CursorConfig {
    CursorShape shape = CursorShape::block;
    bool blink = true;
};

struct FontConfig {
    std::string family{kDefaultFontFamily};
    int size = kDefaultFontSize;
};

// Fully resolved colours: 16 ANSI entries (0-7 normal, 8-15 bright) plus
// foreground, background and cursor.
struct ColourConfig {
    std::array<Rgb, 16> ansi = kDefaultAnsi;
    Rgb foreground{0xd8, 0xd8, 0xd8};
    Rgb background{0x18, 0x18, 0x18};
    Rgb cursor{0xff, 0xff, 0xff};
};

// A profile carries only the settings it overrides; an absent field inherits
// the general/font/defaults layer, so partial palette overrides are lossless.
struct ColourOverrides {
    std::array<std::optional<Rgb>, 16> ansi{};
    std::optional<Rgb> foreground;
    std::optional<Rgb> background;
    std::optional<Rgb> cursor;
};

// Application-wide defaults and behaviour. Some fields (shell, TERM, font,
// scrollback, word characters, bell, cursor, colours) can be overridden per
// profile; the rest are global.
struct GeneralConfig {
    std::string shell = "/bin/sh";
    std::vector<std::string> arguments;
    std::string working_directory;
    bool login_shell = false;
    std::string term{kDefaultTerm};
    std::string colorterm{kDefaultColorTerm};
    int scrollback = kDefaultScrollback;
    bool scrollbar = true;
    int wheel_lines = kDefaultWheelLines;
    bool alternate_scroll = true;
    bool backspace_sends_delete = true;
    BellMode bell = BellMode::none;
    CursorConfig cursor;
    std::string word_chars{kDefaultWordChars};
    std::chrono::milliseconds kill_timeout = kDefaultKillTimeout;
    bool drain_output_on_close = false;
    std::size_t outgoing_limit_bytes = kDefaultOutgoingLimitBytes;
    std::size_t max_paste_bytes = kDefaultMaxPasteBytes;
};

// A named profile holds the per-pane settings it overrides (watchlist #19,
// precedence defined in docs/configuration.md).
struct ProfileConfig {
    std::optional<std::string> shell;
    std::optional<std::vector<std::string>> arguments;
    std::optional<std::string> working_directory;
    std::optional<bool> login_shell;
    std::optional<std::string> term;
    std::optional<std::string> colorterm;
    std::optional<int> scrollback;
    std::optional<FontConfig> font;
    std::optional<std::string> word_chars;
    std::optional<BellMode> bell;
    std::optional<CursorConfig> cursor;
    ColourOverrides colours;
};

// Global keyboard shortcuts, stored as their canonical text form; parsing into
// key codes happens at the wiring boundary (DIP 3.6, 5.2).
struct ShortcutConfig {
    std::string new_tab = "Ctrl+Shift+T";
    std::string close_tab = "Ctrl+Shift+X";
    std::string close_pane = "Ctrl+Shift+W";
    std::string split_horizontal = "Ctrl+Shift+O";
    std::string split_vertical = "Ctrl+Shift+E";
    std::string next_tab = "Ctrl+PageDown";
    std::string previous_tab = "Ctrl+PageUp";
    std::string focus_left = "Alt+Left";
    std::string focus_right = "Alt+Right";
    std::string focus_up = "Alt+Up";
    std::string focus_down = "Alt+Down";
    std::string resize_left = "Ctrl+Shift+Left";
    std::string resize_right = "Ctrl+Shift+Right";
    std::string resize_up = "Ctrl+Shift+Up";
    std::string resize_down = "Ctrl+Shift+Down";
    std::string cancel_paste = "Escape";
};

// The whole parsed configuration. Raw YAML never reaches the application;
// every consumer sees these types (DIP 5.1).
struct ApplicationConfig {
    int version = kConfigVersion;
    GeneralConfig general;
    FontConfig font;
    ColourConfig colours;
    std::string default_profile{kDefaultProfileName};
    // Always contains default_profile; a profile with no fields set inherits
    // every default.
    std::map<std::string, ProfileConfig> profiles;
    ShortcutConfig shortcuts;
};

// The built-in defaults, including the "default" profile (DIP 5.5).
[[nodiscard]] ApplicationConfig default_application_config();

// Validation used by the loader (DIP 5.3) and the model tests. Limits must be
// finite, positive and safe for framing/allocation; no dedicated error type is
// needed because callers report the offending key by name.
[[nodiscard]] bool valid_outgoing_limit(std::size_t bytes) noexcept;
[[nodiscard]] bool valid_max_paste_bytes(std::size_t bytes) noexcept;
[[nodiscard]] bool valid_scrollback(int lines) noexcept;
[[nodiscard]] bool valid_font_size(int points) noexcept;
[[nodiscard]] bool valid_wheel_lines(int lines) noexcept;

// Overlay the present colour overrides onto a base palette (DIP 5.1
// resolution); absent entries leave the base untouched.
void apply_colour_overrides(ColourConfig &base, const ColourOverrides &overrides);

} // namespace consola
