// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

#include <app/tab_manager.hpp>
#include <app/terminal_instance.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::SessionOptions;
using consola::TabId;
using consola::TabManager;
using consola::TerminalId;
using consola::TerminalInstance;

TerminalId tid(std::uint64_t value)
{
    return TerminalId{value};
}

void test_starts_with_one_tab()
{
    TabManager tabs;
    CHECK(tabs.count() == 1);
    const TabId only = tabs.active_id();
    CHECK(tabs.find(only) == &tabs.active());
    CHECK(tabs.find(TabId{9999}) == nullptr);
    CHECK(tabs.index_of(only) == 0);
}

void test_create_selects_new_tab()
{
    TabManager tabs;
    const TabId first = tabs.active_id();
    const TabId second = tabs.create();
    CHECK(tabs.count() == 2);
    CHECK(second != first);
    CHECK(tabs.active_id() == second);
    CHECK(tabs.index_of(first) == 0);
    CHECK(tabs.index_of(second) == 1);

    CHECK(tabs.select(first));
    CHECK(tabs.active_id() == first);
    CHECK(!tabs.select(TabId{4242}));
    CHECK(tabs.active_id() == first);
}

void test_next_previous_wrap()
{
    TabManager tabs;
    const TabId a = tabs.active_id();
    const TabId b = tabs.create();
    const TabId c = tabs.create();

    CHECK(tabs.select(a));
    CHECK(tabs.select_next() == b);
    CHECK(tabs.select_next() == c);
    CHECK(tabs.select_next() == a);
    CHECK(tabs.select_previous() == c);
    CHECK(tabs.select_previous() == b);

    TabManager single;
    CHECK(!single.select_next().has_value());
    CHECK(!single.select_previous().has_value());
    CHECK(single.active_id().value == single.ids().front().value);
}

void test_move_reorders_and_keeps_active()
{
    TabManager tabs;
    const TabId a = tabs.active_id();
    const TabId b = tabs.create();
    const TabId c = tabs.create();

    CHECK(tabs.select(b));
    CHECK(tabs.move(a, 2));
    const std::vector<TabId> order = tabs.ids();
    CHECK(order.size() == 3);
    CHECK(order[0] == b);
    CHECK(order[1] == c);
    CHECK(order[2] == a);
    // The active tab follows the move.
    CHECK(tabs.active_id() == b);

    // Out-of-range index clamps to the last position.
    CHECK(tabs.move(b, 100));
    CHECK(tabs.ids().back() == b);
    CHECK(tabs.active_id() == b);
    CHECK(!tabs.move(TabId{7777}, 0));
}

void test_remove_empty_and_keep_last()
{
    TabManager tabs;
    const TabId a = tabs.active_id();
    const TabId b = tabs.create();
    CHECK(tabs.count() == 2);

    CHECK(tabs.remove(b));
    CHECK(tabs.count() == 1);
    CHECK(tabs.active_id() == a);
    CHECK(tabs.find(b) == nullptr);
    CHECK(!tabs.remove(TabId{5555}));

    // Removing the last tab replaces it with a fresh empty one.
    const TabId fresh = tabs.active_id();
    CHECK(tabs.remove(fresh));
    CHECK(tabs.count() == 1);
    CHECK(tabs.find(fresh) == nullptr);
    CHECK(tabs.active().terminal_count() == 0);
}

void test_remove_rejects_non_empty()
{
    TabManager tabs;
    const TabId a = tabs.active_id();
    tabs.active().add_root(std::make_unique<TerminalInstance>(tid(1), SessionOptions{}));
    CHECK(tabs.active().terminal_count() == 1);

    CHECK(!tabs.remove(a));
    CHECK(tabs.count() == 1);
    CHECK(tabs.find(a) != nullptr);
}

} // namespace

int main()
{
    test_starts_with_one_tab();
    test_create_selects_new_tab();
    test_next_previous_wrap();
    test_move_reorders_and_keeps_active();
    test_remove_empty_and_keep_last();
    test_remove_rejects_non_empty();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d tab manager check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
