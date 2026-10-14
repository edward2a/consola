// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdlib>
#include <string>

#include <ui/font_name.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::fltk_raw_font_name;

void test_modifiers()
{
    // FLTK reads the first character as the style modifier, followed directly by
    // the family.
    CHECK(fltk_raw_font_name("Inconsolata Condensed", false, false) == " Inconsolata Condensed");
    CHECK(fltk_raw_font_name("Inconsolata Condensed", true, false) == "BInconsolata Condensed");
    CHECK(fltk_raw_font_name("Inconsolata Condensed", false, true) == "IInconsolata Condensed");
    CHECK(fltk_raw_font_name("Inconsolata Condensed", true, true) == "PInconsolata Condensed");
}

void test_family_starting_with_style_letter()
{
    // A family that begins with 'I' must not be misread as italic: the regular
    // encoding keeps a leading space.
    CHECK(fltk_raw_font_name("Inconsolata", false, false).front() == ' ');
    CHECK(fltk_raw_font_name("Inconsolata", false, true).front() == 'I');
    CHECK(fltk_raw_font_name("Bitstream Vera Sans Mono", false, false).front() == ' ');
    CHECK(fltk_raw_font_name("Bitstream Vera Sans Mono", true, false).front() == 'B');
}

} // namespace

int main()
{
    test_modifiers();
    test_family_starting_with_style_letter();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d font name check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
