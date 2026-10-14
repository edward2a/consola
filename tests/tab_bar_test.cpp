// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdlib>
#include <optional>

#include <app/terminal_manager.hpp>
#include <ui/tab_bar.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::TabId;
using consola::TerminalManager;
using consola::TerminalTabBar;

struct Recorder {
    std::optional<TabId> selected;
    std::optional<TabId> closed;
};

void on_select(void *data, TabId tab)
{
    static_cast<Recorder *>(data)->selected = tab;
}

void on_close(void *data, TabId tab)
{
    static_cast<Recorder *>(data)->closed = tab;
}

void test_hit_testing()
{
    TerminalManager manager;
    TerminalTabBar bar(0, 0, 400, 24, manager);

    CHECK(manager.tabs().count() == 1);
    const TabId a = manager.tabs().active_id();
    const TabId b = manager.create_tab();
    const TabId c = manager.create_tab();
    CHECK(manager.tabs().count() == 3);

    // Tab width is clamped to 400 / 3 = 133.
    CHECK(!bar.tab_at(-1).has_value());
    CHECK(bar.tab_at(0) == a);
    CHECK(bar.tab_at(132) == a);
    CHECK(bar.tab_at(133) == b);
    CHECK(bar.tab_at(265) == b);
    CHECK(bar.tab_at(266) == c);
    CHECK(bar.tab_at(398) == c);
    CHECK(!bar.tab_at(1000).has_value());

    // The close glyph occupies the last 18px of each tab (133 - 18 = 115).
    CHECK(!bar.close_at(0));
    CHECK(!bar.close_at(114));
    CHECK(bar.close_at(115));
    CHECK(bar.close_at(132));
    CHECK(!bar.close_at(133));
    CHECK(bar.close_at(133 + 115));
    CHECK(!bar.close_at(133 + 114));
}

void test_callbacks()
{
    TerminalManager manager;
    TerminalTabBar bar(0, 0, 400, 24, manager);
    const TabId a = manager.tabs().active_id();
    const TabId b = manager.create_tab();
    manager.create_tab();

    Recorder recorder;
    bar.set_select_callback(&on_select, &recorder);
    bar.set_close_callback(&on_close, &recorder);

    // Clicking the body of the second tab selects it.
    bar.activate(140, 5);
    CHECK(recorder.selected == b);
    CHECK(!recorder.closed.has_value());

    // Clicking its close glyph requests a close instead of a select.
    recorder.selected.reset();
    bar.activate(133 + 120, 5);
    CHECK(recorder.closed == b);
    CHECK(!recorder.selected.has_value());

    // Clicking the first tab selects it.
    recorder.closed.reset();
    bar.activate(10, 5);
    CHECK(recorder.selected == a);

    // A click outside any tab does nothing.
    recorder.selected.reset();
    bar.activate(5000, 5);
    CHECK(!recorder.selected.has_value());

    bar.sync();
}

} // namespace

int main()
{
    test_hit_testing();
    test_callbacks();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d tab bar check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
