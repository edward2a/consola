// SPDX-License-Identifier: MIT

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include <app/emulator.hpp>
#include <app/terminal_style.hpp>
#include <ui/fonts.hpp>
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
    // Register the bundled preferred font and the embedded fallback exactly as
    // the application does, so this test exercises the real font-resolution path
    // (including the embedded fallback when the preferred font is unusable).
    consola::register_application_fonts();

    Fl_Window window(400, 60, "font_metrics_test");
    consola::TerminalEmulator emulator;
    consola::TerminalWidget widget(0, 0, 400, 60, emulator);
    window.resizable(widget);
    window.end();
    window.show();
    Fl::wait(0.2);

    const int size = consola::default_terminal_style().font_size;
    const consola::FontMetrics &metrics = widget.metrics();

    // The cell grid must be positive and not a full-width (CJK) fallback, whose
    // ~1em advance doubles the spacing of Latin text.
    CHECK(metrics.cell_width > 0.0);
    CHECK(metrics.cell_height > 0);
    CHECK(metrics.cell_width <= 0.85 * size);
    CHECK(metrics.cell_width >= 0.3 * size);

    if (g_failures != 0) {
        std::fprintf(stderr, "%d font metric check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
