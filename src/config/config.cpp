// SPDX-License-Identifier: MIT

#include <config/config.hpp>

namespace consola {

bool valid_outgoing_limit(std::size_t bytes) noexcept
{
    return bytes >= kMinOutgoingLimitBytes && bytes <= kMaxOutgoingLimitBytes;
}

bool valid_max_paste_bytes(std::size_t bytes) noexcept
{
    return bytes > 0 && bytes <= kMaxMaxPasteBytes;
}

bool valid_scrollback(int lines) noexcept
{
    return lines >= 0;
}

bool valid_font_size(int points) noexcept
{
    return points >= kMinFontSize && points <= kMaxFontSize;
}

bool valid_wheel_lines(int lines) noexcept
{
    return lines > 0;
}

bool valid_kill_timeout(std::chrono::milliseconds timeout) noexcept
{
    return timeout > std::chrono::milliseconds::zero() && timeout <= kMaxKillTimeout;
}

void apply_colour_overrides(ColourConfig &base, const ColourOverrides &overrides)
{
    for (std::size_t index = 0; index < overrides.ansi.size(); ++index) {
        const std::optional<Rgb> &entry = overrides.ansi[index];
        if (entry.has_value()) {
            base.ansi[index] = entry.value();
        }
    }
    if (overrides.foreground.has_value()) {
        base.foreground = overrides.foreground.value();
    }
    if (overrides.background.has_value()) {
        base.background = overrides.background.value();
    }
    if (overrides.cursor.has_value()) {
        base.cursor = overrides.cursor.value();
    }
}

ApplicationConfig default_application_config()
{
    ApplicationConfig config;
    config.profiles.emplace(std::string{kDefaultProfileName}, ProfileConfig{});
    return config;
}

} // namespace consola
