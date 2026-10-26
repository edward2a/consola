// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

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

bool contains(const std::vector<std::string> &list, const std::string &needle)
{
    return std::ranges::any_of(list, [&needle](const std::string &entry) {
        return entry.find(needle) != std::string::npos;
    });
}

bool error_contains(const consola::ConfigLoadResult &result, const std::string &needle)
{
    return result.error.has_value() && result.error.value().find(needle) != std::string::npos;
}

void test_empty_document()
{
    const consola::ConfigLoadResult empty = consola::parse_config("");
    CHECK(empty.ok());
    CHECK(empty.warnings.empty());
    CHECK(empty.config.version == consola::kConfigVersion);
    CHECK(empty.config.general.scrollback == consola::kDefaultScrollback);
    CHECK(empty.config.profiles.contains("default"));

    const consola::ConfigLoadResult blank = consola::parse_config("   \n# just a comment\n");
    CHECK(blank.ok());
    CHECK(blank.config.general.scrollback == consola::kDefaultScrollback);
}

void test_missing_file()
{
    const consola::ConfigLoadResult result =
        consola::load_config("/nonexistent/consola/does-not-exist.yml");
    CHECK(result.ok());
    CHECK(result.warnings.empty());
    CHECK(result.config.general.shell.empty());
}

void test_valid_config()
{
    const char *yaml = R"(version: 1
general:
  shell: /bin/zsh
  arguments: ["-l", "-c"]
  login_shell: true
  term: screen-256color
  colorterm: ""
  scrollback: 5000
  scrollbar: false
  wheel_lines: 5
  alternate_scroll: false
  backspace_sends_delete: false
  bell: visual
  cursor:
    shape: bar
    blink: false
  word_chars: "abc"
  kill_timeout_ms: 3000
  drain_output_on_close: true
  outgoing_limit_bytes: 2097152
  max_paste_bytes: 4194304
font:
  family: DejaVu Sans Mono
  size: 16
colours:
  foreground: "#112233"
  background: "#445566"
  cursor: "#778899"
  palette:
    red: "#aa0000"
    bright_blue: "#0000ff"
default_profile: default
profiles:
  work:
    shell: /bin/bash
    font:
      size: 20
    scrollback: 100
    colours:
      background: "#101010"
      palette:
        green: "#00ff00"
shortcuts:
  new_tab: "Ctrl+Shift+N"
  cancel_paste: "Ctrl+C"
)";

    const consola::ConfigLoadResult result = consola::parse_config(yaml);
    CHECK(result.ok());
    if (result.error.has_value()) {
        std::fprintf(stderr, "unexpected error: %s\n", result.error.value().c_str());
        return;
    }
    CHECK(result.warnings.empty());

    const consola::GeneralConfig &general = result.config.general;
    CHECK(general.shell == "/bin/zsh");
    CHECK(general.arguments.size() == 2);
    CHECK(general.arguments[0] == "-l");
    CHECK(general.arguments[1] == "-c");
    CHECK(general.login_shell);
    CHECK(general.term == "screen-256color");
    CHECK(general.colorterm.empty());
    CHECK(general.scrollback == 5000);
    CHECK(!general.scrollbar);
    CHECK(general.wheel_lines == 5);
    CHECK(!general.alternate_scroll);
    CHECK(!general.backspace_sends_delete);
    CHECK(general.bell == consola::BellMode::visual);
    CHECK(general.cursor.shape == consola::CursorShape::bar);
    CHECK(!general.cursor.blink);
    CHECK(general.word_chars == "abc");
    CHECK(general.kill_timeout.count() == 3000);
    CHECK(general.drain_output_on_close);
    CHECK(general.outgoing_limit_bytes == 2097152);
    CHECK(general.max_paste_bytes == 4194304);

    CHECK(result.config.font.family == "DejaVu Sans Mono");
    CHECK(result.config.font.size == 16);

    CHECK(result.config.colours.foreground.r == 0x11);
    CHECK(result.config.colours.foreground.g == 0x22);
    CHECK(result.config.colours.foreground.b == 0x33);
    CHECK(result.config.colours.background.r == 0x44);
    CHECK(result.config.colours.cursor.r == 0x77);
    CHECK(result.config.colours.ansi[1].r == 0xaa);
    CHECK(result.config.colours.ansi[12].b == 0xff);

    CHECK(result.config.profiles.contains("work"));
    const consola::ProfileConfig &work = result.config.profiles.at("work");
    CHECK(work.shell.has_value());
    if (work.shell.has_value()) {
        CHECK(work.shell.value() == "/bin/bash");
    }
    CHECK(work.font.size.has_value());
    if (work.font.size.has_value()) {
        CHECK(work.font.size.value() == 20);
    }
    CHECK(!work.font.family.has_value());
    CHECK(work.scrollback.has_value());
    if (work.scrollback.has_value()) {
        CHECK(work.scrollback.value() == 100);
    }
    const std::optional<consola::Rgb> &background = work.colours.background;
    CHECK(background.has_value());
    if (background.has_value()) {
        CHECK(background.value().r == 0x10);
    }
    const std::optional<consola::Rgb> &green = work.colours.ansi[2];
    CHECK(green.has_value());
    if (green.has_value()) {
        CHECK(green.value().g == 0xff);
    }
    CHECK(!work.colours.ansi[1].has_value());

    CHECK(result.config.shortcuts.new_tab == "Ctrl+Shift+N");
    CHECK(result.config.shortcuts.cancel_paste == "Ctrl+C");
    CHECK(result.config.shortcuts.close_tab == "Ctrl+Shift+X");
}

void test_malformed_yaml()
{
    const consola::ConfigLoadResult result = consola::parse_config("version: 1\nkey: [1, 2\n");
    CHECK(!result.ok());
    CHECK(result.error.has_value());
    // No partial data: the defaults are returned.
    CHECK(result.config.general.scrollback == consola::kDefaultScrollback);
    CHECK(result.config.font.size == consola::kDefaultFontSize);
}

void test_conversion_error()
{
    const consola::ConfigLoadResult result =
        consola::parse_config("version: 1\ngeneral:\n  scrollback: not-a-number\n");
    CHECK(!result.ok());
    CHECK(error_contains(result, "general.scrollback"));
    CHECK(result.config.general.scrollback == consola::kDefaultScrollback);
}

void test_unknown_keys()
{
    const consola::ConfigLoadResult result = consola::parse_config(R"(version: 1
mystery: 2
general:
  frobnicate: 3
  cursor:
    mystery_cursor: 1
font:
  mystery_font: 4
colours:
  mystery_colour_key: 5
  palette:
    chartreuse: "#00ff00"
shortcuts:
  mystery_shortcut: "Ctrl+Q"
profiles:
  p:
    gadget: 6
    colours:
      palette:
        chartreuse2: "#00ff00"
)");
    CHECK(result.ok());
    CHECK(contains(result.warnings, "mystery"));
    CHECK(contains(result.warnings, "frobnicate"));
    CHECK(contains(result.warnings, "mystery_cursor"));
    CHECK(contains(result.warnings, "mystery_font"));
    CHECK(contains(result.warnings, "mystery_colour_key"));
    CHECK(contains(result.warnings, "chartreuse"));
    CHECK(contains(result.warnings, "chartreuse2"));
    CHECK(contains(result.warnings, "mystery_shortcut"));
    CHECK(contains(result.warnings, "gadget"));
}

void test_scalar_types_and_overflow()
{
    // Booleans are restricted to true/false, and integers reject non-numeric
    // or out-of-range values (std::from_chars detects overflow).
    CHECK(consola::parse_config("version: 1\ngeneral:\n  login_shell: false\n").ok());
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  login_shell: yes\n").ok());
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  scrollback: 1.5\n").ok());
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  max_paste_bytes: 18446744073709551617\n")
               .ok());
    CHECK(
        !consola::parse_config("version: 1\ngeneral:\n  scrollback: 99999999999999999999\n").ok());
    CHECK(!consola::parse_config(
               "version: 1\ngeneral:\n  outgoing_limit_bytes: 99999999999999999999\n")
               .ok());
    CHECK(!consola::parse_config("version: 1\nfont:\n  size: -5\n").ok());
}

void test_invalid_values()
{
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  scrollback: -1\n").ok());
    CHECK(!consola::parse_config("version: 1\nfont:\n  size: 0\n").ok());
    CHECK(!consola::parse_config("version: 1\nfont:\n  size: 99999\n").ok());
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  wheel_lines: 0\n").ok());
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  bell: loud\n").ok());
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  cursor:\n    shape: dot\n").ok());
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  outgoing_limit_bytes: 1\n").ok());
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  max_paste_bytes: 0\n").ok());
    CHECK(!consola::parse_config("version: 1\ngeneral:\n  kill_timeout_ms: 0\n").ok());
    CHECK(!consola::parse_config("version: 1\ncolours:\n  foreground: red\n").ok());
}

void test_version_handling()
{
    const consola::ConfigLoadResult missing = consola::parse_config("general:\n  scrollback: 1\n");
    CHECK(!missing.ok());
    CHECK(error_contains(missing, "version"));

    const consola::ConfigLoadResult future = consola::parse_config("version: 2\n");
    CHECK(!future.ok());
    CHECK(error_contains(future, "version"));
}

void test_default_profile()
{
    // Named but not defined: rejected.
    const consola::ConfigLoadResult missing =
        consola::parse_config("version: 1\ndefault_profile: absent\n");
    CHECK(!missing.ok());
    CHECK(error_contains(missing, "default_profile"));

    // The built-in default profile is created when the file omits it.
    const consola::ConfigLoadResult created =
        consola::parse_config("version: 1\ndefault_profile: default\n");
    CHECK(created.ok());
    CHECK(created.config.profiles.contains("default"));
}

void test_root_must_be_map()
{
    CHECK(consola::parse_config("version: 1\n").ok());
    CHECK(!consola::parse_config("just a scalar").ok());
    CHECK(!consola::parse_config("[]").ok());

    // An explicit empty map is still a document and must carry a version.
    const consola::ConfigLoadResult empty_map = consola::parse_config("{}");
    CHECK(!empty_map.ok());
    CHECK(error_contains(empty_map, "version"));
}

void test_string_lifetime()
{
    // parse_config copies every string out of the YAML source/tree, so the
    // returned configuration stays valid after the source is destroyed.
    const consola::ConfigLoadResult result = [] {
        std::string yaml = "version: 1\ngeneral:\n  term: my-term\n";
        yaml += "profiles:\n  p1:\n    shell: /bin/zsh\n";
        return consola::parse_config(yaml);
    }();
    CHECK(result.ok());
    CHECK(result.config.general.term == "my-term");
    CHECK(result.config.profiles.contains("p1"));
    const consola::ProfileConfig &p1 = result.config.profiles.at("p1");
    CHECK(p1.shell.has_value());
    if (p1.shell.has_value()) {
        CHECK(p1.shell.value() == "/bin/zsh");
    }
}

void test_default_config_path()
{
    const std::filesystem::path path = consola::default_config_path();
    CHECK(path.filename() == "config.yml");
    CHECK(path.parent_path().filename() == "consola");
}

void test_shipped_example()
{
    const consola::ConfigLoadResult result = consola::load_config(CONSOLA_EXAMPLE_CONFIG);
    CHECK(result.ok());
    if (result.error.has_value()) {
        std::fprintf(stderr, "example error: %s\n", result.error.value().c_str());
        return;
    }
    CHECK(result.warnings.empty());
    CHECK(result.config.font.family == "Inconsolata Condensed");
    CHECK(result.config.profiles.contains("default"));
    CHECK(result.config.general.scrollback == consola::kDefaultScrollback);
}

void test_new_shortcut_keys()
{
    const consola::ConfigLoadResult result = consola::parse_config(R"(version: 1
shortcuts:
  toggle_broadcast: "Ctrl+Shift+B"
  rename_tab: "Ctrl+Shift+R"
  toggle_zoom: "Ctrl+Shift+Z"
  move_tab_left: "Ctrl+Shift+PageUp"
  move_tab_right: "Ctrl+Shift+PageDown"
  move_pane_left: "Ctrl+Shift+Alt+Left"
  move_pane_right: "Ctrl+Shift+Alt+Right"
  move_pane_up: "Ctrl+Shift+Alt+Up"
  move_pane_down: "Ctrl+Shift+Alt+Down"
)");
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    CHECK(result.config.shortcuts.toggle_broadcast == "Ctrl+Shift+B");
    CHECK(result.config.shortcuts.rename_tab == "Ctrl+Shift+R");
    CHECK(result.config.shortcuts.toggle_zoom == "Ctrl+Shift+Z");
    CHECK(result.config.shortcuts.move_tab_left == "Ctrl+Shift+PageUp");
    CHECK(result.config.shortcuts.move_tab_right == "Ctrl+Shift+PageDown");
    CHECK(result.config.shortcuts.move_pane_left == "Ctrl+Shift+Alt+Left");
    CHECK(result.config.shortcuts.move_pane_right == "Ctrl+Shift+Alt+Right");
    CHECK(result.config.shortcuts.move_pane_up == "Ctrl+Shift+Alt+Up");
    CHECK(result.config.shortcuts.move_pane_down == "Ctrl+Shift+Alt+Down");
}

} // namespace

int main()
{
    try {
        test_empty_document();
        test_missing_file();
        test_valid_config();
        test_malformed_yaml();
        test_conversion_error();
        test_unknown_keys();
        test_scalar_types_and_overflow();
        test_invalid_values();
        test_version_handling();
        test_default_profile();
        test_root_must_be_map();
        test_string_lifetime();
        test_default_config_path();
        test_shipped_example();
        test_new_shortcut_keys();
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "unexpected exception: %s\n", exception.what());
        return EXIT_FAILURE;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("config_loader_test: ok\n");
    return EXIT_SUCCESS;
}
