// SPDX-License-Identifier: MIT

#include <config/loader.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ryml.hpp>

namespace consola {
namespace {

// Non-fatal diagnostics (unknown keys) and the first fatal error. Parsing is
// abandoned at the first error so no partial configuration is ever applied
// (DIP 5.3).
struct Diagnostics {
    std::vector<std::string> warnings;
    std::optional<std::string> error;

    bool fail(std::string message)
    {
        if (!error.has_value()) {
            error = std::move(message);
        }
        return false;
    }
};

std::string to_std(ryml::csubstr value)
{
    std::string out;
    if (value.len != 0) {
        out.assign(value.str, value.len);
    }
    return out;
}

std::string joined(std::initializer_list<std::string_view> parts)
{
    std::size_t total = 0;
    for (const std::string_view part : parts) {
        total += part.size();
    }
    std::string out;
    out.reserve(total);
    for (const std::string_view part : parts) {
        out.append(part);
    }
    return out;
}

std::string lower_copy(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char character : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return out;
}

bool is_blank(std::string_view text)
{
    return std::ranges::all_of(text, [](char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

bool read_string(ryml::ConstNodeRef node, std::string &out)
{
    if (!node.has_val()) {
        return false;
    }
    out = to_std(node.val());
    return true;
}

bool read_bool(ryml::ConstNodeRef node, bool &out)
{
    if (!node.has_val()) {
        return false;
    }
    const std::string value = lower_copy(to_std(node.val()));
    if (value == "true") {
        out = true;
        return true;
    }
    if (value == "false") {
        out = false;
        return true;
    }
    return false;
}

// std::from_chars, unlike c4::from_chars (which wraps via atoi/atou), rejects
// out-of-range values, so an oversized limit cannot wrap into an accepted one.
bool parse_integer(std::string_view text, std::int64_t &out)
{
    if (text.empty()) {
        return false;
    }
    const char *first = text.data();
    const char *last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

bool parse_unsigned(std::string_view text, std::uint64_t &out)
{
    if (text.empty()) {
        return false;
    }
    const char *first = text.data();
    const char *last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

bool read_int(ryml::ConstNodeRef node, int &out)
{
    if (!node.has_val() || node.val().len == 0) {
        return false;
    }
    const ryml::csubstr value = node.val();
    std::int64_t parsed = 0;
    if (!parse_integer(std::string_view(value.str, value.len), parsed)) {
        return false;
    }
    if (parsed < INT32_MIN || parsed > INT32_MAX) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

bool read_size(ryml::ConstNodeRef node, std::size_t &out)
{
    if (!node.has_val() || node.val().len == 0) {
        return false;
    }
    const ryml::csubstr value = node.val();
    std::uint64_t parsed = 0;
    if (!parse_unsigned(std::string_view(value.str, value.len), parsed)) {
        return false;
    }
    if (parsed > SIZE_MAX) {
        return false;
    }
    out = static_cast<std::size_t>(parsed);
    return true;
}

bool read_float(ryml::ConstNodeRef node, float &out)
{
    if (!node.has_val() || node.val().len == 0) {
        return false;
    }
    const ryml::csubstr value = node.val();
    const char *first = value.str;
    const char *last = first + value.len;
    const std::from_chars_result result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

int hex_digit(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool parse_hex_colour(std::string_view text, Rgb &out)
{
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        return false;
    }
    unsigned value = 0;
    for (char character : text) {
        const int digit = hex_digit(character);
        if (digit < 0) {
            return false;
        }
        value = (value << 4U) | static_cast<unsigned>(digit);
    }
    out = Rgb{static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
              static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
              static_cast<std::uint8_t>(value & 0xFFU)};
    return true;
}

bool read_colour(ryml::ConstNodeRef node, Rgb &out)
{
    std::string text;
    if (!read_string(node, text)) {
        return false;
    }
    return parse_hex_colour(text, out);
}

std::optional<std::size_t> palette_index(std::string_view name)
{
    const std::string lowered = lower_copy(name);
    if (lowered == "black") {
        return 0;
    }
    if (lowered == "red") {
        return 1;
    }
    if (lowered == "green") {
        return 2;
    }
    if (lowered == "yellow") {
        return 3;
    }
    if (lowered == "blue") {
        return 4;
    }
    if (lowered == "magenta") {
        return 5;
    }
    if (lowered == "cyan") {
        return 6;
    }
    if (lowered == "white") {
        return 7;
    }
    if (lowered == "bright_black") {
        return 8;
    }
    if (lowered == "bright_red") {
        return 9;
    }
    if (lowered == "bright_green") {
        return 10;
    }
    if (lowered == "bright_yellow") {
        return 11;
    }
    if (lowered == "bright_blue") {
        return 12;
    }
    if (lowered == "bright_magenta") {
        return 13;
    }
    if (lowered == "bright_cyan") {
        return 14;
    }
    if (lowered == "bright_white") {
        return 15;
    }
    return std::nullopt;
}

bool parse_bell(ryml::ConstNodeRef node, BellMode &out)
{
    std::string text;
    if (!read_string(node, text)) {
        return false;
    }
    const std::string value = lower_copy(text);
    if (value == "none") {
        out = BellMode::none;
    } else if (value == "audible") {
        out = BellMode::audible;
    } else if (value == "visual") {
        out = BellMode::visual;
    } else if (value == "both") {
        out = BellMode::both;
    } else {
        return false;
    }
    return true;
}

template <class Set>
bool parse_palette(ryml::ConstNodeRef node, Diagnostics &diag, const std::string &prefix, Set set)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string name = to_std(child.key());
        const std::optional<std::size_t> index = palette_index(name);
        if (!index.has_value()) {
            diag.warnings.push_back(joined({prefix, ": unknown colour '", name, "'"}));
            continue;
        }
        Rgb colour;
        if (!read_colour(child, colour)) {
            return diag.fail(joined({prefix, ".", name, ": expected a #rrggbb colour"}));
        }
        set(index.value(), colour);
    }
    return true;
}

bool parse_cursor(ryml::ConstNodeRef node, CursorConfig &cursor, Diagnostics &diag,
                  const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "shape") {
            std::string text;
            if (!read_string(child, text)) {
                return diag.fail(prefix + ".shape: expected a string");
            }
            const std::string value = lower_copy(text);
            if (value == "block") {
                cursor.shape = CursorShape::block;
            } else if (value == "underline") {
                cursor.shape = CursorShape::underline;
            } else if (value == "bar") {
                cursor.shape = CursorShape::bar;
            } else {
                return diag.fail(prefix + ".shape: expected block, underline or bar");
            }
        } else if (key == "blink") {
            bool value = false;
            if (!read_bool(child, value)) {
                return diag.fail(prefix + ".blink: expected true or false");
            }
            cursor.blink = value;
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    return true;
}

bool parse_cursor_overrides(ryml::ConstNodeRef node, CursorOverrides &cursor, Diagnostics &diag,
                            const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "shape") {
            std::string text;
            if (!read_string(child, text)) {
                return diag.fail(prefix + ".shape: expected a string");
            }
            const std::string value = lower_copy(text);
            if (value == "block") {
                cursor.shape = CursorShape::block;
            } else if (value == "underline") {
                cursor.shape = CursorShape::underline;
            } else if (value == "bar") {
                cursor.shape = CursorShape::bar;
            } else {
                return diag.fail(prefix + ".shape: expected block, underline or bar");
            }
        } else if (key == "blink") {
            bool value = false;
            if (!read_bool(child, value)) {
                return diag.fail(prefix + ".blink: expected true or false");
            }
            cursor.blink = value;
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    return true;
}

bool parse_font(ryml::ConstNodeRef node, FontConfig &font, Diagnostics &diag,
                const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "family") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".family: expected a string");
            }
            font.family = std::move(value);
        } else if (key == "size") {
            int value = 0;
            if (!read_int(child, value)) {
                return diag.fail(prefix + ".size: expected an integer");
            }
            if (!valid_font_size(value)) {
                return diag.fail(
                    joined({prefix, ".size: must be between ", std::to_string(kMinFontSize),
                            " and ", std::to_string(kMaxFontSize)}));
            }
            font.size = value;
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    return true;
}

bool parse_font_overrides(ryml::ConstNodeRef node, FontOverrides &font, Diagnostics &diag,
                          const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "family") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".family: expected a string");
            }
            font.family = std::move(value);
        } else if (key == "size") {
            int value = 0;
            if (!read_int(child, value)) {
                return diag.fail(prefix + ".size: expected an integer");
            }
            if (!valid_font_size(value)) {
                return diag.fail(
                    joined({prefix, ".size: must be between ", std::to_string(kMinFontSize),
                            " and ", std::to_string(kMaxFontSize)}));
            }
            font.size = value;
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    return true;
}

bool parse_colours(ryml::ConstNodeRef node, ColourConfig &colours, Diagnostics &diag,
                   const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "foreground") {
            if (!read_colour(child, colours.foreground)) {
                return diag.fail(prefix + ".foreground: expected a #rrggbb colour");
            }
        } else if (key == "background") {
            if (!read_colour(child, colours.background)) {
                return diag.fail(prefix + ".background: expected a #rrggbb colour");
            }
        } else if (key == "cursor") {
            if (!read_colour(child, colours.cursor)) {
                return diag.fail(prefix + ".cursor: expected a #rrggbb colour");
            }
        } else if (key == "palette") {
            if (!parse_palette(
                    child, diag, prefix + ".palette",
                    [&colours](std::size_t index, Rgb value) { colours.ansi[index] = value; })) {
                return false;
            }
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    return true;
}

bool parse_colour_overrides(ryml::ConstNodeRef node, ColourOverrides &colours, Diagnostics &diag,
                            const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "foreground") {
            Rgb value;
            if (!read_colour(child, value)) {
                return diag.fail(prefix + ".foreground: expected a #rrggbb colour");
            }
            colours.foreground = value;
        } else if (key == "background") {
            Rgb value;
            if (!read_colour(child, value)) {
                return diag.fail(prefix + ".background: expected a #rrggbb colour");
            }
            colours.background = value;
        } else if (key == "cursor") {
            Rgb value;
            if (!read_colour(child, value)) {
                return diag.fail(prefix + ".cursor: expected a #rrggbb colour");
            }
            colours.cursor = value;
        } else if (key == "palette") {
            if (!parse_palette(
                    child, diag, prefix + ".palette",
                    [&colours](std::size_t index, Rgb value) { colours.ansi[index] = value; })) {
                return false;
            }
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    return true;
}

bool parse_arguments(ryml::ConstNodeRef node, std::vector<std::string> &out, Diagnostics &diag,
                     const std::string &prefix)
{
    if (!node.is_seq()) {
        return diag.fail(prefix + ": expected a list of strings");
    }
    std::vector<std::string> arguments;
    for (ryml::ConstNodeRef item : node.children()) {
        if (!item.has_val()) {
            return diag.fail(prefix + ": expected a list of strings");
        }
        arguments.push_back(to_std(item.val()));
    }
    out = std::move(arguments);
    return true;
}

bool parse_general(ryml::ConstNodeRef node, GeneralConfig &general, Diagnostics &diag)
{
    if (!node.is_map()) {
        return diag.fail("general: expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "shell") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail("general.shell: expected a string");
            }
            general.shell = std::move(value);
        } else if (key == "arguments") {
            if (!parse_arguments(child, general.arguments, diag, "general.arguments")) {
                return false;
            }
        } else if (key == "working_directory") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail("general.working_directory: expected a string");
            }
            general.working_directory = std::move(value);
        } else if (key == "login_shell") {
            bool value = false;
            if (!read_bool(child, value)) {
                return diag.fail("general.login_shell: expected true or false");
            }
            general.login_shell = value;
        } else if (key == "term") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail("general.term: expected a string");
            }
            general.term = std::move(value);
        } else if (key == "colorterm") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail("general.colorterm: expected a string");
            }
            general.colorterm = std::move(value);
        } else if (key == "scrollback") {
            int value = 0;
            if (!read_int(child, value)) {
                return diag.fail("general.scrollback: expected an integer");
            }
            if (!valid_scrollback(value)) {
                return diag.fail("general.scrollback: must be >= 0");
            }
            general.scrollback = value;
        } else if (key == "scrollbar") {
            bool value = false;
            if (!read_bool(child, value)) {
                return diag.fail("general.scrollbar: expected true or false");
            }
            general.scrollbar = value;
        } else if (key == "wheel_lines") {
            int value = 0;
            if (!read_int(child, value)) {
                return diag.fail("general.wheel_lines: expected an integer");
            }
            if (!valid_wheel_lines(value)) {
                return diag.fail("general.wheel_lines: must be > 0");
            }
            general.wheel_lines = value;
        } else if (key == "alternate_scroll") {
            bool value = false;
            if (!read_bool(child, value)) {
                return diag.fail("general.alternate_scroll: expected true or false");
            }
            general.alternate_scroll = value;
        } else if (key == "backspace_sends_delete") {
            bool value = false;
            if (!read_bool(child, value)) {
                return diag.fail("general.backspace_sends_delete: expected true or false");
            }
            general.backspace_sends_delete = value;
        } else if (key == "bell") {
            BellMode value = BellMode::none;
            if (!parse_bell(child, value)) {
                return diag.fail("general.bell: expected none, audible, visual or both");
            }
            general.bell = value;
        } else if (key == "cursor") {
            if (!parse_cursor(child, general.cursor, diag, "general.cursor")) {
                return false;
            }
        } else if (key == "word_chars") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail("general.word_chars: expected a string");
            }
            general.word_chars = std::move(value);
        } else if (key == "kill_timeout_ms") {
            int value = 0;
            if (!read_int(child, value)) {
                return diag.fail("general.kill_timeout_ms: expected an integer");
            }
            const std::chrono::milliseconds timeout{value};
            if (!valid_kill_timeout(timeout)) {
                return diag.fail(joined({"general.kill_timeout_ms: must be between 1 and ",
                                         std::to_string(kMaxKillTimeout.count())}));
            }
            general.kill_timeout = timeout;
        } else if (key == "drain_output_on_close") {
            bool value = false;
            if (!read_bool(child, value)) {
                return diag.fail("general.drain_output_on_close: expected true or false");
            }
            general.drain_output_on_close = value;
        } else if (key == "outgoing_limit_bytes") {
            std::size_t value = 0;
            if (!read_size(child, value)) {
                return diag.fail("general.outgoing_limit_bytes: expected an integer");
            }
            if (!valid_outgoing_limit(value)) {
                return diag.fail(joined({"general.outgoing_limit_bytes: must be between ",
                                         std::to_string(kMinOutgoingLimitBytes), " and ",
                                         std::to_string(kMaxOutgoingLimitBytes)}));
            }
            general.outgoing_limit_bytes = value;
        } else if (key == "max_paste_bytes") {
            std::size_t value = 0;
            if (!read_size(child, value)) {
                return diag.fail("general.max_paste_bytes: expected an integer");
            }
            if (!valid_max_paste_bytes(value)) {
                return diag.fail(joined({"general.max_paste_bytes: must be between 1 and ",
                                         std::to_string(kMaxMaxPasteBytes)}));
            }
            general.max_paste_bytes = value;
        } else {
            diag.warnings.push_back("general: unknown key '" + key + "'");
        }
    }
    return true;
}

bool parse_profile(ryml::ConstNodeRef node, ProfileConfig &profile, Diagnostics &diag,
                   const std::string &name)
{
    const std::string prefix = "profiles." + name;
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "shell") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".shell: expected a string");
            }
            profile.shell = std::move(value);
        } else if (key == "arguments") {
            std::vector<std::string> arguments;
            if (!parse_arguments(child, arguments, diag, prefix + ".arguments")) {
                return false;
            }
            profile.arguments = std::move(arguments);
        } else if (key == "working_directory") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".working_directory: expected a string");
            }
            profile.working_directory = std::move(value);
        } else if (key == "login_shell") {
            bool value = false;
            if (!read_bool(child, value)) {
                return diag.fail(prefix + ".login_shell: expected true or false");
            }
            profile.login_shell = value;
        } else if (key == "term") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".term: expected a string");
            }
            profile.term = std::move(value);
        } else if (key == "colorterm") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".colorterm: expected a string");
            }
            profile.colorterm = std::move(value);
        } else if (key == "scrollback") {
            int value = 0;
            if (!read_int(child, value)) {
                return diag.fail(prefix + ".scrollback: expected an integer");
            }
            if (!valid_scrollback(value)) {
                return diag.fail(prefix + ".scrollback: must be >= 0");
            }
            profile.scrollback = value;
        } else if (key == "font") {
            if (!parse_font_overrides(child, profile.font, diag, prefix + ".font")) {
                return false;
            }
        } else if (key == "word_chars") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".word_chars: expected a string");
            }
            profile.word_chars = std::move(value);
        } else if (key == "bell") {
            BellMode value = BellMode::none;
            if (!parse_bell(child, value)) {
                return diag.fail(prefix + ".bell: expected none, audible, visual or both");
            }
            profile.bell = value;
        } else if (key == "cursor") {
            if (!parse_cursor_overrides(child, profile.cursor, diag, prefix + ".cursor")) {
                return false;
            }
        } else if (key == "colours") {
            if (!parse_colour_overrides(child, profile.colours, diag, prefix + ".colours")) {
                return false;
            }
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    return true;
}

bool parse_profiles(ryml::ConstNodeRef node, ApplicationConfig &config, Diagnostics &diag)
{
    if (!node.is_map()) {
        return diag.fail("profiles: expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string name = to_std(child.key());
        if (name.empty()) {
            return diag.fail("profiles: profile name must not be empty");
        }
        ProfileConfig profile;
        if (!parse_profile(child, profile, diag, name)) {
            return false;
        }
        config.profiles[name] = std::move(profile);
    }
    return true;
}

bool parse_shortcuts(ryml::ConstNodeRef node, ShortcutConfig &shortcuts, Diagnostics &diag)
{
    if (!node.is_map()) {
        return diag.fail("shortcuts: expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        std::string *field = nullptr;
        if (key == "new_tab") {
            field = &shortcuts.new_tab;
        } else if (key == "close_tab") {
            field = &shortcuts.close_tab;
        } else if (key == "close_pane") {
            field = &shortcuts.close_pane;
        } else if (key == "split_horizontal") {
            field = &shortcuts.split_horizontal;
        } else if (key == "split_vertical") {
            field = &shortcuts.split_vertical;
        } else if (key == "next_tab") {
            field = &shortcuts.next_tab;
        } else if (key == "previous_tab") {
            field = &shortcuts.previous_tab;
        } else if (key == "focus_left") {
            field = &shortcuts.focus_left;
        } else if (key == "focus_right") {
            field = &shortcuts.focus_right;
        } else if (key == "focus_up") {
            field = &shortcuts.focus_up;
        } else if (key == "focus_down") {
            field = &shortcuts.focus_down;
        } else if (key == "resize_left") {
            field = &shortcuts.resize_left;
        } else if (key == "resize_right") {
            field = &shortcuts.resize_right;
        } else if (key == "resize_up") {
            field = &shortcuts.resize_up;
        } else if (key == "resize_down") {
            field = &shortcuts.resize_down;
        } else if (key == "cancel_paste") {
            field = &shortcuts.cancel_paste;
        } else if (key == "toggle_broadcast") {
            field = &shortcuts.toggle_broadcast;
        }
        if (field == nullptr) {
            diag.warnings.push_back("shortcuts: unknown key '" + key + "'");
            continue;
        }
        std::string value;
        if (!read_string(child, value)) {
            return diag.fail("shortcuts." + key + ": expected a string");
        }
        *field = std::move(value);
    }
    return true;
}

bool parse_layout_pane(ryml::ConstNodeRef node, LayoutPaneConfig &pane, Diagnostics &diag,
                       const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "id") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".id: expected a string");
            }
            if (value.empty()) {
                return diag.fail(prefix + ".id: must not be empty");
            }
            pane.id = std::move(value);
        } else if (key == "profile") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".profile: expected a string");
            }
            if (value.empty()) {
                return diag.fail(prefix + ".profile: must not be empty");
            }
            pane.profile = std::move(value);
        } else if (key == "working_directory") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail(prefix + ".working_directory: expected a string");
            }
            pane.working_directory = std::move(value);
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    if (pane.id.empty()) {
        return diag.fail(prefix + ": pane id must not be empty");
    }
    return true;
}

bool parse_layout_split(ryml::ConstNodeRef node, LayoutNodeConfig &split, Diagnostics &diag,
                        const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "orientation") {
            std::string text;
            if (!read_string(child, text)) {
                return diag.fail(prefix + ".orientation: expected a string");
            }
            const std::string value = lower_copy(text);
            if (value == "horizontal") {
                split.orientation = Orientation::horizontal;
            } else if (value == "vertical") {
                split.orientation = Orientation::vertical;
            } else {
                return diag.fail(prefix + ".orientation: expected horizontal or vertical");
            }
        } else if (key == "ratio") {
            float value = 0.0F;
            if (!read_float(child, value)) {
                return diag.fail(prefix + ".ratio: expected a number");
            }
            if (!std::isfinite(value) || value <= 0.0F || value >= 1.0F) {
                return diag.fail(prefix + ".ratio: must be strictly between 0 and 1");
            }
            split.ratio = value;
        } else if (key == "children") {
            if (!child.is_seq()) {
                return diag.fail(prefix + ".children: expected a list of node indices");
            }
            split.children.clear();
            for (ryml::ConstNodeRef item : child.children()) {
                std::size_t index = 0;
                if (!read_size(item, index)) {
                    return diag.fail(prefix + ".children: expected a list of node indices");
                }
                split.children.push_back(index);
            }
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    if (split.children.size() != 2) {
        return diag.fail(prefix + ": a split must have exactly two children");
    }
    return true;
}

bool parse_layout_node(ryml::ConstNodeRef node, LayoutNodeConfig &result, Diagnostics &diag,
                       const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    bool has_split = false;
    bool has_pane = false;
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "split") {
            if (has_pane || has_split) {
                return diag.fail(prefix + ": a node is exactly one of 'split' or 'pane'");
            }
            has_split = true;
            if (!parse_layout_split(child, result, diag, prefix + ".split")) {
                return false;
            }
        } else if (key == "pane") {
            if (has_split || has_pane) {
                return diag.fail(prefix + ": a node is exactly one of 'split' or 'pane'");
            }
            has_pane = true;
            LayoutPaneConfig pane;
            if (!parse_layout_pane(child, pane, diag, prefix + ".pane")) {
                return false;
            }
            result.pane = std::move(pane);
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    if (has_split == has_pane) {
        return diag.fail(prefix + ": expected exactly one of 'split' or 'pane'");
    }
    return true;
}

bool validate_layout_tab(const LayoutTabConfig &tab, Diagnostics &diag, const std::string &prefix)
{
    if (tab.nodes.empty()) {
        return diag.fail(prefix + ": a tab must have at least one node");
    }
    if (tab.root >= tab.nodes.size()) {
        return diag.fail(prefix + ".root: index out of range");
    }
    if (tab.focused.empty()) {
        return diag.fail(prefix + ".focused: must not be empty");
    }

    std::vector<bool> visited(tab.nodes.size(), false);
    std::vector<std::size_t> stack;
    stack.push_back(tab.root);
    std::vector<std::string> pane_ids;
    while (!stack.empty()) {
        const std::size_t index = stack.back();
        stack.pop_back();
        if (index >= tab.nodes.size()) {
            return diag.fail(prefix + ": node index out of range");
        }
        if (visited[index]) {
            return diag.fail(prefix + ": node graph contains a cycle or shared child");
        }
        visited[index] = true;
        const LayoutNodeConfig &node = tab.nodes[index];
        if (node.pane.has_value()) {
            if (!node.children.empty()) {
                return diag.fail(prefix + ": a pane node must not have children");
            }
            pane_ids.push_back(node.pane->id);
        } else {
            if (node.children.size() != 2) {
                return diag.fail(prefix + ": a split must have exactly two children");
            }
            if (!std::isfinite(node.ratio) || node.ratio <= 0.0F || node.ratio >= 1.0F) {
                return diag.fail(prefix + ": split ratio must be strictly between 0 and 1");
            }
            stack.push_back(node.children[0]);
            stack.push_back(node.children[1]);
        }
    }
    for (std::size_t index = 0; index < visited.size(); ++index) {
        if (!visited[index]) {
            return diag.fail(prefix + ": unreachable node at index " + std::to_string(index));
        }
    }
    std::ranges::sort(pane_ids);
    if (std::ranges::adjacent_find(pane_ids) != pane_ids.end()) {
        return diag.fail(prefix + ": duplicate pane id");
    }
    if (std::ranges::find(pane_ids, tab.focused) == pane_ids.end()) {
        return diag.fail(prefix + ".focused: '" + tab.focused + "' is not a pane in this tab");
    }
    return true;
}

bool parse_layout_tab(ryml::ConstNodeRef node, LayoutTabConfig &tab, Diagnostics &diag,
                      const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "root") {
            if (!read_size(child, tab.root)) {
                return diag.fail(prefix + ".root: expected an integer");
            }
        } else if (key == "focused") {
            if (!read_string(child, tab.focused)) {
                return diag.fail(prefix + ".focused: expected a string");
            }
        } else if (key == "nodes") {
            if (!child.is_seq()) {
                return diag.fail(prefix + ".nodes: expected a list");
            }
            tab.nodes.clear();
            std::size_t index = 0;
            for (ryml::ConstNodeRef item : child.children()) {
                LayoutNodeConfig layout_node;
                if (!parse_layout_node(item, layout_node, diag,
                                       prefix + ".nodes[" + std::to_string(index) + "]")) {
                    return false;
                }
                tab.nodes.push_back(std::move(layout_node));
                ++index;
            }
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    return validate_layout_tab(tab, diag, prefix);
}

bool parse_layout(ryml::ConstNodeRef node, LayoutConfig &layout, Diagnostics &diag,
                  const std::string &prefix)
{
    if (!node.is_map()) {
        return diag.fail(prefix + ": expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string key = to_std(child.key());
        if (key == "active_tab") {
            if (!read_size(child, layout.active_tab)) {
                return diag.fail(prefix + ".active_tab: expected an integer");
            }
        } else if (key == "tabs") {
            if (!child.is_seq()) {
                return diag.fail(prefix + ".tabs: expected a list");
            }
            layout.tabs.clear();
            std::size_t index = 0;
            for (ryml::ConstNodeRef item : child.children()) {
                LayoutTabConfig tab;
                if (!parse_layout_tab(item, tab, diag,
                                      prefix + ".tabs[" + std::to_string(index) + "]")) {
                    return false;
                }
                layout.tabs.push_back(std::move(tab));
                ++index;
            }
        } else {
            diag.warnings.push_back(joined({prefix, ": unknown key '", key, "'"}));
        }
    }
    if (layout.tabs.empty()) {
        return diag.fail(prefix + ": a layout must have at least one tab");
    }
    if (layout.active_tab >= layout.tabs.size()) {
        return diag.fail(prefix + ".active_tab: index out of range");
    }
    return true;
}

bool parse_layouts(ryml::ConstNodeRef node, ApplicationConfig &config, Diagnostics &diag)
{
    if (!node.is_map()) {
        return diag.fail("layouts: expected a map");
    }
    for (ryml::ConstNodeRef child : node.children()) {
        const std::string name = to_std(child.key());
        if (name.empty()) {
            return diag.fail("layouts: layout name must not be empty");
        }
        LayoutConfig layout;
        if (!parse_layout(child, layout, diag, "layouts." + name)) {
            return false;
        }
        config.layouts[name] = std::move(layout);
    }
    return true;
}

bool validate_layout_profiles(const ApplicationConfig &config, Diagnostics &diag)
{
    for (const auto &[name, layout] : config.layouts) {
        for (const LayoutTabConfig &tab : layout.tabs) {
            for (const LayoutNodeConfig &node : tab.nodes) {
                if (!node.pane.has_value()) {
                    continue;
                }
                const std::string &profile = node.pane.value().profile;
                if (!config.profiles.contains(profile)) {
                    return diag.fail(
                        joined({"layouts.", name, ": profile '", profile, "' is not defined"}));
                }
            }
        }
    }
    return true;
}

bool parse_root(ryml::ConstNodeRef root, ApplicationConfig &config, Diagnostics &diag)
{
    for (ryml::ConstNodeRef child : root.children()) {
        const std::string key = to_std(child.key());
        if (key == "version") {
            continue; // handled by the caller
        }
        if (key == "general") {
            if (!parse_general(child, config.general, diag)) {
                return false;
            }
        } else if (key == "font") {
            if (!parse_font(child, config.font, diag, "font")) {
                return false;
            }
        } else if (key == "colours") {
            if (!parse_colours(child, config.colours, diag, "colours")) {
                return false;
            }
        } else if (key == "default_profile") {
            std::string value;
            if (!read_string(child, value)) {
                return diag.fail("default_profile: expected a string");
            }
            if (value.empty()) {
                return diag.fail("default_profile: must not be empty");
            }
            config.default_profile = std::move(value);
        } else if (key == "profiles") {
            if (!parse_profiles(child, config, diag)) {
                return false;
            }
        } else if (key == "shortcuts") {
            if (!parse_shortcuts(child, config.shortcuts, diag)) {
                return false;
            }
        } else if (key == "layouts") {
            if (!parse_layouts(child, config, diag)) {
                return false;
            }
        } else {
            diag.warnings.push_back("unknown key '" + key + "'");
        }
    }
    return true;
}

} // namespace

ConfigLoadResult parse_config(std::string_view yaml)
{
    ConfigLoadResult result;
    result.config = default_application_config();

    if (is_blank(yaml)) {
        return result; // an empty document keeps the defaults
    }

    Diagnostics diag;

    ryml::Tree tree;
    try {
        tree = ryml::parse_in_arena(ryml::csubstr(yaml.data(), yaml.size()));
    } catch (ryml::ExceptionBasic const &exception) {
        result.error = std::string{"malformed YAML: "} + exception.what();
        return result;
    }

    ryml::ConstNodeRef root = tree.crootref();
    if (!root.is_map()) {
        // A truly empty or comment-only document parses to a valueless node and
        // keeps the defaults; any other non-map root is rejected.
        if (!root.has_val() && !root.is_seq()) {
            return result;
        }
        result.error = "configuration root must be a map";
        return result;
    }

    ryml::ConstNodeRef version = root.find_child("version");
    if (version.invalid()) {
        result.error = "missing required 'version' key";
        return result;
    }
    int version_value = 0;
    if (!read_int(version, version_value)) {
        result.error = "version: expected an integer";
        return result;
    }
    if (version_value != kConfigVersion) {
        result.error = "unsupported configuration version " + std::to_string(version_value);
        return result;
    }

    ApplicationConfig config = default_application_config();
    if (!parse_root(root, config, diag)) {
        result.error = diag.error.value_or("invalid configuration");
        return result;
    }

    // Invariant: the profile set always contains the default profile, and the
    // configured default_profile names an existing profile.
    if (!config.profiles.contains(std::string{kDefaultProfileName})) {
        config.profiles.emplace(std::string{kDefaultProfileName}, ProfileConfig{});
    }
    if (!config.profiles.contains(config.default_profile)) {
        result.error = joined({"default_profile: '", config.default_profile, "' is not defined"});
        return result;
    }
    if (!validate_layout_profiles(config, diag)) {
        result.error = diag.error.value_or("invalid layout");
        return result;
    }

    result.config = std::move(config);
    result.warnings = std::move(diag.warnings);
    return result;
}

ConfigLoadResult load_config(const std::filesystem::path &path)
{
    ConfigLoadResult result;
    result.config = default_application_config();

    std::error_code status_error;
    const bool exists = std::filesystem::exists(path, status_error);
    if (status_error) {
        result.error = "cannot access configuration file: " + path.string();
        return result;
    }
    if (!exists) {
        return result; // a missing file is not an error
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.error = "cannot open configuration file: " + path.string();
        return result;
    }
    const std::string content((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    return parse_config(content);
}

std::filesystem::path default_config_path()
{
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / "consola" / "config.yml";
    }
    if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "consola" / "config.yml";
    }
    return std::filesystem::path(".config") / "consola" / "config.yml";
}

} // namespace consola
