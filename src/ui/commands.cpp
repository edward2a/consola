// SPDX-License-Identifier: MIT

#include <ui/commands.hpp>

#include <FL/Fl.H>

namespace consola {

std::optional<CommandRequest> command_for_key(int key, int state)
{
    const bool ctrl = (state & FL_CTRL) != 0;
    const bool shift = (state & FL_SHIFT) != 0;
    const bool alt = (state & FL_ALT) != 0;

    // Terminator's default pane shortcuts.
    if (ctrl && shift && !alt) {
        switch (key) {
        case 'o':
        case 'O':
            return CommandRequest{.command = Command::SplitHorizontal,
                                  .direction = FocusDirection::left};
        case 'e':
        case 'E':
            return CommandRequest{.command = Command::SplitVertical,
                                  .direction = FocusDirection::left};
        case 'w':
        case 'W':
            return CommandRequest{.command = Command::ClosePane, .direction = FocusDirection::left};
        case 't':
        case 'T':
            return CommandRequest{.command = Command::NewTab, .direction = FocusDirection::left};
        case 'x':
        case 'X':
            return CommandRequest{.command = Command::CloseTab, .direction = FocusDirection::left};
        case FL_Left:
            return CommandRequest{.command = Command::ResizePane,
                                  .direction = FocusDirection::left};
        case FL_Right:
            return CommandRequest{.command = Command::ResizePane,
                                  .direction = FocusDirection::right};
        case FL_Up:
            return CommandRequest{.command = Command::ResizePane, .direction = FocusDirection::up};
        case FL_Down:
            return CommandRequest{.command = Command::ResizePane,
                                  .direction = FocusDirection::down};
        default:
            break;
        }
    }

    // Ctrl+PageUp/PageDown cycle tabs (DIP 4.2).
    if (ctrl && !shift && !alt) {
        switch (key) {
        case FL_Page_Down:
            return CommandRequest{.command = Command::NextTab, .direction = FocusDirection::left};
        case FL_Page_Up:
            return CommandRequest{.command = Command::PreviousTab,
                                  .direction = FocusDirection::left};
        default:
            break;
        }
    }

    if (alt && !ctrl && !shift) {
        switch (key) {
        case FL_Left:
            return CommandRequest{.command = Command::FocusLeft, .direction = FocusDirection::left};
        case FL_Right:
            return CommandRequest{.command = Command::FocusRight,
                                  .direction = FocusDirection::right};
        case FL_Up:
            return CommandRequest{.command = Command::FocusUp, .direction = FocusDirection::up};
        case FL_Down:
            return CommandRequest{.command = Command::FocusDown, .direction = FocusDirection::down};
        default:
            break;
        }
    }

    return std::nullopt;
}

} // namespace consola
