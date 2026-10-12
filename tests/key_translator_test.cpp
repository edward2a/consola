// SPDX-License-Identifier: MIT

#include <ui/key_translator.hpp>

#include <cstdint>
#include <cstdio>

#include <FL/Fl.H>

int main()
{
    // Ctrl+C carries the ASCII keysym so it works on non-Latin layouts.
    const consola::KeyEvent ctrl_c = consola::translate_key('c', FL_CTRL, "", 0);
    if (ctrl_c.keysym != 'c' || ctrl_c.ascii != 'c' ||
        (ctrl_c.modifiers & consola::KeyModifierCtrl) == 0 ||
        ctrl_c.unicode != consola::kUnicodeNone) {
        std::printf("ctrl+c translation failed\n");
        return 1;
    }

    // Special keys pass through as keysyms with no ASCII fallback.
    const consola::KeyEvent left = consola::translate_key(FL_Left, 0, "", 0);
    if (left.keysym != static_cast<std::uint32_t>(FL_Left) || left.ascii != consola::kKeysymNone) {
        std::printf("left translation failed\n");
        return 1;
    }

    // Printable text is carried as a composed codepoint.
    const consola::KeyEvent a = consola::translate_key('a', 0, "a", 1);
    if (a.unicode != 'a') {
        std::printf("text translation failed\n");
        return 1;
    }

    const consola::KeyEvent shifted = consola::translate_key('A', FL_SHIFT, "A", 1);
    if (shifted.unicode != 'A' || (shifted.modifiers & consola::KeyModifierShift) == 0) {
        std::printf("shift translation failed\n");
        return 1;
    }

    const consola::KeyEvent alt = consola::translate_key('x', FL_ALT, "x", 1);
    if ((alt.modifiers & consola::KeyModifierAlt) == 0) {
        std::printf("alt translation failed\n");
        return 1;
    }

    std::printf("key_translator_test ok\n");
    return 0;
}
