// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <config/restore.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

bool contains(const std::vector<std::string> &values, const std::string &value)
{
    return std::ranges::find(values, value) != values.end();
}

} // namespace

int main()
{
    consola::ApplicationConfig config = consola::default_application_config();
    consola::ProfileConfig work;
    work.shell = std::string("/bin/sh");
    work.font.size = 18;
    work.environment = std::map<std::string, std::string>{{"FOO", "bar"}};
    config.profiles["work"] = work;

    consola::LayoutPaneConfig pane;
    pane.id = "p1";
    pane.profile = "work";
    pane.working_directory = "/tmp";

    consola::RestorePaneOptions resolved = consola::resolve_restore_pane(config, pane);
    CHECK(resolved.profile == "work");
    CHECK(resolved.session.working_directory == "/tmp");
    CHECK(resolved.style.font_size == 18);
    CHECK(contains(resolved.session.environment, "FOO=bar"));
    CHECK(resolved.warnings.empty());
    CHECK(!resolved.retain_on_exit);

    pane.working_directory = "/definitely/not/a/consola/directory";
    resolved = consola::resolve_restore_pane(config, pane);
    CHECK(resolved.session.working_directory.empty());
    CHECK(!resolved.warnings.empty());

    pane.profile = "ghost";
    pane.working_directory.clear();
    resolved = consola::resolve_restore_pane(config, pane);
    CHECK(resolved.profile == config.default_profile);
    CHECK(!resolved.warnings.empty());

    // A missing profile shell falls back to the default shell once.
    config.profiles["work"].shell = std::string("/nonexistent/profile-shell");
    pane.profile = "work";
    resolved = consola::resolve_restore_pane(config, pane);
    CHECK(consola::executable_available(resolved.session.executable));
    CHECK(!resolved.retain_on_exit);
    CHECK(!resolved.warnings.empty());

    // If the default shell is also unavailable, the pane is retained.
    config.general.shell = "/nonexistent/default-shell";
    resolved = consola::resolve_restore_pane(config, pane);
    CHECK(resolved.retain_on_exit);
    CHECK(!consola::executable_available(resolved.session.executable));

    CHECK(consola::executable_available("/bin/sh"));
    CHECK(!consola::executable_available("/nonexistent/shell"));

    if (g_failures != 0) {
        std::fprintf(stderr, "%d restore resolution failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
