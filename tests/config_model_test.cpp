// SPDX-License-Identifier: MIT

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <optional>
#include <string>

#include <app/terminal_style.hpp>
#include <config/config.hpp>
#include <config/layout_config.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

void test_application_defaults()
{
    const consola::ApplicationConfig config = consola::default_application_config();

    CHECK(config.version == consola::kConfigVersion);
    CHECK(config.version == 1);
    CHECK(config.default_profile == "default");
    CHECK(config.profiles.contains("default"));
    CHECK(config.profiles.size() == 1);

    const consola::ProfileConfig &profile = config.profiles.at("default");
    CHECK(!profile.shell.has_value());
    CHECK(!profile.arguments.has_value());
    CHECK(!profile.working_directory.has_value());
    CHECK(!profile.login_shell.has_value());
    CHECK(!profile.term.has_value());
    CHECK(!profile.colorterm.has_value());
    CHECK(!profile.scrollback.has_value());
    CHECK(!profile.font.has_value());
    CHECK(!profile.word_chars.has_value());
    CHECK(!profile.bell.has_value());
    CHECK(!profile.cursor.has_value());
    CHECK(!profile.colours.foreground.has_value());
    CHECK(!profile.colours.background.has_value());
    CHECK(!profile.colours.cursor.has_value());
    for (const std::optional<consola::Rgb> &entry : profile.colours.ansi) {
        CHECK(!entry.has_value());
    }
}

void test_general_defaults()
{
    const consola::GeneralConfig general = consola::default_application_config().general;

    CHECK(general.shell == "/bin/sh");
    CHECK(general.arguments.empty());
    CHECK(general.working_directory.empty());
    CHECK(!general.login_shell);
    CHECK(general.term == "xterm-256color");
    CHECK(general.colorterm == "truecolor");
    CHECK(general.scrollback == consola::kDefaultScrollback);
    CHECK(general.scrollback == 10000);
    CHECK(general.scrollbar);
    CHECK(general.wheel_lines == consola::kDefaultWheelLines);
    CHECK(general.wheel_lines == 3);
    CHECK(general.alternate_scroll);
    CHECK(general.backspace_sends_delete);
    CHECK(general.bell == consola::BellMode::none);
    CHECK(general.cursor.shape == consola::CursorShape::block);
    CHECK(general.cursor.blink);
    CHECK(general.word_chars == consola::kDefaultWordChars);
    CHECK(general.kill_timeout == consola::kDefaultKillTimeout);
    CHECK(general.kill_timeout == std::chrono::milliseconds{2000});
    CHECK(!general.drain_output_on_close);
    CHECK(general.outgoing_limit_bytes == consola::kDefaultOutgoingLimitBytes);
    CHECK(general.max_paste_bytes == consola::kDefaultMaxPasteBytes);
}

void test_model_matches_terminal_style()
{
    const consola::ApplicationConfig config = consola::default_application_config();
    const consola::TerminalStyle &style = consola::default_terminal_style();

    // The config defaults and the widget's style defaults must not drift.
    CHECK(config.font.family == style.font_family);
    CHECK(config.font.size == style.font_size);
    CHECK(config.general.word_chars == style.word_chars);
    CHECK(config.general.max_paste_bytes == style.max_paste_bytes);
    CHECK(config.colours.ansi == style.ansi);
    CHECK(config.colours.foreground.r == style.foreground.r);
    CHECK(config.colours.background.r == style.background.r);
    CHECK(config.general.scrollback == consola::kDefaultScrollback);
}

void test_validation()
{
    CHECK(consola::valid_outgoing_limit(consola::kMinOutgoingLimitBytes));
    CHECK(consola::valid_outgoing_limit(consola::kMinOutgoingLimitBytes + 1));
    CHECK(consola::valid_outgoing_limit(consola::kMaxOutgoingLimitBytes));
    CHECK(!consola::valid_outgoing_limit(consola::kMinOutgoingLimitBytes - 1));
    CHECK(!consola::valid_outgoing_limit(consola::kMaxOutgoingLimitBytes + 1));
    CHECK(!consola::valid_outgoing_limit(0));

    CHECK(consola::valid_max_paste_bytes(1));
    CHECK(consola::valid_max_paste_bytes(consola::kMaxMaxPasteBytes));
    CHECK(!consola::valid_max_paste_bytes(0));
    CHECK(!consola::valid_max_paste_bytes(consola::kMaxMaxPasteBytes + 1));

    CHECK(consola::valid_scrollback(0));
    CHECK(consola::valid_scrollback(10000));
    CHECK(!consola::valid_scrollback(-1));

    CHECK(consola::valid_font_size(consola::kMinFontSize));
    CHECK(consola::valid_font_size(consola::kMaxFontSize));
    CHECK(!consola::valid_font_size(0));
    CHECK(!consola::valid_font_size(consola::kMaxFontSize + 1));

    CHECK(consola::valid_wheel_lines(1));
    CHECK(!consola::valid_wheel_lines(0));
    CHECK(!consola::valid_wheel_lines(-3));

    CHECK(consola::valid_kill_timeout(std::chrono::milliseconds{2000}));
    CHECK(consola::valid_kill_timeout(consola::kMaxKillTimeout));
    CHECK(!consola::valid_kill_timeout(consola::kMaxKillTimeout + std::chrono::milliseconds{1}));
    CHECK(!consola::valid_kill_timeout(std::chrono::milliseconds{0}));
    CHECK(!consola::valid_kill_timeout(std::chrono::milliseconds{-1}));
}

void test_layout_defaults()
{
    const consola::LayoutConfig layout;
    CHECK(layout.tabs.empty());
    CHECK(layout.active_tab == 0);

    const consola::LayoutPaneConfig pane;
    CHECK(pane.id.empty());
    CHECK(pane.profile == "default");
    CHECK(pane.working_directory.empty());

    const consola::LayoutNodeConfig node;
    CHECK(node.orientation == consola::Orientation::horizontal);
    CHECK(node.ratio == 0.5F);
    CHECK(node.children.empty());
    CHECK(!node.pane.has_value());
}

void test_colour_overrides()
{
    consola::ColourConfig base;
    consola::ColourOverrides overrides;
    overrides.ansi[1] = consola::Rgb{0x10, 0x20, 0x30};
    overrides.foreground = consola::Rgb{0xaa, 0xbb, 0xcc};

    consola::apply_colour_overrides(base, overrides);

    CHECK(base.ansi[1].r == 0x10);
    CHECK(base.ansi[1].g == 0x20);
    CHECK(base.ansi[1].b == 0x30);
    // Unset entries keep the base value.
    CHECK(base.ansi[0] == consola::kDefaultAnsi[0]);
    CHECK(base.foreground.r == 0xaa);
    CHECK(base.foreground.g == 0xbb);
    CHECK(base.foreground.b == 0xcc);
    CHECK(base.background.r == 0x18);
}

} // namespace

int main()
{
    test_application_defaults();
    test_general_defaults();
    test_model_matches_terminal_style();
    test_validation();
    test_layout_defaults();
    test_colour_overrides();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("config_model_test: ok\n");
    return EXIT_SUCCESS;
}
