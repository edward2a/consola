// SPDX-License-Identifier: MIT

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include <app/session.hpp>
#include <app/terminal_manager.hpp>
#include <layout/layout_geometry.hpp>
#include <ui/terminal_view.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::FocusDirection;
using consola::Orientation;
using consola::SessionOptions;
using consola::TerminalId;
using consola::TerminalManager;
using consola::TerminalView;

template <typename T> const T &required(const std::optional<T> &value, int line)
{
    static const T fallback{};
    if (!value) {
        std::fprintf(stderr, "FAIL line %d: expected a value\n", line);
        ++g_failures;
        return fallback;
    }
    return *value;
}

#define REQUIRED(value) required((value), __LINE__)

bool wait_for(const std::function<bool()> &predicate, int timeout_ms, TerminalView &view)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        view.pump();
        Fl::check();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    view.pump();
    Fl::check();
    return predicate();
}

// Window-title behaviour (DIP 2.12, 6.1): OSC 0/2 wins, OSC 7 is the fallback,
// an inactive tab cannot retitle, and closing the active tab retitles from the
// remaining tab.
void test_window_titles(Fl_Window &window, TerminalManager &manager, TerminalView &view,
                        TerminalId root, const SessionOptions &options)
{
    const consola::TabId titled_tab = manager.tabs().active_id();
    const consola::TabId other_tab = manager.create_tab();
    const auto other_root =
        manager.create_root_terminal(other_tab, consola::TerminalOptions{.session = options});
    CHECK(other_root.has_value());
    view.pump();
    const std::string before(window.label());
    const char *osc = "\x1b]0;hello-title\x07";
    manager.find(root)->emulator().feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte *>(osc), std::strlen(osc)));
    view.pump();
    CHECK(std::string(window.label()) == before);
    CHECK(view.select_tab(titled_tab));
    CHECK(std::string(window.label()) == "hello-title");

    // Give the other tab a distinct title, select it, then close the active tab;
    // the window must retitle from the remaining tab through pump().
    const char *other_osc = "\x1b]0;other-title\x07";
    manager.find(REQUIRED(other_root))
        ->emulator()
        .feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>(other_osc),
                                         std::strlen(other_osc)));
    CHECK(view.select_tab(other_tab));
    CHECK(std::string(window.label()) == "other-title");
    CHECK(manager.close_tab(other_tab));
    const bool other_closed = wait_for([&] { return manager.tabs().count() == 1; }, 8000, view);
    CHECK(other_closed);
    view.pump();
    CHECK(std::string(window.label()) == "hello-title");

    // OSC 7 is the fallback when no OSC 0/2 title is set; an explicit title
    // takes precedence, and clearing it restores the cwd.
    const auto feed_root = [&](const char *bytes) {
        manager.find(root)->emulator().feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte *>(bytes), std::strlen(bytes)));
        view.pump();
    };
    feed_root("\x1b]0;\x07");
    feed_root("\x1b]7;file:///tmp/osc7-dir\x07");
    CHECK(std::string(window.label()) == "/tmp/osc7-dir");
    feed_root("\x1b]0;explicit\x07");
    CHECK(std::string(window.label()) == "explicit");
    feed_root("\x1b]2;osc2-title\x07");
    CHECK(std::string(window.label()) == "osc2-title");
    feed_root("\x1b]0;\x07");
    CHECK(std::string(window.label()) == "/tmp/osc7-dir");
}

// Visual-bell timer lifecycle (DIP 6.2): the flash expires, a repeated BEL
// restarts it, and destroying a widget with a pending timeout is safe.
void test_bell_timer(const consola::TerminalStyle &style)
{
    consola::TerminalEmulator emulator;
    auto widget = std::make_unique<consola::TerminalWidget>(0, 0, 40, 20, emulator, style);

    widget->ring_bell();
    CHECK(widget->bell_flash_active());
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    Fl::check();
    CHECK(!widget->bell_flash_active());

    widget->ring_bell();
    CHECK(widget->bell_flash_active());
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    widget->ring_bell();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    Fl::check();
    CHECK(widget->bell_flash_active());

    // The timer is still pending here; the destructor must cancel it.
    widget.reset();
    Fl::check();
}

// Runtime profile selection (DIP 6.4): the resolver supplies the new pane's
// style/session and the assignment is recorded for capture/restore.
void test_profile_selection(TerminalView &view, TerminalManager &manager,
                            const SessionOptions &options)
{
    view.set_profile_names({"one", "two"});
    view.set_profile_resolver([options](std::string_view name) {
        consola::TerminalOptions result;
        result.session = options;
        result.style = consola::default_terminal_style();
        result.style.font_size = name == "one" ? 10 : 20;
        return result;
    });
    CHECK(view.select_profile("one"));
    CHECK(view.active_profile() == "one");
    CHECK(!view.select_profile("missing"));
    CHECK(view.active_profile() == "one");
    const auto profiled_tab = view.new_tab("two");
    CHECK(profiled_tab.has_value());
    CHECK(view.profile_for(REQUIRED(profiled_tab)) == "two");
    CHECK(view.widget(REQUIRED(profiled_tab))->style().font_size == 20);
    CHECK(manager.close_tab(manager.tabs().active_id()));
    const bool profiled_closed = wait_for([&] { return manager.tabs().count() == 1; }, 8000, view);
    CHECK(profiled_closed);
}

} // namespace

int main()
{
    Fl_Window window(500, 300, "terminal_view_test");

    TerminalManager manager(nullptr, nullptr, std::chrono::milliseconds{200});
    manager.start();

    TerminalView view(0, 0, 500, 300, manager);
    window.resizable(view);
    window.end();
    window.show();

    SessionOptions options;
    options.executable = "/bin/sh";
    options.arguments = {"-c", "sleep 5"};
    const auto root = view.start(options);
    CHECK(root.has_value());

    const bool root_running = wait_for(
        [&] { return manager.find(REQUIRED(root))->state() == consola::TerminalState::running; },
        5000, view);
    CHECK(root_running);
    CHECK(manager.focused() == root);

    // Visual bell (DIP 6.2): a BEL flashes the emitting pane.
    consola::TerminalStyle bell_style = consola::default_terminal_style();
    bell_style.bell = consola::BellMode::visual;
    view.widget(REQUIRED(root))->set_style(bell_style);
    manager.find(REQUIRED(root))
        ->emulator()
        .feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>("\a"), 1));
    CHECK(view.widget(REQUIRED(root))->bell_flash_active());
    test_bell_timer(bell_style);

    // URL detection is presentation-only (DIP 6.3).
    {
        const char *url_text = "\r\nvisit https://example.com/x now\r\n";
        manager.find(REQUIRED(root))
            ->emulator()
            .feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>(url_text),
                                             std::strlen(url_text)));
        consola::TerminalWidget *widget = view.widget(REQUIRED(root));
        widget->draw();
        const auto &spans = widget->url_spans();
        CHECK(spans.size() == 1);
        if (!spans.empty()) {
            CHECK(spans[0].text == "https://example.com/x");
        }

        // A URL starting at the first column maps to column 0.
        const char *edge_text = "\r\nhttp://start.example\r\n";
        manager.find(REQUIRED(root))
            ->emulator()
            .feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>(edge_text),
                                             std::strlen(edge_text)));
        widget->draw();
        const auto &edge_spans = widget->url_spans();
        const bool found_edge = std::ranges::any_of(edge_spans, [](const consola::UrlSpan &span) {
            return span.text == "http://start.example" && span.start_column == 0;
        });
        CHECK(found_edge);
    }

    // Forced undersizing keeps the PTY grid positive (DIP 3.3, 2.11).
    window.size(1, 1);
    Fl::check();
    view.pump();
    CHECK(manager.find(REQUIRED(root))->emulator().columns() >= 1);
    CHECK(manager.find(REQUIRED(root))->emulator().rows() >= 1);
    window.size(500, 300);
    Fl::check();
    view.pump();
    CHECK(manager.find(REQUIRED(root))->emulator().columns() > 1);

    const auto second = view.split_focused(Orientation::vertical);
    CHECK(second.has_value());
    CHECK(manager.tab().terminal_count() == 2);
    const bool both_running = wait_for(
        [&] {
            return manager.find(REQUIRED(root))->state() == consola::TerminalState::running &&
                   manager.find(REQUIRED(second))->state() == consola::TerminalState::running;
        },
        5000, view);
    CHECK(both_running);
    CHECK(manager.focused() == second);

    // Broadcast targets (DIP 6.1): the source alone when off; every
    // input-enabled pane in the active tab (source included once) when on.
    view.set_broadcast(false);
    const auto single = view.broadcast_targets(REQUIRED(root));
    CHECK(single.size() == 1);
    CHECK(single.front() == REQUIRED(root));
    view.set_broadcast(true);
    const auto targets = view.broadcast_targets(REQUIRED(root));
    CHECK(targets.size() == 2);
    CHECK(std::ranges::find(targets, REQUIRED(root)) != targets.end());
    CHECK(std::ranges::find(targets, REQUIRED(second)) != targets.end());
    CHECK(view.execute(
        consola::CommandRequest{consola::Command::ToggleBroadcast, FocusDirection::left}));
    CHECK(!view.broadcast());

    // The split placed the new pane to the right; focus left returns to root.
    CHECK(view.focus_direction(FocusDirection::left));
    CHECK(manager.focused() == root);
    CHECK(view.focus_direction(FocusDirection::right));
    CHECK(manager.focused() == second);

    // Pane zoom/maximize (DIP 6.1): only the focused pane is visible at the full
    // content rectangle; the tree is unchanged.
    const consola::SplitNode *split_before =
        manager.tab().layout().split(manager.tab().layout().root());
    CHECK(split_before != nullptr);
    const consola::Orientation orientation_before =
        split_before != nullptr ? split_before->orientation : Orientation::horizontal;
    const float ratio_before = split_before != nullptr ? split_before->ratio : 0.5F;
    const auto before_zoom = manager.tab().layout().terminals();
    view.toggle_zoom();
    CHECK(view.zoomed());
    CHECK(view.widget(REQUIRED(root))->visible() == 0);
    CHECK(view.widget(REQUIRED(second))->visible() != 0);
    CHECK(view.widget(REQUIRED(second))->x() == view.x());
    CHECK(view.widget(REQUIRED(second))->y() == view.y());
    CHECK(view.widget(REQUIRED(second))->w() == view.w());
    CHECK(view.widget(REQUIRED(second))->h() == view.h());
    CHECK(manager.tab().layout().terminals() == before_zoom);
    const consola::SplitNode *split_during =
        manager.tab().layout().split(manager.tab().layout().root());
    CHECK(split_during != nullptr);
    if (split_during != nullptr) {
        CHECK(split_during->orientation == orientation_before);
        CHECK(split_during->ratio == ratio_before);
    }
    // A focus change clears zoom and shows both panes again.
    view.focus(REQUIRED(root));
    CHECK(!view.zoomed());
    CHECK(manager.focused() == root);
    CHECK(view.widget(REQUIRED(root))->visible() != 0);
    CHECK(view.widget(REQUIRED(second))->visible() != 0);
    view.focus(REQUIRED(second));

    // Pane movement (DIP 6.1): no neighbor at an edge, then swap with the left.
    CHECK(!view.move_focused(FocusDirection::right));
    CHECK(view.move_focused(FocusDirection::left));
    const auto moved = manager.tab().layout().terminals();
    CHECK(moved.size() == 2);
    CHECK(moved[0] == REQUIRED(second));
    CHECK(moved[1] == REQUIRED(root));
    CHECK(manager.focused() == second);
    CHECK(view.move_focused(FocusDirection::right)); // swap back
    const auto restored = manager.tab().layout().terminals();
    CHECK(restored[0] == REQUIRED(root));
    CHECK(restored[1] == REQUIRED(second));
    view.focus(REQUIRED(root));
    CHECK(!view.move_focused(FocusDirection::left));
    view.focus(REQUIRED(second));

    // Command dispatch (DIP 3.6).
    CHECK(
        view.execute(consola::CommandRequest{consola::Command::ResizePane, FocusDirection::left}));
    CHECK(view.execute(consola::CommandRequest{consola::Command::FocusLeft, FocusDirection::left}));
    CHECK(manager.focused() == root);
    CHECK(
        view.execute(consola::CommandRequest{consola::Command::FocusRight, FocusDirection::right}));
    CHECK(manager.focused() == second);

    view.equalize();
    const consola::SplitNode *root_split =
        manager.tab().layout().split(manager.tab().layout().root());
    CHECK(root_split != nullptr);
    CHECK(root_split->ratio == 0.5F);

    // Zoom is cleared when the zoomed pane closes.
    view.toggle_zoom();
    CHECK(view.zoomed());
    CHECK(view.close_focused());
    const bool collapsed =
        wait_for([&] { return manager.tab().terminal_count() == 1; }, 5000, view);
    CHECK(collapsed);
    CHECK(!view.zoomed());
    CHECK(manager.tab().layout().contains(REQUIRED(root)));
    CHECK(!manager.tab().layout().contains(REQUIRED(second)));

    // A split that cannot fit the current content area is refused before
    // launching (DIP 3.3).
    window.size(10, 10);
    Fl::check();
    CHECK(!view.split_focused(Orientation::vertical).has_value());
    CHECK(manager.tab().terminal_count() == 1);

    // Switching the active tab relayouts the view (DIP 4.2): the new tab's
    // widget is shown while the previous tab's is hidden, and switching back
    // reverses it.
    window.size(500, 300);
    Fl::check();
    view.pump();
    const consola::TabId first_tab = manager.tabs().active_id();
    const consola::TabId second_tab = manager.create_tab();
    SessionOptions tab_options;
    tab_options.executable = "/bin/sh";
    tab_options.arguments = {"-c", "sleep 5"};
    const auto tab_root =
        manager.create_root_terminal(second_tab, consola::TerminalOptions{.session = tab_options});
    CHECK(tab_root.has_value());
    view.pump();
    CHECK(view.widget(REQUIRED(root)) != nullptr);
    CHECK(view.widget(REQUIRED(tab_root)) != nullptr);
    CHECK(view.widget(REQUIRED(root))->visible() == 0);
    CHECK(view.widget(REQUIRED(tab_root))->visible() != 0);

    // Tab movement (DIP 6.1): move the active (second) tab left and back.
    const auto tab_order = manager.tabs().ids();
    CHECK(tab_order.size() == 2);
    CHECK(tab_order[0] == first_tab);
    CHECK(tab_order[1] == second_tab);
    CHECK(view.move_tab(-1));
    const auto moved_tabs = manager.tabs().ids();
    CHECK(moved_tabs[0] == second_tab);
    CHECK(moved_tabs[1] == first_tab);
    CHECK(manager.tabs().active_id() == second_tab);
    CHECK(view.move_tab(1));
    CHECK(manager.tabs().ids()[0] == first_tab);
    CHECK(!view.move_tab(1)); // already last

    // Zoom is cleared when the active tab changes (the zoomed pane leaves the
    // active tab).
    view.toggle_zoom();
    CHECK(view.zoomed());
    CHECK(manager.select_tab(first_tab));
    view.pump();
    CHECK(!view.zoomed());
    CHECK(!view.move_tab(-1)); // already first
    CHECK(view.widget(REQUIRED(root))->visible() != 0);
    CHECK(view.widget(REQUIRED(tab_root))->visible() == 0);

    // Tab renaming (DIP 6.1): surrounding whitespace is trimmed and an empty
    // label clears the override.
    CHECK(view.rename_active_tab("  work  "));
    CHECK(manager.tab().title() == "work");
    CHECK(view.rename_active_tab("   "));
    CHECK(manager.tab().title().empty());

    CHECK(manager.close_tab(second_tab));
    const bool tab_removed =
        wait_for([&] { return manager.find(REQUIRED(tab_root)) == nullptr; }, 8000, view);
    CHECK(tab_removed);
    CHECK(manager.tabs().count() == 1);

    // View-level tab commands (DIP 4.2).
    CHECK(view.execute(consola::CommandRequest{consola::Command::NewTab, FocusDirection::left}));
    CHECK(manager.tabs().count() == 2);
    const consola::TabId new_active = manager.tabs().active_id();
    CHECK(manager.tabs().find(new_active)->terminal_count() == 1);
    CHECK(manager.tabs().active_id() != first_tab);

    CHECK(
        view.execute(consola::CommandRequest{consola::Command::PreviousTab, FocusDirection::left}));
    CHECK(manager.tabs().active_id() == first_tab);
    CHECK(view.execute(consola::CommandRequest{consola::Command::NextTab, FocusDirection::left}));
    CHECK(manager.tabs().active_id() == new_active);

    CHECK(view.execute(consola::CommandRequest{consola::Command::CloseTab, FocusDirection::left}));
    const bool tab_closed = wait_for([&] { return manager.tabs().count() == 1; }, 8000, view);
    CHECK(tab_closed);
    CHECK(manager.terminal_count() == 1);

    test_window_titles(window, manager, view, REQUIRED(root), options);

    // A background pane's title must not retitle the window; focusing that pane
    // does. The split pane becomes focused, so root is background.
    const auto split_pane = view.split_focused(Orientation::vertical);
    CHECK(split_pane.has_value());
    const std::string focused_label(window.label());
    const char *root_osc = "\x1b]0;root-title\x07";
    manager.find(REQUIRED(root))
        ->emulator()
        .feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>(root_osc),
                                         std::strlen(root_osc)));
    view.pump();
    CHECK(std::string(window.label()) == focused_label);
    view.focus(REQUIRED(root));
    CHECK(std::string(window.label()) == "root-title");

    // A pane whose process exits closes itself, and its tab closes once empty
    // while other tabs remain (DIP 4.4).
    const consola::TabId short_tab = manager.create_tab();
    SessionOptions short_options;
    short_options.executable = "/bin/sh";
    short_options.arguments = {"-c", "printf bye"};
    const auto short_root =
        manager.create_root_terminal(short_tab, consola::TerminalOptions{.session = short_options});
    CHECK(short_root.has_value());
    const bool auto_closed =
        wait_for([&] { return manager.find(REQUIRED(short_root)) == nullptr; }, 8000, view);
    CHECK(auto_closed);
    CHECK(manager.tabs().find(short_tab) == nullptr);
    CHECK(manager.tabs().count() == 1);

    test_profile_selection(view, manager, options);

    manager.shutdown();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d terminal view check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
