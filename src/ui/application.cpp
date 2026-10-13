// SPDX-License-Identifier: MIT

#include <app/application.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <fontconfig/fontconfig.h>

#include <app/emulator.hpp>
#include <app/log.hpp>
#include <app/message.hpp>
#include <app/session.hpp>
#include <app/session_queue.hpp>
#include <platform/io_loop.hpp>
#include <platform/startup.hpp>
#include <ui/terminal_widget.hpp>

namespace consola {
namespace {

bool g_smoke_timeout_ran = false;

void smoke_test_exit([[maybe_unused]] void *data)
{
    g_smoke_timeout_ran = true;
    Fl::hide_all_windows();
}

// Test/CI hook: close the window after CONSOLA_EXIT_AFTER_MS to exercise the
// window-close shutdown path headlessly.
void exit_after_timeout([[maybe_unused]] void *data)
{
    Fl::hide_all_windows();
}

struct SmokeFeedContext {
    TerminalEmulator *emulator;
    TerminalWidget *terminal;
};

struct TitleContext {
    Fl_Window *window;
};

void title_callback(void *data, std::string_view title)
{
    auto *context = static_cast<TitleContext *>(data);
    const std::string text(title);
    context->window->copy_label(text.c_str());
    context->window->redraw();
}

void smoke_test_feed(void *data)
{
    auto *context = static_cast<SmokeFeedContext *>(data);
    const char *more = "second line\r\n";
    context->emulator->feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte *>(more), std::strlen(more)));
    // Trigger a partial (age-driven) redraw rather than a full one.
    context->terminal->damage(FL_DAMAGE_EXPOSE);
}

// Minimal single-session wiring: one shell fed by the I/O loop, keys and pastes
// routed back through the session's outbound queue (DIP Stage 1/2 exit).
struct SessionHost {
    StartupWorker worker{nullptr, nullptr};
    std::unique_ptr<IoLoop> loop = make_linux_io_loop(nullptr, nullptr);
    SessionInboundQueue inbound;
    SessionOutboundQueue outbound{std::size_t{1} << 20};
    StartupResult result;
    TerminalEmulator *emulator = nullptr;
    TerminalWidget *widget = nullptr;
    TerminalId id{1};
    bool launched = false;
    bool added = false;
    bool exited = false;
    bool closing = false;
    bool removed = false;
    bool launch_failed = false;

    ~SessionHost()
    {
        // Last resort if shutdown() was not run: terminate the child directly.
        if (result.session != nullptr) {
            result.session->terminate();
        }
        if (loop) {
            loop->stop();
        }
        worker.stop();
    }

    static void output_callback(void *context, std::span<const std::byte> bytes)
    {
        static_cast<SessionHost *>(context)->send(bytes);
    }

    static void paste_callback(void *context, std::string_view bytes)
    {
        static_cast<SessionHost *>(context)->send(std::span<const std::byte>(
            reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()));
    }

    static void resize_callback(void *context, int columns, int rows)
    {
        auto *host = static_cast<SessionHost *>(context);
        host->emulator->resize(columns, rows);
        if (host->result.session != nullptr) {
            host->result.session->resize(columns, rows);
        }
        host->widget->redraw();
    }

    static void poll_timer(void *context)
    {
        auto *host = static_cast<SessionHost *>(context);
        if (!host->pump()) {
            Fl::repeat_timeout(0.016, &SessionHost::poll_timer, context);
        }
    }

    void send(std::span<const std::byte> bytes)
    {
        if (!added || bytes.empty()) {
            return;
        }
        if (outbound.try_push(bytes)) {
            loop->write_pending(id);
        }
    }

    void start()
    {
        loop->start();
        LaunchRequest request;
        request.id = id;
        const char *shell = std::getenv("SHELL");
        request.options.executable = (shell != nullptr && shell[0] != '\0') ? shell : "/bin/sh";
        request.options.columns = emulator->columns();
        request.options.rows = emulator->rows();
        worker.submit(std::move(request));
    }

    // Processes one poll cycle; returns true once the session is fully removed
    // and the window has been hidden.
    bool pump()
    {
        if (!launched && worker.try_pop(result)) {
            launched = true;
            const bool registered =
                result.session != nullptr &&
                loop->add(*result.session, result.master_fd, result.exit_notification_fd,
                          result.process_id, id, false, inbound, outbound);
            if (registered) {
                added = true;
            } else {
                launch_failed = true;
                exited = true;
            }
        }
        if (added) {
            InboundMessage message;
            bool dirty = false;
            while (inbound.try_pop(message)) {
                if (message.kind == InboundMessage::Kind::data) {
                    emulator->feed(message.bytes);
                    dirty = true;
                } else if (message.kind == InboundMessage::Kind::exited) {
                    exited = true;
                }
            }
            if (dirty) {
                widget->redraw();
            }
            loop->resume(id);
        }

        LoopEvent event;
        while (loop->try_pop_event(event)) {
            if (event.kind == LoopEvent::Kind::removed) {
                removed = true;
            }
        }

        if (exited && !closing) {
            closing = true;
            if (added) {
                loop->remove(id);
            } else {
                removed = true;
            }
        }
        if (closing && removed) {
            loop->shutdown();
            Fl::hide_all_windows();
            return true;
        }
        return false;
    }

    // Orderly shutdown: cancel startup, terminate the session through the loop
    // (with SIGHUP/SIGKILL escalation), wait for removal, then stop the loop.
    void shutdown()
    {
        worker.stop();
        while (worker.try_pop(result)) {
            if (result.session == nullptr) {
                launch_failed = true;
                continue;
            }
            if (added || removed) {
                continue;
            }
            if (loop->add(*result.session, result.master_fd, result.exit_notification_fd,
                          result.process_id, id, false, inbound, outbound)) {
                added = true;
            } else {
                launch_failed = true;
            }
        }

        if (added && !removed) {
            closing = true;
            loop->terminate(id);
            for (int attempt = 0; attempt < 300 && !removed; ++attempt) {
                pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        loop->shutdown();
        loop->stop();
    }
};

std::vector<std::filesystem::path> bundled_font_candidates()
{
    std::vector<std::filesystem::path> candidates;

    if (const char *override_path = std::getenv("CONSOLA_FONT_FILE")) {
        candidates.emplace_back(override_path);
    }
    candidates.emplace_back(CONSOLA_FONT_FILE);

#ifdef __linux__
    {
        std::error_code link_error;
        const std::filesystem::path exe =
            std::filesystem::read_symlink("/proc/self/exe", link_error);
        if (!link_error) {
            candidates.push_back(exe.parent_path() / ".." / "share" / "consola" / "fonts" /
                                 "Inconsolata-Condensed.ttf");
        }
    }
#endif

    return candidates;
}

void register_bundled_font()
{
    if (FcInit() == FcFalse) {
        log(LogLevel::Warning, "fontconfig initialization failed");
        return;
    }
    for (const std::filesystem::path &candidate : bundled_font_candidates()) {
        std::error_code error;
        if (!std::filesystem::exists(candidate, error)) {
            continue;
        }
        if (FcConfigAppFontAddFile(nullptr, reinterpret_cast<const FcChar8 *>(candidate.c_str())) !=
            FcFalse) {
            return;
        }
    }
    log(LogLevel::Warning, "bundled font not found or could not be registered");
}

} // namespace

int run_application(int argc, char **argv, bool smoke_test)
{
    register_bundled_font();

    Fl_Window window(640, 400, "consola");

    TerminalEmulator emulator;
    emulator.set_max_scrollback(10000);
    TitleContext title_context{.window = &window};
    emulator.set_title_callback(&title_callback, &title_context);

    TerminalWidget terminal(0, 0, 640, 400, emulator);
    window.resizable(terminal);
    window.end();
    window.show(argc, argv);

    if (window.shown() == 0) {
        log(LogLevel::Error, "window failed to show");
        return EXIT_FAILURE;
    }

    SmokeFeedContext smoke_context{.emulator = &emulator, .terminal = &terminal};
    if (smoke_test) {
        const char *banner = "consola\r\n";
        emulator.feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>(banner),
                                                 std::strlen(banner)));
        Fl::add_timeout(0.05, smoke_test_feed, &smoke_context);
        Fl::add_timeout(0.1, smoke_test_exit, nullptr);
    } else {
        const int columns = std::max(
            1, static_cast<int>(static_cast<double>(terminal.w()) / terminal.metrics().cell_width));
        const int rows = std::max(1, terminal.h() / terminal.metrics().cell_height);
        emulator.resize(columns, rows);

        auto host = std::make_unique<SessionHost>();
        host->emulator = &emulator;
        host->widget = &terminal;
        emulator.set_output_callback(&SessionHost::output_callback, host.get());
        terminal.set_paste_callback(&SessionHost::paste_callback, host.get());
        terminal.set_resize_callback(&SessionHost::resize_callback, host.get());
        host->start();
        Fl::add_timeout(0.016, &SessionHost::poll_timer, host.get());

        if (const char *exit_after = std::getenv("CONSOLA_EXIT_AFTER_MS")) {
            const int milliseconds = std::atoi(exit_after);
            if (milliseconds > 0) {
                Fl::add_timeout(static_cast<double>(milliseconds) / 1000.0, &exit_after_timeout,
                                nullptr);
            }
        }

        Fl::run();

        host->shutdown();
        const bool failed = host->launch_failed;
        host.reset();
        return failed ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    log(LogLevel::Debug, "entering event loop");
    Fl::run();
    log(LogLevel::Debug, "event loop exited");

    if (smoke_test && !g_smoke_timeout_ran) {
        log(LogLevel::Error, "smoke test did not complete");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

} // namespace consola
