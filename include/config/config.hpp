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

#include <app/visual.hpp>
#include <config/defaults.hpp>
#include <config/layout_config.hpp>

namespace consola {

// Schema version (DIP 5.2). Bumped when the on-disk format changes
// incompatibly; the loader rejects versions it does not understand.
inline constexpr int kConfigVersion = 1;

// Configuration-only defaults (DIP 5.5). Style/interaction defaults shared with
// the widget live in app/terminal_style.hpp.
inline constexpr int kMinFontSize = 1;
inline constexpr int kMaxFontSize = 512;
inline constexpr std::size_t kDefaultOutgoingLimitBytes = std::size_t{1} << 20; // 1 MiB
// The outgoing limit must stay comfortably above its internal control reserve
// (16 KiB, DIP 1.7) so normal payloads are not starved by framing.
inline constexpr std::size_t kMinOutgoingLimitBytes = std::size_t{32} << 10; // 32 KiB
// Upper bounds keep a mistyped value (e.g. SIZE_MAX) from driving an oversized
// allocation or overflowing framing arithmetic; there is no unlimited setting.
inline constexpr std::size_t kMaxOutgoingLimitBytes = std::size_t{64} << 20; // 64 MiB
inline constexpr std::size_t kMaxMaxPasteBytes = std::size_t{256} << 20;     // 256 MiB
inline constexpr auto kDefaultKillTimeout = std::chrono::milliseconds{2000};
inline constexpr auto kMaxKillTimeout = std::chrono::milliseconds{600'000}; // 10 min
inline constexpr std::string_view kDefaultTerm = "xterm-256color";
inline constexpr std::string_view kDefaultColorTerm = "truecolor";

struct CursorConfig {
    CursorShape shape = CursorShape::block;
    bool blink = true;

    friend bool operator==(const CursorConfig &, const CursorConfig &) = default;
};

// Per-profile cursor overrides; an absent field inherits from `general.cursor`.
struct CursorOverrides {
    std::optional<CursorShape> shape;
    std::optional<bool> blink;

    friend bool operator==(const CursorOverrides &, const CursorOverrides &) = default;
};

struct FontConfig {
    std::string family{kDefaultFontFamily};
    int size = kDefaultFontSize;

    friend bool operator==(const FontConfig &, const FontConfig &) = default;
};

// Per-profile font overrides; an absent field inherits from the top-level font.
struct FontOverrides {
    std::optional<std::string> family;
    std::optional<int> size;

    friend bool operator==(const FontOverrides &, const FontOverrides &) = default;
};

// Fully resolved colours: 16 ANSI entries (0-7 normal, 8-15 bright) plus
// foreground, background and cursor.
struct ColourConfig {
    std::array<Rgb, 16> ansi = kDefaultAnsi;
    Rgb foreground{0xd8, 0xd8, 0xd8};
    Rgb background{0x18, 0x18, 0x18};
    Rgb cursor{0xff, 0xff, 0xff};

    friend bool operator==(const ColourConfig &, const ColourConfig &) = default;
};

// A profile carries only the settings it overrides; an absent field inherits
// the general/font/defaults layer, so partial palette overrides are lossless.
struct ColourOverrides {
    std::array<std::optional<Rgb>, 16> ansi{};
    std::optional<Rgb> foreground;
    std::optional<Rgb> background;
    std::optional<Rgb> cursor;

    friend bool operator==(const ColourOverrides &, const ColourOverrides &) = default;
};

// Application-wide defaults and behaviour. Some fields (shell, TERM, font,
// scrollback, word characters, bell, cursor, colours) can be overridden per
// profile; the rest are global.
struct GeneralConfig {
    // Empty means "the user's login shell" ($SHELL, or /bin/sh when unset);
    // resolved at the wiring boundary so the model stays environment-free.
    std::string shell;
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

    friend bool operator==(const GeneralConfig &, const GeneralConfig &) = default;
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
    FontOverrides font;
    std::optional<std::string> word_chars;
    std::optional<BellMode> bell;
    CursorOverrides cursor;
    ColourOverrides colours;

    friend bool operator==(const ProfileConfig &, const ProfileConfig &) = default;
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
    std::string toggle_broadcast = "Ctrl+Shift+B";

    friend bool operator==(const ShortcutConfig &, const ShortcutConfig &) = default;
};

// The whole parsed configuration. Raw YAML never reaches the application;
// every consumer sees these types (DIP 5.1). Saved layouts are data only and
// are never reconstructed into live tabs here (DIP 5.6).
struct ApplicationConfig {
    int version = kConfigVersion;
    GeneralConfig general;
    FontConfig font;
    ColourConfig colours;
    std::string default_profile{kDefaultProfileName};
    // Invariant (validated by the loader): always contains default_profile, and
    // default_profile names an existing entry. A profile with no fields set
    // inherits every default.
    std::map<std::string, ProfileConfig> profiles;
    ShortcutConfig shortcuts;
    // Named saved layouts; pane profiles must reference `profiles`.
    std::map<std::string, LayoutConfig> layouts;

    friend bool operator==(const ApplicationConfig &, const ApplicationConfig &) = default;
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
[[nodiscard]] bool valid_kill_timeout(std::chrono::milliseconds timeout) noexcept;

// Overlay the present colour overrides onto a base palette (DIP 5.1
// resolution); absent entries leave the base untouched.
void apply_colour_overrides(ColourConfig &base, const ColourOverrides &overrides);

} // namespace consola
