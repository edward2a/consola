// SPDX-License-Identifier: MIT

#include <config/writer.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace consola {
namespace {

constexpr std::array<std::string_view, 16> kPaletteNames = {
    "black",        "red",           "green",       "yellow",         "blue",
    "magenta",      "cyan",          "white",       "bright_black",   "bright_red",
    "bright_green", "bright_yellow", "bright_blue", "bright_magenta", "bright_cyan",
    "bright_white",
};

// A YAML double-quoted scalar; quoting everything keeps the output unambiguous
// and re-parseable regardless of punctuation in the value.
std::string quote(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (byte < 0x20) {
                std::array<char, 8> buffer{};
                std::snprintf(buffer.data(), buffer.size(), "\\x%02x", byte);
                out += buffer.data();
            } else {
                out.push_back(character);
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

// YAML requires UTF-8, so the writer's contract is valid UTF-8 input. High-bit
// bytes are passed through unchanged; save_config rejects a result that is not
// valid UTF-8 rather than emitting an unparseable file.
bool is_valid_utf8(std::string_view text)
{
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        if (lead < 0x80) {
            ++index;
            continue;
        }
        std::size_t extra = 0;
        std::uint32_t codepoint = 0;
        if ((lead & 0xE0U) == 0xC0U) {
            extra = 1;
            codepoint = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            extra = 2;
            codepoint = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            extra = 3;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (index + extra >= text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= extra; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (continuation & 0x3FU);
        }
        if ((extra == 1 && codepoint < 0x80U) || (extra == 2 && codepoint < 0x800U) ||
            (extra == 3 && codepoint < 0x10000U) || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return false;
        }
        index += extra + 1;
    }
    return true;
}

std::string_view bool_text(bool value)
{
    return value ? "true" : "false";
}

std::string_view bell_text(BellMode mode)
{
    switch (mode) {
    case BellMode::none:
        return "none";
    case BellMode::audible:
        return "audible";
    case BellMode::visual:
        return "visual";
    case BellMode::both:
        return "both";
    }
    return "none";
}

std::string_view cursor_shape_text(CursorShape shape)
{
    switch (shape) {
    case CursorShape::block:
        return "block";
    case CursorShape::underline:
        return "underline";
    case CursorShape::bar:
        return "bar";
    }
    return "block";
}

std::string hex_colour(Rgb colour)
{
    std::array<char, 8> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "#%02x%02x%02x", colour.r, colour.g, colour.b);
    return buffer.data();
}

std::string flow_string_list(const std::vector<std::string> &items)
{
    std::string out = "[";
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index != 0) {
            out += ", ";
        }
        out += quote(items[index]);
    }
    out += "]";
    return out;
}

void append_line(std::string &out, int indent, std::string_view text)
{
    out.append(static_cast<std::size_t>(indent) * 2, ' ');
    out.append(text);
    out.push_back('\n');
}

void append_cursor(std::string &out, const CursorConfig &cursor, int indent)
{
    append_line(out, indent, "cursor:");
    append_line(out, indent + 1,
                std::string{"shape: "} + std::string{cursor_shape_text(cursor.shape)});
    append_line(out, indent + 1, std::string{"blink: "} + std::string{bool_text(cursor.blink)});
}

void append_cursor_overrides(std::string &out, const CursorOverrides &cursor, int indent)
{
    if (!cursor.shape.has_value() && !cursor.blink.has_value()) {
        append_line(out, indent, "cursor: {}");
        return;
    }
    append_line(out, indent, "cursor:");
    if (cursor.shape.has_value()) {
        append_line(out, indent + 1,
                    std::string{"shape: "} + std::string{cursor_shape_text(cursor.shape.value())});
    }
    if (cursor.blink.has_value()) {
        append_line(out, indent + 1,
                    std::string{"blink: "} + std::string{bool_text(cursor.blink.value())});
    }
}

void append_general(std::string &out, const GeneralConfig &general)
{
    append_line(out, 0, "general:");
    append_line(out, 1, "shell: " + quote(general.shell));
    append_line(out, 1, "arguments: " + flow_string_list(general.arguments));
    append_line(out, 1, "working_directory: " + quote(general.working_directory));
    append_line(out, 1, std::string{"login_shell: "} + std::string{bool_text(general.login_shell)});
    append_line(out, 1, "term: " + quote(general.term));
    append_line(out, 1, "colorterm: " + quote(general.colorterm));
    append_line(out, 1, "scrollback: " + std::to_string(general.scrollback));
    append_line(out, 1, std::string{"scrollbar: "} + std::string{bool_text(general.scrollbar)});
    append_line(out, 1, "wheel_lines: " + std::to_string(general.wheel_lines));
    append_line(out, 1,
                std::string{"alternate_scroll: "} +
                    std::string{bool_text(general.alternate_scroll)});
    append_line(out, 1,
                std::string{"backspace_sends_delete: "} +
                    std::string{bool_text(general.backspace_sends_delete)});
    append_line(out, 1, std::string{"bell: "} + std::string{bell_text(general.bell)});
    append_cursor(out, general.cursor, 1);
    append_line(out, 1, "word_chars: " + quote(general.word_chars));
    append_line(out, 1, "kill_timeout_ms: " + std::to_string(general.kill_timeout.count()));
    append_line(out, 1,
                std::string{"drain_output_on_close: "} +
                    std::string{bool_text(general.drain_output_on_close)});
    append_line(out, 1, "outgoing_limit_bytes: " + std::to_string(general.outgoing_limit_bytes));
    append_line(out, 1, "max_paste_bytes: " + std::to_string(general.max_paste_bytes));
}

void append_font(std::string &out, const FontConfig &font, int indent)
{
    append_line(out, indent, "font:");
    append_line(out, indent + 1, "family: " + quote(font.family));
    append_line(out, indent + 1, "size: " + std::to_string(font.size));
}

void append_font_overrides(std::string &out, const FontOverrides &font, int indent)
{
    if (!font.family.has_value() && !font.size.has_value()) {
        append_line(out, indent, "font: {}");
        return;
    }
    append_line(out, indent, "font:");
    if (font.family.has_value()) {
        append_line(out, indent + 1, "family: " + quote(font.family.value()));
    }
    if (font.size.has_value()) {
        append_line(out, indent + 1, "size: " + std::to_string(font.size.value()));
    }
}

void append_colours(std::string &out, const ColourConfig &colours, int indent)
{
    append_line(out, indent, "colours:");
    append_line(out, indent + 1, "foreground: " + quote(hex_colour(colours.foreground)));
    append_line(out, indent + 1, "background: " + quote(hex_colour(colours.background)));
    append_line(out, indent + 1, "cursor: " + quote(hex_colour(colours.cursor)));
    append_line(out, indent + 1, "palette:");
    for (std::size_t index = 0; index < colours.ansi.size(); ++index) {
        append_line(out, indent + 2,
                    std::string{kPaletteNames[index]} + ": " +
                        quote(hex_colour(colours.ansi[index])));
    }
}

bool has_colour_overrides(const ColourOverrides &colours)
{
    if (colours.foreground.has_value() || colours.background.has_value() ||
        colours.cursor.has_value()) {
        return true;
    }
    return std::ranges::any_of(colours.ansi,
                               [](const std::optional<Rgb> &entry) { return entry.has_value(); });
}

void append_colour_overrides(std::string &out, const ColourOverrides &colours, int indent)
{
    append_line(out, indent, "colours:");
    if (colours.foreground.has_value()) {
        append_line(out, indent + 1,
                    "foreground: " + quote(hex_colour(colours.foreground.value())));
    }
    if (colours.background.has_value()) {
        append_line(out, indent + 1,
                    "background: " + quote(hex_colour(colours.background.value())));
    }
    if (colours.cursor.has_value()) {
        append_line(out, indent + 1, "cursor: " + quote(hex_colour(colours.cursor.value())));
    }
    bool palette_written = false;
    for (std::size_t index = 0; index < colours.ansi.size(); ++index) {
        const std::optional<Rgb> &entry = colours.ansi[index];
        if (entry.has_value()) {
            if (!palette_written) {
                append_line(out, indent + 1, "palette:");
                palette_written = true;
            }
            append_line(out, indent + 2,
                        std::string{kPaletteNames[index]} + ": " +
                            quote(hex_colour(entry.value())));
        }
    }
}

bool is_empty_profile(const ProfileConfig &profile)
{
    const bool font_empty = !profile.font.family.has_value() && !profile.font.size.has_value();
    return !profile.shell.has_value() && !profile.arguments.has_value() &&
           !profile.working_directory.has_value() && !profile.login_shell.has_value() &&
           !profile.term.has_value() && !profile.colorterm.has_value() &&
           !profile.scrollback.has_value() && font_empty && !profile.word_chars.has_value() &&
           !profile.bell.has_value() && !profile.cursor.shape.has_value() &&
           !profile.cursor.blink.has_value() && !has_colour_overrides(profile.colours);
}

void append_profile(std::string &out, const std::string &name, const ProfileConfig &profile)
{
    if (is_empty_profile(profile)) {
        append_line(out, 1, quote(name) + ": {}");
        return;
    }
    append_line(out, 1, quote(name) + ":");
    if (profile.shell.has_value()) {
        append_line(out, 2, "shell: " + quote(profile.shell.value()));
    }
    if (profile.arguments.has_value()) {
        append_line(out, 2, "arguments: " + flow_string_list(profile.arguments.value()));
    }
    if (profile.working_directory.has_value()) {
        append_line(out, 2, "working_directory: " + quote(profile.working_directory.value()));
    }
    if (profile.login_shell.has_value()) {
        append_line(out, 2,
                    std::string{"login_shell: "} +
                        std::string{bool_text(profile.login_shell.value())});
    }
    if (profile.term.has_value()) {
        append_line(out, 2, "term: " + quote(profile.term.value()));
    }
    if (profile.colorterm.has_value()) {
        append_line(out, 2, "colorterm: " + quote(profile.colorterm.value()));
    }
    if (profile.scrollback.has_value()) {
        append_line(out, 2, "scrollback: " + std::to_string(profile.scrollback.value()));
    }
    if (profile.font.family.has_value() || profile.font.size.has_value()) {
        append_font_overrides(out, profile.font, 2);
    }
    if (profile.word_chars.has_value()) {
        append_line(out, 2, "word_chars: " + quote(profile.word_chars.value()));
    }
    if (profile.bell.has_value()) {
        append_line(out, 2, std::string{"bell: "} + std::string{bell_text(profile.bell.value())});
    }
    if (profile.cursor.shape.has_value() || profile.cursor.blink.has_value()) {
        append_cursor_overrides(out, profile.cursor, 2);
    }
    if (has_colour_overrides(profile.colours)) {
        append_colour_overrides(out, profile.colours, 2);
    }
}

void append_profiles(std::string &out, const std::map<std::string, ProfileConfig> &profiles)
{
    append_line(out, 0, "profiles:");
    for (const auto &[name, profile] : profiles) {
        append_profile(out, name, profile);
    }
}

void append_shortcuts(std::string &out, const ShortcutConfig &shortcuts)
{
    append_line(out, 0, "shortcuts:");
    append_line(out, 1, "new_tab: " + quote(shortcuts.new_tab));
    append_line(out, 1, "close_tab: " + quote(shortcuts.close_tab));
    append_line(out, 1, "rename_tab: " + quote(shortcuts.rename_tab));
    append_line(out, 1, "close_pane: " + quote(shortcuts.close_pane));
    append_line(out, 1, "split_horizontal: " + quote(shortcuts.split_horizontal));
    append_line(out, 1, "split_vertical: " + quote(shortcuts.split_vertical));
    append_line(out, 1, "next_tab: " + quote(shortcuts.next_tab));
    append_line(out, 1, "previous_tab: " + quote(shortcuts.previous_tab));
    append_line(out, 1, "focus_left: " + quote(shortcuts.focus_left));
    append_line(out, 1, "focus_right: " + quote(shortcuts.focus_right));
    append_line(out, 1, "focus_up: " + quote(shortcuts.focus_up));
    append_line(out, 1, "focus_down: " + quote(shortcuts.focus_down));
    append_line(out, 1, "resize_left: " + quote(shortcuts.resize_left));
    append_line(out, 1, "resize_right: " + quote(shortcuts.resize_right));
    append_line(out, 1, "resize_up: " + quote(shortcuts.resize_up));
    append_line(out, 1, "resize_down: " + quote(shortcuts.resize_down));
    append_line(out, 1, "cancel_paste: " + quote(shortcuts.cancel_paste));
}

std::string float_text(float value)
{
    std::array<char, 32> buffer{};
    const std::to_chars_result result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        return "0.5";
    }
    return {buffer.data(), result.ptr};
}

std::string_view orientation_text(Orientation orientation)
{
    return orientation == Orientation::vertical ? "vertical" : "horizontal";
}

void append_layout_node(std::string &out, const LayoutNodeConfig &node, int indent)
{
    if (node.pane.has_value()) {
        const LayoutPaneConfig &pane = node.pane.value();
        append_line(out, indent, "- pane:");
        append_line(out, indent + 2, "id: " + quote(pane.id));
        append_line(out, indent + 2, "profile: " + quote(pane.profile));
        append_line(out, indent + 2, "working_directory: " + quote(pane.working_directory));
        return;
    }
    append_line(out, indent, "- split:");
    append_line(out, indent + 2,
                std::string{"orientation: "} + std::string{orientation_text(node.orientation)});
    append_line(out, indent + 2, "ratio: " + float_text(node.ratio));
    std::string children = "children: [";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (index != 0) {
            children += ", ";
        }
        children += std::to_string(node.children[index]);
    }
    children += "]";
    append_line(out, indent + 2, children);
}

void append_layout_tab(std::string &out, const LayoutTabConfig &tab, int indent)
{
    append_line(out, indent, "- root: " + std::to_string(tab.root));
    append_line(out, indent + 1, "focused: " + quote(tab.focused));
    append_line(out, indent + 1, "nodes:");
    for (const LayoutNodeConfig &node : tab.nodes) {
        append_layout_node(out, node, indent + 2);
    }
}

void append_layout(std::string &out, const LayoutConfig &layout, int indent)
{
    append_line(out, indent, "active_tab: " + std::to_string(layout.active_tab));
    append_line(out, indent, "tabs:");
    for (const LayoutTabConfig &tab : layout.tabs) {
        append_layout_tab(out, tab, indent + 1);
    }
}

void append_layouts(std::string &out, const std::map<std::string, LayoutConfig> &layouts)
{
    append_line(out, 0, "layouts:");
    for (const auto &[name, layout] : layouts) {
        append_line(out, 1, quote(name) + ":");
        append_layout(out, layout, 2);
    }
}

} // namespace

std::string serialize_config(const ApplicationConfig &config)
{
    std::string out;
    append_line(out, 0, "version: " + std::to_string(config.version));

    out.push_back('\n');
    append_general(out, config.general);

    out.push_back('\n');
    append_font(out, config.font, 0);

    out.push_back('\n');
    append_colours(out, config.colours, 0);

    out.push_back('\n');
    append_line(out, 0, "default_profile: " + quote(config.default_profile));

    out.push_back('\n');
    append_profiles(out, config.profiles);

    out.push_back('\n');
    append_shortcuts(out, config.shortcuts);

    if (!config.layouts.empty()) {
        out.push_back('\n');
        append_layouts(out, config.layouts);
    }

    return out;
}

std::optional<std::string> save_config(const std::filesystem::path &path,
                                       const ApplicationConfig &config)
{
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return "cannot create directory: " + path.parent_path().string();
        }
    }

    const std::string text = serialize_config(config);
    if (!is_valid_utf8(text)) {
        return "configuration contains invalid UTF-8";
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return "cannot open for writing: " + path.string();
    }
    stream << text;
    stream.flush();
    if (!stream) {
        return "cannot write: " + path.string();
    }
    return std::nullopt;
}

} // namespace consola
