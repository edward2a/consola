// SPDX-License-Identifier: MIT

#include <cstdlib>

#include <FL/Fl.H>
#include <libtsm.h>
#include <ryml.hpp>

int main()
{
    tsm_screen *screen = nullptr;
    if (tsm_screen_new(&screen, nullptr, nullptr) < 0) {
        return EXIT_FAILURE;
    }
    tsm_screen_unref(screen);

    ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr("consola: link-check"));
    if (tree.empty()) {
        return EXIT_FAILURE;
    }

    if (Fl::version() <= 0.0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
