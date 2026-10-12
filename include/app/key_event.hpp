// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace consola {

// No key symbol (XKB_KEY_NoSymbol); used for composed text with no keysym.
inline constexpr std::uint32_t kKeysymNone = 0;
// No composed Unicode text; distinct from U+0000.
inline constexpr std::uint32_t kUnicodeNone = 0xFFFFFFFFU;

enum KeyModifier : std::uint8_t {
    KeyModifierShift = 1U << 0,
    KeyModifierCtrl = 1U << 1,
    KeyModifierAlt = 1U << 2,
    KeyModifierMeta = 1U << 3,
};

// Platform-neutral key input (DIP 2.6). The UI translator fills this from an
// FLTK event; the emulator encodes it with libtsm, so libtsm types stay inside
// the emulator.
struct KeyEvent {
    // X11 keysym (special keys pass through unchanged); kKeysymNone for
    // composed text.
    std::uint32_t keysym = kKeysymNone;
    // ASCII keysym for Ctrl combinations (best effort so Ctrl+C keeps working
    // on many non-Latin layouts); kKeysymNone otherwise.
    std::uint32_t ascii = kKeysymNone;
    unsigned modifiers = 0;
    // Composed codepoint from the input method; kUnicodeNone when absent.
    std::uint32_t unicode = kUnicodeNone;
};

} // namespace consola
