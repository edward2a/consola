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

    // Best effort: provide the ASCII keysym for Ctrl combinations so Ctrl+C
    // keeps working on many non-Latin layouts. A layout-aware lookup is not
    // implemented (watchlist #16).
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

std::vector<KeyEvent> translate_fltk_key_events()
{
    const auto state = static_cast<unsigned>(Fl::event_state());
    const char *text = Fl::event_text();
    const int length = Fl::event_length();
    const auto key = static_cast<std::uint32_t>(Fl::event_key());

    std::vector<KeyEvent> events;
    // Ctrl combinations use the ASCII keysym path; composed/text input is
    // split into one codepoint per KeyEvent with no keysym (DIP 2.6).
    if ((state & FL_CTRL) == 0 && text != nullptr && length > 0) {
        const char *cursor = text;
        const char *end = text + length;
        while (cursor < end) {
            int decoded_length = 0;
            const auto codepoint = fl_utf8decode(cursor, end, &decoded_length);
            if (decoded_length <= 0) {
                break;
            }
            KeyEvent event = translate_key(key, state, cursor, decoded_length);
            event.keysym = kKeysymNone;
            event.unicode = codepoint;
            events.push_back(event);
            cursor += decoded_length;
        }
        if (!events.empty()) {
            return events;
        }
    }

    events.push_back(translate_key(key, state, text, length));
    return events;
}

bool dispatch_fltk_key(TerminalEmulator &emulator)
{
    bool produced = false;
    for (const KeyEvent &event : translate_fltk_key_events()) {
        if (emulator.handle_keyboard(event)) {
            produced = true;
        }
    }
    return produced;
}

} // namespace consola
