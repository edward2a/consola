// SPDX-License-Identifier: MIT

#include <ui/commands.hpp>

#include <ui/shortcuts.hpp>

namespace consola {

std::optional<CommandRequest> command_for_key(int key, int state)
{
    static const ShortcutMap defaults;
    return defaults.match(key, state);
}

} // namespace consola
