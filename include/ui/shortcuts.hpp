// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <config/config.hpp>
#include <ui/commands.hpp>

namespace consola {

// Parsed, config-driven keyboard shortcuts (DIP 3.6, 5.2). Canonical text such
// as "Ctrl+Shift+T" or "Alt+Left" is parsed once. An invalid entry falls back
// to the built-in default and logs a warning, so a mistyped shortcut never
// makes a command unreachable.
class ShortcutMap {
public:
    ShortcutMap();
    explicit ShortcutMap(const ShortcutConfig &config);

    [[nodiscard]] std::optional<CommandRequest> match(int key, int state) const;

private:
    struct Binding {
        int key = 0;
        int modifiers = 0;
        CommandRequest request;
    };

    void bind(std::string_view name, const std::string &text, const std::string &fallback,
              Command command, FocusDirection direction);

    std::vector<Binding> bindings_;
};

} // namespace consola
