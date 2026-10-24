// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <config/loader.hpp>
#include <config/writer.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

const char *kRichYaml = R"(version: 1
general:
  shell: /bin/zsh
  arguments: ["-l", "-c", "echo hi"]
  working_directory: /tmp
  login_shell: true
  term: screen-256color
  colorterm: "truecolor"
  scrollback: 2500
  scrollbar: false
  wheel_lines: 7
  alternate_scroll: false
  backspace_sends_delete: false
  bell: both
  cursor:
    shape: underline
    blink: false
  word_chars: "abc#:?,-"
  kill_timeout_ms: 1234
  drain_output_on_close: true
  outgoing_limit_bytes: 1048576
  max_paste_bytes: 2097152
font:
  family: DejaVu Sans Mono
  size: 15
colours:
  foreground: "#abcdef"
  background: "#012345"
  cursor: "#fedcba"
  palette:
    red: "#110000"
    bright_cyan: "#00ffff"
default_profile: work
profiles:
  work:
    shell: /bin/bash
    font:
      size: 18
    scrollback: 42
    cursor:
      shape: bar
    colours:
      background: "#222222"
      palette:
        green: "#00ff00"
  plain: {}
shortcuts:
  new_tab: "Ctrl+Shift+N"
  cancel_paste: "Ctrl+C"
)";

consola::ConfigLoadResult parse_or_fail(const char *yaml)
{
    consola::ConfigLoadResult result = consola::parse_config(yaml);
    if (result.error.has_value()) {
        std::fprintf(stderr, "unexpected parse error: %s\n", result.error.value().c_str());
    }
    return result;
}

void test_default_round_trip()
{
    const consola::ApplicationConfig original = consola::default_application_config();
    const std::string text = consola::serialize_config(original);
    const consola::ConfigLoadResult reparsed = parse_or_fail(text.c_str());
    CHECK(reparsed.ok());
    CHECK(reparsed.config == original);
}

void test_rich_round_trip()
{
    const consola::ConfigLoadResult first = parse_or_fail(kRichYaml);
    CHECK(first.ok());

    const std::string text = consola::serialize_config(first.config);
    const consola::ConfigLoadResult second = parse_or_fail(text.c_str());
    CHECK(second.ok());
    CHECK(second.config == first.config);
    CHECK(second.warnings.empty());

    // Spot-check that the values survived, not just that the two agree.
    CHECK(second.config.general.arguments.size() == 3);
    CHECK(second.config.general.arguments[2] == "echo hi");
    CHECK(second.config.colours.ansi[1].r == 0x11);
    const consola::ProfileConfig &work = second.config.profiles.at("work");
    CHECK(work.font.size.has_value());
    if (work.font.size.has_value()) {
        CHECK(work.font.size.value() == 18);
    }
    const std::optional<consola::Rgb> &green = work.colours.ansi[2];
    CHECK(green.has_value());
    if (green.has_value()) {
        CHECK(green.value().g == 0xff);
    }
    CHECK(second.config.profiles.at("plain") == consola::ProfileConfig{});
}

void test_stable_output()
{
    const consola::ConfigLoadResult first = parse_or_fail(kRichYaml);
    const std::string text = consola::serialize_config(first.config);
    const consola::ConfigLoadResult second = parse_or_fail(text.c_str());
    // Serializing a re-parsed configuration yields byte-identical output.
    CHECK(consola::serialize_config(second.config) == text);
}

void test_special_characters()
{
    consola::ApplicationConfig config = consola::default_application_config();
    config.general.shell = "weird \"quoted\"\\path\tand\nnewline";
    config.general.word_chars = "#comment: [list] {map} &anchor *alias";

    const std::string text = consola::serialize_config(config);
    const consola::ConfigLoadResult reparsed = parse_or_fail(text.c_str());
    CHECK(reparsed.ok());
    CHECK(reparsed.config.general.shell == config.general.shell);
    CHECK(reparsed.config.general.word_chars == config.general.word_chars);
}

bool round_trips(const consola::ApplicationConfig &config)
{
    const std::string text = consola::serialize_config(config);
    const consola::ConfigLoadResult reparsed = consola::parse_config(text);
    return reparsed.ok() && reparsed.config == config;
}

void test_edge_values()
{
    // All 16 palette slots, each distinct.
    consola::ApplicationConfig palette = consola::default_application_config();
    for (std::size_t index = 0; index < palette.colours.ansi.size(); ++index) {
        palette.colours.ansi[index] =
            consola::Rgb{static_cast<std::uint8_t>(index), static_cast<std::uint8_t>(index * 2U),
                         static_cast<std::uint8_t>(255U - index)};
    }
    CHECK(round_trips(palette));

    // Every bell mode and cursor shape/blink combination.
    for (const consola::BellMode bell : {consola::BellMode::none, consola::BellMode::audible,
                                         consola::BellMode::visual, consola::BellMode::both}) {
        for (const consola::CursorShape shape :
             {consola::CursorShape::block, consola::CursorShape::underline,
              consola::CursorShape::bar}) {
            for (const bool blink : {false, true}) {
                consola::ApplicationConfig config = consola::default_application_config();
                config.general.bell = bell;
                config.general.cursor = consola::CursorConfig{.shape = shape, .blink = blink};
                CHECK(round_trips(config));
            }
        }
    }

    // Control characters, CR and non-ASCII UTF-8.
    consola::ApplicationConfig control = consola::default_application_config();
    control.general.shell = std::string("a\rb\x01c\x1f"
                                        "d");
    control.font.family = "Caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    CHECK(round_trips(control));

    // A profile containing only colour overrides (not "empty").
    consola::ApplicationConfig colour_only = consola::default_application_config();
    consola::ProfileConfig colour_profile;
    colour_profile.colours.foreground = consola::Rgb{0x01, 0x02, 0x03};
    colour_profile.colours.ansi[15] = consola::Rgb{0xfe, 0xfd, 0xfc};
    colour_only.profiles.emplace("colour_only", colour_profile);
    CHECK(round_trips(colour_only));

    // An explicit empty arguments override and empty strings survive.
    consola::ApplicationConfig empties = consola::default_application_config();
    consola::ProfileConfig empty_args;
    empty_args.arguments = std::vector<std::string>{};
    empty_args.shell = std::string{};
    empties.profiles.emplace("empties", empty_args);
    CHECK(round_trips(empties));

    // default_profile naming a non-default profile.
    consola::ApplicationConfig named = consola::default_application_config();
    named.profiles.emplace("named", consola::ProfileConfig{});
    named.default_profile = "named";
    CHECK(round_trips(named));

    // A profile with every optional field set.
    consola::ApplicationConfig full = consola::default_application_config();
    consola::ProfileConfig everything;
    everything.shell = "/bin/fish";
    everything.arguments = std::vector<std::string>{"-l"};
    everything.working_directory = "/srv";
    everything.login_shell = true;
    everything.term = "vt100";
    everything.colorterm = std::string{};
    everything.scrollback = 0;
    everything.font = consola::FontOverrides{.family = "Mono", .size = 9};
    everything.word_chars = "x";
    everything.bell = consola::BellMode::audible;
    everything.cursor =
        consola::CursorConfig{.shape = consola::CursorShape::underline, .blink = false};
    everything.colours.background = consola::Rgb{0, 0, 1};
    full.profiles.emplace("everything", everything);
    CHECK(round_trips(full));
}

void test_invalid_utf8_is_rejected()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "consola_writer_utf8_test";
    const std::filesystem::path path = directory / "config.yml";
    std::filesystem::remove_all(directory);

    consola::ApplicationConfig config = consola::default_application_config();
    config.general.shell = std::string("\xff\xfe");
    const std::optional<std::string> error = consola::save_config(path, config);
    CHECK(error.has_value());
    CHECK(!std::filesystem::exists(path));

    std::filesystem::remove_all(directory);
}

void test_save_and_load()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "consola_writer_test";
    const std::filesystem::path path = directory / "nested" / "config.yml";
    std::filesystem::remove_all(directory);

    const consola::ConfigLoadResult first = parse_or_fail(kRichYaml);
    const std::optional<std::string> error = consola::save_config(path, first.config);
    CHECK(!error.has_value());
    if (error.has_value()) {
        std::fprintf(stderr, "save error: %s\n", error->c_str());
    }
    CHECK(std::filesystem::exists(path));

    const consola::ConfigLoadResult loaded = consola::load_config(path);
    CHECK(loaded.ok());
    CHECK(loaded.config == first.config);

    std::filesystem::remove_all(directory);
}

} // namespace

int main()
{
    try {
        test_default_round_trip();
        test_rich_round_trip();
        test_stable_output();
        test_special_characters();
        test_edge_values();
        test_invalid_utf8_is_rejected();
        test_save_and_load();
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "unexpected exception: %s\n", exception.what());
        return EXIT_FAILURE;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("config_writer_test: ok\n");
    return EXIT_SUCCESS;
}
