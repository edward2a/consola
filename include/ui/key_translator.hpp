// SPDX-License-Identifier: MIT

#pragma once
#include <cstdint>

#include <app/emulator.hpp>
#include <app/key_event.hpp>

namespace consola {

// Translate an FLTK key event into a platform-neutral KeyEvent (DIP 2.6).
// Contains no encoding logic; the emulator encodes with libtsm.
KeyEvent translate_fltk_key();

// Pure translation used by translate_fltk_key() and tests: fltk_key is
// Fl::event_key(), fltk_state is Fl::event_state(), text/text_len describe
// Fl::event_text()/Fl::event_length().
KeyEvent translate_key(std::uint32_t fltk_key, unsigned fltk_state, const char *text, int text_len);

// Encode the current FLTK key event through the emulator, splitting composed
// multi-codepoint text into one KeyEvent per codepoint with no keysym
// (DIP 2.6). Returns true if any output was produced.
bool dispatch_fltk_key(TerminalEmulator &emulator);

} // namespace consola
