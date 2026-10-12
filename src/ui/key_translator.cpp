// SPDX-License-Identifier: MIT

#include <ui/key_translator.hpp>

#include <FL/Fl.H>
#include <FL/fl_utf8.h>

namespace consola {

KeyEvent translate_key(std::uint32_t fltk_key, unsigned fltk_state, const char *text, int text_len)
{
    KeyEvent event;
    // FLTK key codes for special keys are X11 keysyms, so pass them through.
    event.keysym = fltk_key;

    if ((fltk_state & FL_SHIFT) != 0) {
        event.modifiers |= KeyModifierShift;
    }
    if ((fltk_state & FL_CTRL) != 0) {
        event.modifiers |= KeyModifierCtrl;
    }
    if ((fltk_state & FL_ALT) != 0) {
        event.modifiers |= KeyModifierAlt;
    }
    if ((fltk_state & FL_META) != 0) {
        event.modifiers |= KeyModifierMeta;
    }

    // Provide the ASCII keysym for Ctrl combinations so Ctrl+C works on
    // non-Latin layouts (DIP 2.6).
    if ((event.modifiers & KeyModifierCtrl) != 0 && fltk_key >= 0x20 && fltk_key < 0x7f) {
        event.ascii = fltk_key;
    }

    if (text != nullptr && text_len > 0) {
        int decoded_length = 0;
        event.unicode = fl_utf8decode(text, text + text_len, &decoded_length);
    }
    return event;
}

KeyEvent translate_fltk_key()
{
    return translate_key(static_cast<std::uint32_t>(Fl::event_key()),
                         static_cast<unsigned>(Fl::event_state()), Fl::event_text(),
                         Fl::event_length());
}

} // namespace consola
