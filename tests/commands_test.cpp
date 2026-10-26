// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdlib>
#include <optional>

#include <FL/Fl.H>

#include <ui/commands.hpp>

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
using consola::command_for_key;
using consola::CommandRequest;
using consola::FocusDirection;

void check(int key, int state, Command command, FocusDirection direction)
{
    const std::optional<CommandRequest> request = command_for_key(key, state);
    CHECK(request.has_value());
    if (request) {
        CHECK(request->command == command);
        CHECK(request->direction == direction);
    }
}

void test_shortcuts()
{
    const int cs = FL_CTRL | FL_SHIFT;
    const int alt = FL_ALT;

    check('o', cs, Command::SplitHorizontal, FocusDirection::left);
    check('E', cs, Command::SplitVertical, FocusDirection::left);
    check('w', cs, Command::ClosePane, FocusDirection::left);
    check('t', cs, Command::NewTab, FocusDirection::left);
    check('X', cs, Command::CloseTab, FocusDirection::left);
    check('R', cs, Command::RenameTab, FocusDirection::left);
    check('P', cs, Command::SelectProfile, FocusDirection::left);
    check('b', cs, Command::ToggleBroadcast, FocusDirection::left);
    check('z', cs, Command::ToggleZoom, FocusDirection::left);
    check(FL_Page_Up, cs, Command::MoveTabLeft, FocusDirection::left);
    check(FL_Page_Down, cs, Command::MoveTabRight, FocusDirection::left);
    check(FL_Left, cs | alt, Command::MovePane, FocusDirection::left);
    check(FL_Right, cs | alt, Command::MovePane, FocusDirection::right);
    check(FL_Up, cs | alt, Command::MovePane, FocusDirection::up);
    check(FL_Down, cs | alt, Command::MovePane, FocusDirection::down);

    check(FL_Page_Down, FL_CTRL, Command::NextTab, FocusDirection::left);
    check(FL_Page_Up, FL_CTRL, Command::PreviousTab, FocusDirection::left);

    check(FL_Left, alt, Command::FocusLeft, FocusDirection::left);
    check(FL_Right, alt, Command::FocusRight, FocusDirection::right);
    check(FL_Up, alt, Command::FocusUp, FocusDirection::up);
    check(FL_Down, alt, Command::FocusDown, FocusDirection::down);

    check(FL_Left, cs, Command::ResizePane, FocusDirection::left);
    check(FL_Right, cs, Command::ResizePane, FocusDirection::right);
    check(FL_Up, cs, Command::ResizePane, FocusDirection::up);
    check(FL_Down, cs, Command::ResizePane, FocusDirection::down);
}

void test_unmapped()
{
    CHECK(!command_for_key('a', 0).has_value());
    CHECK(!command_for_key('o', FL_CTRL).has_value());
    CHECK(!command_for_key(FL_Left, 0).has_value());
    CHECK(!command_for_key(FL_Left, FL_CTRL).has_value());
    CHECK(!command_for_key(FL_Page_Down, 0).has_value());
    CHECK(!command_for_key(FL_Page_Down, FL_CTRL | FL_ALT).has_value());
    CHECK(!command_for_key('t', FL_CTRL).has_value());

    // Near-miss modifier combinations are not shortcuts.
    CHECK(!command_for_key('o', FL_CTRL | FL_SHIFT | FL_ALT).has_value());
    CHECK(!command_for_key(FL_Left, FL_ALT | FL_SHIFT).has_value());
    CHECK(!command_for_key(FL_Left, FL_ALT | FL_CTRL).has_value());
}

} // namespace

int main()
{
    test_shortcuts();
    test_unmapped();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d command check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
