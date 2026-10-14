// SPDX-License-Identifier: MIT

#include <app/application.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/platform.H>
#include <X11/Xlib.h>
#include <fontconfig/fontconfig.h>

// Declared directly to avoid the freetype include dependency of Xft.h.
extern "C" FcPattern *XftFontMatch(Display *dpy, int screen, FcPattern *pattern, FcResult *result);
struct _XftFont;
typedef struct _XftFont XftFont;
extern "C" XftFont *XftFontOpenPattern(Display *dpy, FcPattern *pattern);
extern "C" void XftFontClose(Display *dpy, XftFont *font);

#include <app/emulator.hpp>
#include <app/log.hpp>
#include <app/session.hpp>
#include <app/terminal_manager.hpp>
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

// Diagnostic: report the family fontconfig assigns to the bundled file and what
// it matches for the requested family, to explain font-resolution failures.
void log_bundled_font_diagnostics(FcConfig *config, const std::string &filename)
{
    FcPattern *pattern = FcPatternCreate();
    FcObjectSet *objects = FcObjectSetBuild(FC_FAMILY, FC_FILE, static_cast<void *>(nullptr));
    FcFontSet *fonts = FcFontList(config, pattern, objects);
    if (fonts != nullptr) {
        for (int i = 0; i < fonts->nfont; ++i) {
            FcChar8 *file = nullptr;
            FcChar8 *family = nullptr;
            FcPatternGetString(fonts->fonts[i], FC_FILE, 0, &file);
            FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &family);
            if (file != nullptr && family != nullptr &&
                std::string(reinterpret_cast<const char *>(file)).find(filename) !=
                    std::string::npos) {
                log(LogLevel::Info, "bundled file family: '" +
                                        std::string(reinterpret_cast<const char *>(family)) + "'");
            }
        }
        FcFontSetDestroy(fonts);
    }
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);

    FcPattern *request = FcNameParse(reinterpret_cast<const FcChar8 *>("Inconsolata Condensed"));
    FcConfigSubstitute(config, request, FcMatchPattern);
    FcDefaultSubstitute(request);
    FcResult result = FcResultNoMatch;
    FcPattern *match = FcFontMatch(config, request, &result);
    if (match != nullptr) {
        FcChar8 *file = nullptr;
        FcChar8 *family = nullptr;
        FcPatternGetString(match, FC_FILE, 0, &file);
        FcPatternGetString(match, FC_FAMILY, 0, &family);
        log(LogLevel::Info, "fontconfig match 'Inconsolata Condensed' -> '" +
                                std::string(reinterpret_cast<const char *>(
                                    family != nullptr ? family : (const FcChar8 *)"?")) +
                                "' file='" +
                                std::string(reinterpret_cast<const char *>(
                                    file != nullptr ? file : (const FcChar8 *)"?")) +
                                "'");
        FcPatternDestroy(match);
    }
    FcPatternDestroy(request);

    // Reproduce the pattern FLTK's Xft path builds (family + weight/slant/size/
    // encoding) to see which attribute changes the match.
    FcPattern *xft = FcPatternCreate();
    FcPatternAddString(xft, FC_FAMILY, reinterpret_cast<const FcChar8 *>("Inconsolata Condensed"));
    FcPatternAddInteger(xft, FC_WEIGHT, FC_WEIGHT_MEDIUM);
    FcPatternAddInteger(xft, FC_SLANT, FC_SLANT_ROMAN);
    FcPatternAddDouble(xft, FC_PIXEL_SIZE, 14.0);
    FcConfigSubstitute(config, xft, FcMatchPattern);
    FcDefaultSubstitute(xft);
    FcResult xft_result = FcResultNoMatch;
    FcPattern *xft_match = FcFontMatch(config, xft, &xft_result);
    if (xft_match != nullptr) {
        FcChar8 *file = nullptr;
        FcChar8 *family = nullptr;
        FcPatternGetString(xft_match, FC_FILE, 0, &file);
        FcPatternGetString(xft_match, FC_FAMILY, 0, &family);
        log(LogLevel::Info, "fontconfig match (xft pattern) -> '" +
                                std::string(reinterpret_cast<const char *>(
                                    family != nullptr ? family : (const FcChar8 *)"?")) +
                                "' file='" +
                                std::string(reinterpret_cast<const char *>(
                                    file != nullptr ? file : (const FcChar8 *)"?")) +
                                "'");
        FcPatternDestroy(xft_match);
    }
    FcPatternDestroy(xft);

    // Reproduce Xft's own matching (the exact XftFontMatch FLTK calls) to see
    // whether Xft resolves differently from a plain fontconfig match.
    fl_open_display();
    if (fl_display != nullptr) {
        for (const char *encoding : {"(none)", "iso10646-1", "iso8859-1"}) {
            FcPattern *xp = FcPatternCreate();
            FcPatternAddString(xp, FC_FAMILY,
                               reinterpret_cast<const FcChar8 *>("Inconsolata Condensed"));
            FcPatternAddInteger(xp, FC_WEIGHT, FC_WEIGHT_MEDIUM);
            FcPatternAddInteger(xp, FC_SLANT, FC_SLANT_ROMAN);
            FcPatternAddDouble(xp, FC_PIXEL_SIZE, 14.0);
            if (std::strcmp(encoding, "(none)") != 0) {
                FcPatternAddString(xp, "encoding", reinterpret_cast<const FcChar8 *>(encoding));
            }
            FcResult xr = FcResultNoMatch;
            FcPattern *xm = XftFontMatch(fl_display, fl_screen, xp, &xr);
            FcPatternDestroy(xp);
            if (xm != nullptr) {
                FcChar8 *file = nullptr;
                FcPatternGetString(xm, FC_FILE, 0, &file);
                XftFont *opened = XftFontOpenPattern(fl_display, xm);
                log(LogLevel::Info, std::string("xft match [encoding=") + encoding + "] -> '" +
                                        std::string(reinterpret_cast<const char *>(
                                            file != nullptr ? file : (const FcChar8 *)"?")) +
                                        "' open=" + (opened != nullptr ? "ok" : "FAILED"));
                if (opened != nullptr) {
                    XftFontClose(fl_display, opened);
                } else {
                    FcPatternDestroy(xm);
                }
            } else {
                log(LogLevel::Info, std::string("xft match [encoding=") + encoding + "] -> null");
            }
        }
    }
}

void register_bundled_font()
{
    if (FcInit() == FcFalse) {
        log(LogLevel::Warning, "fontconfig initialization failed");
        return;
    }
    FcConfig *config = FcConfigGetCurrent();
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
            log_bundled_font_diagnostics(config, candidate.filename().string());
            return;
        }
    }
    log(LogLevel::Warning, "bundled font not found or could not be registered");
}

int run_smoke_test(int argc, char **argv, Fl_Window &window)
{
    TerminalEmulator emulator;
    emulator.set_max_scrollback(10000);

    TerminalWidget terminal(0, 0, 640, 400, emulator);
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

int run_session(int argc, char **argv, Fl_Window &window)
{
    TerminalManager manager;
    manager.start();

    TerminalFrame frame(0, 0, window.w(), window.h(), manager);
    window.resizable(frame);
    window.end();
    window.show(argc, argv);
    if (window.shown() == 0) {
        log(LogLevel::Error, "window failed to show");
        manager.shutdown();
        return EXIT_FAILURE;
    }

    SessionOptions options;
    const char *shell = std::getenv("SHELL");
    options.executable = (shell != nullptr && shell[0] != '\0') ? shell : "/bin/sh";
    if (!frame.view().start(options)) {
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

int run_application(int argc, char **argv, bool smoke_test)
{
    register_bundled_font();

    Fl_Window window(640, 400, "consola");
    if (smoke_test) {
        return run_smoke_test(argc, argv, window);
    }
    return run_session(argc, argv, window);
}

} // namespace consola
