// SPDX-License-Identifier: MIT

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <fontconfig/fontconfig.h>

#include <app/emulator.hpp>
#include <app/terminal_style.hpp>
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

} // namespace

int main()
{
    // Register the bundled font exactly as the application does, so this test
    // exercises the real font-resolution path.
    FcInit();
#ifdef CONSOLA_FONT_FILE
    FcConfig *config = FcConfigGetCurrent();
    FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8 *>(CONSOLA_FONT_FILE));
    FcConfigBuildFonts(config);
#endif

    Fl_Window window(400, 60, "font_metrics_test");
    consola::TerminalEmulator emulator;
    consola::TerminalWidget widget(0, 0, 400, 60, emulator);
    window.resizable(widget);
    window.end();
    window.show();
    Fl::wait(0.2);

    const int size = consola::default_terminal_style().font_size;
    const consola::FontMetrics &metrics = widget.metrics();

    // The cell grid must be positive, monospace and not a full-width fallback.
    CHECK(metrics.cell_width > 0.0);
    CHECK(metrics.cell_height > 0);
    fl_font(FL_HELVETICA, size);
    const double m = fl_width("M");
    CHECK(std::abs(metrics.cell_width - m) < 0.5);
    CHECK(std::abs(m - fl_width("i")) < 0.5);
    CHECK(std::abs(m - fl_width("W")) < 0.5);
    CHECK(std::abs(m - fl_width(" ")) < 0.5);
    CHECK(m <= 0.85 * size);

    if (g_failures != 0) {
        std::fprintf(stderr, "%d font metric check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
