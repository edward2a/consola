// SPDX-License-Identifier: MIT

#include <app/application.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <fontconfig/fontconfig.h>

#include <app/emulator.hpp>
#include <app/log.hpp>
#include <ui/terminal_widget.hpp>

namespace consola {
namespace {

bool g_smoke_timeout_ran = false;

void smoke_test_exit([[maybe_unused]] void *data)
{
    g_smoke_timeout_ran = true;
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

    const char *banner = "consola\r\n";
    emulator.feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>(banner),
                                             std::strlen(banner)));

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
        Fl::add_timeout(0.05, smoke_test_feed, &smoke_context);
        Fl::add_timeout(0.1, smoke_test_exit, nullptr);
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
