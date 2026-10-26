// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdlib>
#include <optional>

#include <FL/Fl.H>

#include <ui/shortcuts.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::Command;
using consola::CommandRequest;
using consola::FocusDirection;
using consola::ShortcutConfig;
using consola::ShortcutMap;

void check_match(const ShortcutMap &map, int key, int state, Command command, FocusDirection dir)
{
    const std::optional<CommandRequest> request = map.match(key, state);
    CHECK(request.has_value());
    if (request.has_value()) {
        CHECK(request->command == command);
        CHECK(request->direction == dir);
    }
}

void test_defaults()
{
    const ShortcutMap map;
    const int ctrl_shift = FL_CTRL | FL_SHIFT;

    check_match(map, 't', ctrl_shift, Command::NewTab, FocusDirection::left);
    check_match(map, 'T', ctrl_shift, Command::NewTab, FocusDirection::left);
    check_match(map, 'w', ctrl_shift, Command::ClosePane, FocusDirection::left);
    check_match(map, 'x', ctrl_shift, Command::CloseTab, FocusDirection::left);
    check_match(map, 'r', ctrl_shift, Command::RenameTab, FocusDirection::left);
    check_match(map, 'p', ctrl_shift, Command::SelectProfile, FocusDirection::left);
    check_match(map, 'o', ctrl_shift, Command::SplitHorizontal, FocusDirection::left);
    check_match(map, 'e', ctrl_shift, Command::SplitVertical, FocusDirection::left);

    check_match(map, FL_Page_Down, FL_CTRL, Command::NextTab, FocusDirection::left);
    check_match(map, FL_Page_Up, FL_CTRL, Command::PreviousTab, FocusDirection::left);

    check_match(map, FL_Left, FL_ALT, Command::FocusLeft, FocusDirection::left);
    check_match(map, FL_Right, FL_ALT, Command::FocusRight, FocusDirection::right);
    check_match(map, FL_Up, FL_ALT, Command::FocusUp, FocusDirection::up);
    check_match(map, FL_Down, FL_ALT, Command::FocusDown, FocusDirection::down);

    check_match(map, FL_Left, ctrl_shift, Command::ResizePane, FocusDirection::left);
    check_match(map, FL_Right, ctrl_shift, Command::ResizePane, FocusDirection::right);
    check_match(map, FL_Up, ctrl_shift, Command::ResizePane, FocusDirection::up);
    check_match(map, FL_Down, ctrl_shift, Command::ResizePane, FocusDirection::down);

    CHECK(!map.match('a', 0).has_value());
    CHECK(!map.match('t', FL_CTRL).has_value());
    CHECK(!map.match(FL_Left, FL_CTRL).has_value());
    CHECK(!map.match('t', ctrl_shift | FL_ALT).has_value());

    CHECK(map.is_cancel_paste(FL_Escape, 0));
    CHECK(!map.is_cancel_paste(FL_Escape, FL_CTRL));
}

void test_overrides()
{
    ShortcutConfig config;
    config.new_tab = "Ctrl+Shift+N";
    config.focus_left = "Alt+H";
    config.close_tab = "F4";
    config.split_vertical = "Ctrl+Alt+E";
    config.cancel_paste = "Ctrl+G";
    const ShortcutMap map(config);

    check_match(map, 'n', FL_CTRL | FL_SHIFT, Command::NewTab, FocusDirection::left);
    check_match(map, 'N', FL_CTRL | FL_SHIFT, Command::NewTab, FocusDirection::left);
    CHECK(!map.match('t', FL_CTRL | FL_SHIFT).has_value());

    check_match(map, 'h', FL_ALT, Command::FocusLeft, FocusDirection::left);
    check_match(map, FL_F + 4, 0, Command::CloseTab, FocusDirection::left);
    check_match(map, 'e', FL_CTRL | FL_ALT, Command::SplitVertical, FocusDirection::left);

    // Unchanged defaults still apply.
    check_match(map, 'w', FL_CTRL | FL_SHIFT, Command::ClosePane, FocusDirection::left);
    CHECK(map.is_cancel_paste('g', FL_CTRL));
    CHECK(!map.is_cancel_paste(FL_Escape, 0));
}

void test_invalid_falls_back()
{
    ShortcutConfig config;
    config.new_tab = "Bogus+Key";
    config.close_tab = "";
    const ShortcutMap map(config);

    // A mistyped shortcut uses the built-in default, so the command stays
    // reachable.
    check_match(map, 't', FL_CTRL | FL_SHIFT, Command::NewTab, FocusDirection::left);
    check_match(map, 'x', FL_CTRL | FL_SHIFT, Command::CloseTab, FocusDirection::left);
}

} // namespace

int main()
{
    test_defaults();
    test_overrides();
    test_invalid_falls_back();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("shortcuts_test: ok\n");
    return EXIT_SUCCESS;
}
