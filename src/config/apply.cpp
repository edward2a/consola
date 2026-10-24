// SPDX-License-Identifier: MIT

#include <config/apply.hpp>

#include <utility>

namespace consola {
namespace {

const ProfileConfig *find_profile(const ApplicationConfig &config, const std::string &profile)
{
    const std::string &name = profile.empty() ? config.default_profile : profile;
    const auto entry = config.profiles.find(name);
    return entry == config.profiles.end() ? nullptr : &entry->second;
}

} // namespace

TerminalStyle resolve_terminal_style(const ApplicationConfig &config, const std::string &profile)
{
    ColourConfig colours = config.colours;
    FontConfig font = config.font;
    std::string word_chars = config.general.word_chars;
    int scrollback = config.general.scrollback;
    CursorConfig cursor = config.general.cursor;

    if (const ProfileConfig *overrides = find_profile(config, profile)) {
        apply_colour_overrides(colours, overrides->colours);
        if (overrides->font.family.has_value()) {
            font.family = overrides->font.family.value();
        }
        if (overrides->font.size.has_value()) {
            font.size = overrides->font.size.value();
        }
        if (overrides->word_chars.has_value()) {
            word_chars = overrides->word_chars.value();
        }
        if (overrides->scrollback.has_value()) {
            scrollback = overrides->scrollback.value();
        }
        if (overrides->cursor.has_value()) {
            cursor = overrides->cursor.value();
        }
    }

    TerminalStyle style = default_terminal_style();
    style.ansi = colours.ansi;
    style.foreground = colours.foreground;
    style.background = colours.background;
    style.cursor = colours.cursor;
    style.font_family = font.family;
    style.font_size = font.size;
    style.word_chars = std::move(word_chars);
    style.max_paste_bytes = config.general.max_paste_bytes;
    style.scrollback = scrollback;
    style.scrollbar = config.general.scrollbar;
    style.wheel_lines = config.general.wheel_lines;
    style.alternate_scroll = config.general.alternate_scroll;
    style.backspace_sends_delete = config.general.backspace_sends_delete;
    style.cursor_shape = cursor.shape;
    style.cursor_blink = cursor.blink;
    return style;
}

SessionOptions resolve_session_options(const ApplicationConfig &config, const std::string &profile)
{
    SessionOptions options;
    options.executable = config.general.shell;
    options.arguments = config.general.arguments;
    options.working_directory = config.general.working_directory;
    options.login_shell = config.general.login_shell;
    options.term = config.general.term;
    options.colorterm = config.general.colorterm;
    options.drain_output_on_close = config.general.drain_output_on_close;
    options.outgoing_limit_bytes = config.general.outgoing_limit_bytes;

    if (const ProfileConfig *overrides = find_profile(config, profile)) {
        if (overrides->shell.has_value()) {
            options.executable = overrides->shell.value();
        }
        if (overrides->arguments.has_value()) {
            options.arguments = overrides->arguments.value();
        }
        if (overrides->working_directory.has_value()) {
            options.working_directory = overrides->working_directory.value();
        }
        if (overrides->login_shell.has_value()) {
            options.login_shell = overrides->login_shell.value();
        }
        if (overrides->term.has_value()) {
            options.term = overrides->term.value();
        }
        if (overrides->colorterm.has_value()) {
            options.colorterm = overrides->colorterm.value();
        }
    }
    return options;
}

} // namespace consola
