// SPDX-License-Identifier: MIT

#include <ui/shortcuts.hpp>

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include <FL/Fl.H>

#include <app/log.hpp>

namespace consola {
namespace {

std::string lower_copy(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char character : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return out;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<int> key_code(std::string_view text)
{
    const std::string lowered = lower_copy(trim(text));
    if (lowered.empty()) {
        return std::nullopt;
    }
    if (lowered.size() == 1) {
        return static_cast<int>(lowered[0]);
    }
    if (lowered == "left") {
        return FL_Left;
    }
    if (lowered == "right") {
        return FL_Right;
    }
    if (lowered == "up") {
        return FL_Up;
    }
    if (lowered == "down") {
        return FL_Down;
    }
    if (lowered == "pageup" || lowered == "page_up" || lowered == "pgup") {
        return FL_Page_Up;
    }
    if (lowered == "pagedown" || lowered == "page_down" || lowered == "pgdn") {
        return FL_Page_Down;
    }
    if (lowered == "home") {
        return FL_Home;
    }
    if (lowered == "end") {
        return FL_End;
    }
    if (lowered == "insert" || lowered == "ins") {
        return FL_Insert;
    }
    if (lowered == "delete" || lowered == "del") {
        return FL_Delete;
    }
    if (lowered == "backspace" || lowered == "back_space") {
        return FL_BackSpace;
    }
    if (lowered == "tab") {
        return FL_Tab;
    }
    if (lowered == "escape" || lowered == "esc") {
        return FL_Escape;
    }
    if (lowered == "enter" || lowered == "return") {
        return FL_Enter;
    }
    if (lowered == "space") {
        return ' ';
    }
    if (lowered.front() == 'f') {
        int number = 0;
        for (std::size_t index = 1; index < lowered.size(); ++index) {
            if (std::isdigit(static_cast<unsigned char>(lowered[index])) == 0) {
                return std::nullopt;
            }
            number = (number * 10) + (lowered[index] - '0');
        }
        if (number >= 1 && number <= 35) {
            return FL_F + number;
        }
    }
    return std::nullopt;
}

struct ParsedShortcut {
    int key = 0;
    int modifiers = 0;
};

std::optional<ParsedShortcut> parse_shortcut(std::string_view text)
{
    int modifiers = 0;
    std::string_view remaining = trim(text);
    while (true) {
        const std::size_t plus = remaining.find('+');
        if (plus == std::string_view::npos) {
            break;
        }
        const std::string token = lower_copy(trim(remaining.substr(0, plus)));
        if (token == "ctrl" || token == "control") {
            modifiers |= FL_CTRL;
        } else if (token == "shift") {
            modifiers |= FL_SHIFT;
        } else if (token == "alt" || token == "meta") {
            modifiers |= FL_ALT;
        } else {
            return std::nullopt;
        }
        remaining.remove_prefix(plus + 1);
    }
    const std::optional<int> key = key_code(remaining);
    if (!key.has_value()) {
        return std::nullopt;
    }
    return ParsedShortcut{.key = key.value(), .modifiers = modifiers};
}

bool keys_equal(int left, int right)
{
    if (left == right) {
        return true;
    }
    if (left > 0x7F || right > 0x7F) {
        return false;
    }
    return std::tolower(left) == std::tolower(right);
}

} // namespace

ShortcutMap::ShortcutMap() : ShortcutMap(ShortcutConfig{})
{}

ShortcutMap::ShortcutMap(const ShortcutConfig &config)
{
    const ShortcutConfig defaults;
    bind("new_tab", config.new_tab, defaults.new_tab, Command::NewTab, FocusDirection::left);
    bind("close_tab", config.close_tab, defaults.close_tab, Command::CloseTab,
         FocusDirection::left);
    bind("close_pane", config.close_pane, defaults.close_pane, Command::ClosePane,
         FocusDirection::left);
    bind("split_horizontal", config.split_horizontal, defaults.split_horizontal,
         Command::SplitHorizontal, FocusDirection::left);
    bind("split_vertical", config.split_vertical, defaults.split_vertical, Command::SplitVertical,
         FocusDirection::left);
    bind("next_tab", config.next_tab, defaults.next_tab, Command::NextTab, FocusDirection::left);
    bind("previous_tab", config.previous_tab, defaults.previous_tab, Command::PreviousTab,
         FocusDirection::left);
    bind("focus_left", config.focus_left, defaults.focus_left, Command::FocusLeft,
         FocusDirection::left);
    bind("focus_right", config.focus_right, defaults.focus_right, Command::FocusRight,
         FocusDirection::right);
    bind("focus_up", config.focus_up, defaults.focus_up, Command::FocusUp, FocusDirection::up);
    bind("focus_down", config.focus_down, defaults.focus_down, Command::FocusDown,
         FocusDirection::down);
    bind("resize_left", config.resize_left, defaults.resize_left, Command::ResizePane,
         FocusDirection::left);
    bind("resize_right", config.resize_right, defaults.resize_right, Command::ResizePane,
         FocusDirection::right);
    bind("resize_up", config.resize_up, defaults.resize_up, Command::ResizePane,
         FocusDirection::up);
    bind("resize_down", config.resize_down, defaults.resize_down, Command::ResizePane,
         FocusDirection::down);
    bind("toggle_broadcast", config.toggle_broadcast, defaults.toggle_broadcast,
         Command::ToggleBroadcast, FocusDirection::left);
    bind("toggle_zoom", config.toggle_zoom, defaults.toggle_zoom, Command::ToggleZoom,
         FocusDirection::left);
    bind("move_tab_left", config.move_tab_left, defaults.move_tab_left, Command::MoveTabLeft,
         FocusDirection::left);
    bind("move_tab_right", config.move_tab_right, defaults.move_tab_right, Command::MoveTabRight,
         FocusDirection::left);
    bind("move_pane_left", config.move_pane_left, defaults.move_pane_left, Command::MovePane,
         FocusDirection::left);
    bind("move_pane_right", config.move_pane_right, defaults.move_pane_right, Command::MovePane,
         FocusDirection::right);
    bind("move_pane_up", config.move_pane_up, defaults.move_pane_up, Command::MovePane,
         FocusDirection::up);
    bind("move_pane_down", config.move_pane_down, defaults.move_pane_down, Command::MovePane,
         FocusDirection::down);

    // Validate and carry the cancel-paste binding; the action belongs to the
    // deferred streaming-paste transport.
    std::optional<ParsedShortcut> cancel = parse_shortcut(config.cancel_paste);
    if (!cancel.has_value()) {
        log(LogLevel::Warning,
            "invalid shortcut 'cancel_paste': '" + config.cancel_paste + "' (using default)");
        cancel = parse_shortcut(defaults.cancel_paste);
    }
    if (cancel.has_value()) {
        cancel_paste_key_ = cancel->key;
        cancel_paste_modifiers_ = cancel->modifiers;
    }
}

bool ShortcutMap::is_cancel_paste(int key, int state) const
{
    if (!cancel_paste_key_.has_value()) {
        return false;
    }
    const int modifiers = state & (FL_CTRL | FL_SHIFT | FL_ALT);
    return modifiers == cancel_paste_modifiers_ && keys_equal(cancel_paste_key_.value(), key);
}

std::optional<CommandRequest> ShortcutMap::match(int key, int state) const
{
    const int modifiers = state & (FL_CTRL | FL_SHIFT | FL_ALT);
    for (const Binding &binding : bindings_) {
        if (binding.modifiers == modifiers && keys_equal(binding.key, key)) {
            return binding.request;
        }
    }
    return std::nullopt;
}

void ShortcutMap::bind(std::string_view name, const std::string &text, const std::string &fallback,
                       Command command, FocusDirection direction)
{
    std::optional<ParsedShortcut> parsed = parse_shortcut(text);
    if (!parsed.has_value()) {
        log(LogLevel::Warning,
            "invalid shortcut '" + std::string(name) + "': '" + text + "' (using default)");
        parsed = parse_shortcut(fallback);
    }
    if (!parsed.has_value()) {
        return;
    }
    bindings_.push_back(Binding{
        .key = parsed->key,
        .modifiers = parsed->modifiers,
        .request = CommandRequest{.command = command, .direction = direction},
    });
}

} // namespace consola
