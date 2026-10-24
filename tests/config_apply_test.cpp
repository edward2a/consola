// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdlib>
#include <string>

#include <config/apply.hpp>
#include <config/loader.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

void test_defaults()
{
    const consola::ApplicationConfig config = consola::default_application_config();
    const consola::TerminalStyle style = consola::resolve_terminal_style(config);

    CHECK(style.font_family == config.font.family);
    CHECK(style.font_size == config.font.size);
    CHECK(style.ansi == config.colours.ansi);
    CHECK(style.foreground == config.colours.foreground);
    CHECK(style.background == config.colours.background);
    CHECK(style.cursor == config.colours.cursor);
    CHECK(style.word_chars == config.general.word_chars);
    CHECK(style.max_paste_bytes == config.general.max_paste_bytes);
    CHECK(style.scrollback == config.general.scrollback);
    CHECK(style.scrollbar == config.general.scrollbar);
    CHECK(style.wheel_lines == config.general.wheel_lines);
    CHECK(style.alternate_scroll == config.general.alternate_scroll);
    CHECK(style.backspace_sends_delete == config.general.backspace_sends_delete);
    CHECK(style.cursor_shape == config.general.cursor.shape);
    CHECK(style.cursor_blink == config.general.cursor.blink);

    const consola::SessionOptions session = consola::resolve_session_options(config);
    CHECK(!session.executable.empty());
    CHECK(session.arguments == config.general.arguments);
    CHECK(session.working_directory == config.general.working_directory);
    CHECK(session.login_shell == config.general.login_shell);
    CHECK(session.term == config.general.term);
    CHECK(session.colorterm == config.general.colorterm);
    CHECK(session.drain_output_on_close == config.general.drain_output_on_close);
    CHECK(session.outgoing_limit_bytes == config.general.outgoing_limit_bytes);
}

void test_profile_overrides()
{
    unsetenv("SHELL");
    const char *yaml = R"(version: 1
general:
  shell: /bin/sh
  scrollback: 1000
  cursor:
    shape: block
    blink: true
font:
  family: Base
  size: 12
colours:
  foreground: "#111111"
  background: "#222222"
  palette:
    red: "#aa0000"
default_profile: work
profiles:
  work:
    shell: /bin/zsh
    scrollback: 50
    bell: visual
    font:
      size: 18
    cursor:
      shape: bar
      blink: false
    colours:
      background: "#000000"
      palette:
        red: "#ff0000"
  blink_only:
    cursor:
      blink: false
)";

    const consola::ConfigLoadResult loaded = consola::parse_config(yaml);
    CHECK(loaded.ok());
    if (!loaded.ok()) {
        return;
    }

    const consola::TerminalStyle style = consola::resolve_terminal_style(loaded.config);
    CHECK(style.font_family == "Base"); // not overridden by the profile
    CHECK(style.font_size == 18);
    CHECK(style.scrollback == 50);
    CHECK(style.cursor_shape == consola::CursorShape::bar);
    CHECK(!style.cursor_blink);
    CHECK(style.foreground.r == 0x11); // inherited from the colours layer
    CHECK(style.background.r == 0x00); // overridden
    CHECK(style.ansi[1].r == 0xff);    // palette override
    CHECK(style.ansi[0] == consola::kDefaultAnsi[0]);
    CHECK(style.bell == consola::BellMode::visual);

    // A profile that overrides only blink inherits the general shape.
    const consola::TerminalStyle blink_only =
        consola::resolve_terminal_style(loaded.config, "blink_only");
    CHECK(blink_only.cursor_shape == consola::CursorShape::block);
    CHECK(!blink_only.cursor_blink);

    const consola::SessionOptions session = consola::resolve_session_options(loaded.config);
    CHECK(session.executable == "/bin/zsh");

    // Explicit profile selection overrides default_profile.
    const consola::SessionOptions base = consola::resolve_session_options(loaded.config, "default");
    CHECK(base.executable == "/bin/sh");

    // An unknown profile falls back to the general layer.
    const consola::SessionOptions unknown =
        consola::resolve_session_options(loaded.config, "missing");
    CHECK(unknown.executable == "/bin/sh");
}

void test_style_interaction_settings()
{
    const char *yaml = R"(version: 1
general:
  scrollbar: false
  wheel_lines: 9
  alternate_scroll: false
  backspace_sends_delete: false
  max_paste_bytes: 1024
)";
    const consola::ConfigLoadResult loaded = consola::parse_config(yaml);
    CHECK(loaded.ok());
    if (!loaded.ok()) {
        return;
    }
    const consola::TerminalStyle style = consola::resolve_terminal_style(loaded.config);
    CHECK(!style.scrollbar);
    CHECK(style.wheel_lines == 9);
    CHECK(!style.alternate_scroll);
    CHECK(!style.backspace_sends_delete);
    CHECK(style.max_paste_bytes == 1024);
}

void test_environment_shell()
{
    const consola::ApplicationConfig config = consola::default_application_config();
    CHECK(config.general.shell.empty());

    setenv("SHELL", "/bin/consola-test-shell", 1);
    const consola::SessionOptions from_env = consola::resolve_session_options(config);
    CHECK(from_env.executable == "/bin/consola-test-shell");

    unsetenv("SHELL");
    const consola::SessionOptions fallback = consola::resolve_session_options(config);
    CHECK(fallback.executable == "/bin/sh");
}

} // namespace

int main()
{
    test_defaults();
    test_profile_overrides();
    test_style_interaction_settings();
    test_environment_shell();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("config_apply_test: ok\n");
    return EXIT_SUCCESS;
}
