// SPDX-License-Identifier: MIT

#include <app/application.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <fontconfig/fontconfig.h>

#include <app/emulator.hpp>
#include <app/log.hpp>
#include <app/session.hpp>
#include <app/terminal_manager.hpp>
#include <app/terminal_style.hpp>
#include <config/apply.hpp>
#include <config/loader.hpp>
#include <config/restore.hpp>
#include <consola_dejavu_sans_mono.hpp>
#include <ui/fonts.hpp>
#include <ui/shortcuts.hpp>
#include <ui/terminal_frame.hpp>
#include <ui/terminal_view.hpp>
#include <ui/terminal_widget.hpp>

namespace consola {
namespace {

bool g_smoke_timeout_ran = false;

void smoke_test_exit([[maybe_unused]] void *data)
{
    g_smoke_timeout_ran = true;
    Fl::hide_all_windows();
}

// Test/CI hook: request shutdown after CONSOLA_EXIT_AFTER_MS to exercise the
// window-close shutdown path headlessly.
struct ViewContext {
    TerminalFrame *frame = nullptr;
    bool shutdown_requested = false;
    bool shutdown_started = false;
};

void request_shutdown(void *data)
{
    static_cast<ViewContext *>(data)->shutdown_requested = true;
}

void window_close([[maybe_unused]] Fl_Widget *widget, void *data)
{
    // Do not hide here: keep the UI loop alive until teardown completes.
    static_cast<ViewContext *>(data)->shutdown_requested = true;
}

void tab_selected(void *data, TabId tab)
{
    static_cast<ViewContext *>(data)->frame->view().select_tab(tab);
}

void tab_close_requested(void *data, TabId tab)
{
    static_cast<ViewContext *>(data)->frame->view().close_tab(tab);
}

// Drives the managed view and its orderly shutdown from the FLTK event loop
// (DIP 4.5): the UI loop stays alive through reaping and removal.
void pump_timer(void *data)
{
    auto *context = static_cast<ViewContext *>(data);
    TerminalManager &manager = context->frame->view().manager();
    context->frame->tab_bar().sync();

    if (!context->shutdown_started) {
        const bool active = context->frame->view().pump();
        if (!active || context->shutdown_requested) {
            context->shutdown_started = true;
            manager.begin_shutdown();
        }
        Fl::repeat_timeout(0.016, &pump_timer, data);
        return;
    }

    context->frame->view().pump();
    if (manager.shutdown_complete()) {
        manager.finish_shutdown();
        Fl::hide_all_windows();
        return;
    }
    Fl::repeat_timeout(0.016, &pump_timer, data);
}

struct SmokeFeedContext {
    TerminalEmulator *emulator;
    TerminalWidget *terminal;
};

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

// Write the embedded fallback font to a temporary file (fontconfig needs a
// path) and register it. The file name encodes the content size so repeated
// runs reuse the file instead of rewriting it.
void register_embedded_font(FcConfig *config)
{
    std::error_code error;
    std::filesystem::path directory = std::filesystem::temp_directory_path(error);
    if (error) {
        log(LogLevel::Warning, "no temporary directory for the embedded fallback font");
        return;
    }
    directory /= "consola";
    std::filesystem::create_directories(directory, error);
    if (error) {
        log(LogLevel::Warning, "cannot create a directory for the embedded fallback font");
        return;
    }

    const std::filesystem::path path =
        directory / ("DejaVuSansMono-" + std::to_string(consola_dejavu_sans_mono.size()) + ".ttf");
    std::error_code size_error;
    const auto existing = std::filesystem::file_size(path, size_error);
    if (size_error || existing != consola_dejavu_sans_mono.size()) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(consola_dejavu_sans_mono.data()),
                  static_cast<std::streamsize>(consola_dejavu_sans_mono.size()));
        if (!out) {
            log(LogLevel::Warning, "cannot write the embedded fallback font");
            return;
        }
    }

    if (FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8 *>(path.c_str())) !=
        FcFalse) {
        FcConfigBuildFonts(config);
        FcConfigSetCurrent(config);
        log(LogLevel::Info, "registered embedded fallback font: DejaVu Sans Mono");
    } else {
        log(LogLevel::Warning, "could not register the embedded fallback font");
    }
}

int run_smoke_test(int argc, char **argv, Fl_Window &window, const ApplicationConfig &config)
{
    const TerminalStyle style = resolve_terminal_style(config);
    TerminalEmulator emulator(style);

    TerminalWidget terminal(0, 0, 640, 400, emulator, style);
    window.resizable(terminal);
    window.end();
    window.show(argc, argv);
    if (window.shown() == 0) {
        log(LogLevel::Error, "window failed to show");
        return EXIT_FAILURE;
    }

    SmokeFeedContext smoke_context{.emulator = &emulator, .terminal = &terminal};
    const char *banner = "consola\r\n";
    emulator.feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>(banner),
                                             std::strlen(banner)));
    Fl::add_timeout(0.05, smoke_test_feed, &smoke_context);
    Fl::add_timeout(0.1, smoke_test_exit, nullptr);

    log(LogLevel::Debug, "entering event loop");
    Fl::run();
    log(LogLevel::Debug, "event loop exited");

    if (!g_smoke_timeout_ran) {
        log(LogLevel::Error, "smoke test did not complete");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int run_session(int argc, char **argv, Fl_Window &window, const ApplicationConfig &config,
                const std::optional<std::string> &layout_name)
{
    const TerminalStyle style = resolve_terminal_style(config);
    const SessionOptions session = resolve_session_options(config);
    const ShortcutMap shortcuts(config.shortcuts);

    TerminalManager manager(nullptr, nullptr, config.general.kill_timeout);
    manager.start();

    TerminalFrame frame(0, 0, window.w(), window.h(), manager, style, shortcuts);
    // Runtime profile selection (DIP 6.4): resolve a named profile on demand so
    // new panes/tabs can use any loaded profile.
    frame.view().set_profile_resolver([config](std::string_view name) {
        const std::string profile(name);
        return TerminalOptions{
            .session = resolve_session_options(config, profile),
            .style = resolve_terminal_style(config, profile),
        };
    });
    std::vector<std::string> profile_names;
    profile_names.reserve(config.profiles.size());
    for (const auto &entry : config.profiles) {
        profile_names.push_back(entry.first);
    }
    frame.view().set_profile_names(std::move(profile_names));
    frame.view().select_profile(config.default_profile);
    window.resizable(frame);
    window.end();
    window.show(argc, argv);
    if (window.shown() == 0) {
        log(LogLevel::Error, "window failed to show");
        manager.shutdown();
        return EXIT_FAILURE;
    }

    if (layout_name.has_value()) {
        const auto found = config.layouts.find(*layout_name);
        if (found == config.layouts.end()) {
            log(LogLevel::Error, "layout '" + *layout_name + "' was not found");
            manager.shutdown();
            return EXIT_FAILURE;
        }
        std::vector<std::string> warnings;
        const auto restore_resolver = [config](const LayoutPaneConfig &pane) {
            return resolve_restore_pane(config, pane);
        };
        if (!frame.view().restore_layout(found->second, restore_resolver, &warnings)) {
            for (const std::string &warning : warnings) {
                log(LogLevel::Warning, warning);
            }
            log(LogLevel::Error, "layout restore failed");
            manager.shutdown();
            return EXIT_FAILURE;
        }
        for (const std::string &warning : warnings) {
            log(LogLevel::Warning, warning);
        }
    } else if (!frame.view().start(session)) {
        log(LogLevel::Error, "failed to create the initial terminal");
        manager.shutdown();
        return EXIT_FAILURE;
    }

    ViewContext context{.frame = &frame};
    frame.tab_bar().set_select_callback(&tab_selected, &context);
    frame.tab_bar().set_close_callback(&tab_close_requested, &context);
    window.callback(&window_close, &context);
    Fl::add_timeout(0.016, &pump_timer, &context);

    if (const char *exit_after = std::getenv("CONSOLA_EXIT_AFTER_MS")) {
        const int milliseconds = std::atoi(exit_after);
        if (milliseconds > 0) {
            Fl::add_timeout(static_cast<double>(milliseconds) / 1000.0, &request_shutdown,
                            &context);
        }
    }

    log(LogLevel::Debug, "entering event loop");
    Fl::run();
    log(LogLevel::Debug, "event loop exited");

    const bool failed = manager.launch_failed();
    // Safety net if the event loop ended before the timer completed teardown.
    manager.shutdown();
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

} // namespace

void register_application_fonts()
{
    if (FcInit() == FcFalse) {
        log(LogLevel::Warning, "fontconfig initialization failed");
        return;
    }
    FcConfig *config = FcConfigGetCurrent();

    // The embedded fallback is always available, so register it first.
    register_embedded_font(config);

    for (const std::filesystem::path &candidate : bundled_font_candidates()) {
        std::error_code error;
        if (!std::filesystem::exists(candidate, error)) {
            continue;
        }
        if (FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8 *>(candidate.c_str())) !=
            FcFalse) {
            // Make the newly added face visible to Xft's font matching.
            FcConfigBuildFonts(config);
            FcConfigSetCurrent(config);
            log(LogLevel::Info, "registered bundled font: " + candidate.string());
            return;
        }
    }
    log(LogLevel::Warning, "bundled font not found or could not be registered");
}

int run_application(int argc, char **argv, bool smoke_test,
                    const std::optional<std::filesystem::path> &config_path,
                    const std::optional<std::string> &layout_name)
{
    register_application_fonts();

    const std::filesystem::path path = config_path.value_or(default_config_path());
    const ConfigLoadResult loaded = load_config(path);
    for (const std::string &warning : loaded.warnings) {
        log(LogLevel::Warning, "configuration: " + warning);
    }
    if (loaded.error.has_value()) {
        log(LogLevel::Error, "configuration: " + loaded.error.value() + " (using defaults)");
    }

    Fl_Window window(640, 400, "consola");
    if (smoke_test) {
        return run_smoke_test(argc, argv, window, loaded.config);
    }
    return run_session(argc, argv, window, loaded.config, layout_name);
}

} // namespace consola
