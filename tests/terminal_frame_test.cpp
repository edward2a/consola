// SPDX-License-Identifier: MIT

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include <app/session.hpp>
#include <app/terminal_manager.hpp>
#include <ui/terminal_frame.hpp>
#include <ui/terminal_view.hpp>
#include <ui/terminal_widget.hpp>

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
using consola::TerminalFrame;
using consola::TerminalManager;

bool wait_for(const std::function<bool()> &predicate, int timeout_ms, TerminalFrame &frame)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        frame.view().pump();
        Fl::check();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    frame.view().pump();
    Fl::check();
    return predicate();
}

} // namespace

int main()
{
    Fl_Window window(500, 300, "terminal_frame_test");

    TerminalManager manager(nullptr, nullptr, std::chrono::milliseconds{200});
    manager.start();

    TerminalFrame frame(0, 0, 500, 300, manager);
    window.resizable(frame);
    window.end();
    window.show();

    // The tab bar occupies the top 24px; the view sits below it and is laid out
    // in its own coordinate space, so pane widgets are positioned relative to
    // the view (not the window).
    CHECK(frame.tab_bar().y() == 0);
    CHECK(frame.view().y() == 24);
    CHECK(frame.view().h() == 300 - 24);

    SessionOptions options;
    options.executable = "/bin/sh";
    options.arguments = {"-c", "sleep 5"};
    const auto root = frame.view().start(options);
    CHECK(root.has_value());

    const bool running = wait_for(
        [&] {
            auto *instance = frame.view().manager().find(*root);
            return instance != nullptr && instance->state() == consola::TerminalState::running;
        },
        5000, frame);
    CHECK(running);

    consola::TerminalWidget *widget = frame.view().widget(*root);
    CHECK(widget != nullptr);
    // FLTK widget coordinates are window-relative, so the pane starts at the
    // view's window position (below the 24px tab bar), not at window y=0.
    CHECK(widget->y() == frame.view().y());
    CHECK(widget->x() == frame.view().x());
    CHECK(widget->h() == frame.view().h());
    CHECK(widget->y() == 24);

    // Resizing the window keeps the bar fixed and grows the view below it.
    window.size(640, 400);
    Fl::check();
    frame.view().pump();
    CHECK(frame.view().y() == 24);
    CHECK(frame.view().h() == 400 - 24);
    CHECK(frame.view().w() == 640);

    manager.shutdown();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d terminal frame check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
