// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>

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
    CHECK(work.font.has_value());
    if (work.font.has_value()) {
        CHECK(work.font.value().size == 18);
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
