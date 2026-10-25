// SPDX-License-Identifier: MIT

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>

#include <app/emulator.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

std::span<const std::byte> as_bytes(const char *text)
{
    return {reinterpret_cast<const std::byte *>(text), std::strlen(text)};
}

struct Collector {
    std::string text;
};

void collect(void *context, std::span<const std::byte> bytes)
{
    auto *collector = static_cast<Collector *>(context);
    collector->text.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

consola::KeyEvent up_key()
{
    consola::KeyEvent event;
    event.keysym = 0xff52U; // XKB_KEY_Up
    return event;
}

// Broadcast fans out the same translated KeyEvent; each destination's VTE
// encodes it with its own modes (DIP 6.1).
void test_cursor_key_modes()
{
    consola::TerminalEmulator normal;
    Collector normal_output;
    normal.set_output_callback(&collect, &normal_output);
    CHECK(normal.handle_keyboard(up_key()));
    CHECK(normal_output.text == "\x1b[A");

    consola::TerminalEmulator application;
    Collector application_output;
    application.set_output_callback(&collect, &application_output);
    application.feed(as_bytes("\x1b[?1h")); // DECCKM: application cursor keys
    CHECK(application.handle_keyboard(up_key()));
    CHECK(application_output.text == "\x1bOA");
}

// Paste is captured per destination so bracketed-paste framing stays
// independent (DIP 2.8, 6.1).
void test_paste_framing()
{
    consola::TerminalEmulator plain;
    const consola::PasteCapture plain_paste = plain.capture_paste("hi", 1024);
    CHECK(plain_paste.ok);
    CHECK(plain_paste.bytes == "hi");

    consola::TerminalEmulator bracketed;
    bracketed.feed(as_bytes("\x1b[?2004h"));
    const consola::PasteCapture bracketed_paste = bracketed.capture_paste("hi", 1024);
    CHECK(bracketed_paste.ok);
    CHECK(bracketed_paste.bytes == "\x1b[200~hi\x1b[201~");
}

// Automatic replies produced by feed() belong to the queried session only.
void test_feed_reply_is_local()
{
    consola::TerminalEmulator queried;
    Collector queried_output;
    queried.set_output_callback(&collect, &queried_output);
    queried.feed(as_bytes("\x1b[6n")); // cursor position request -> reply
    CHECK(!queried_output.text.empty());

    consola::TerminalEmulator other;
    Collector other_output;
    other.set_output_callback(&collect, &other_output);
    CHECK(other_output.text.empty());
}

} // namespace

int main()
{
    test_cursor_key_modes();
    test_paste_framing();
    test_feed_reply_is_local();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("broadcast_test: ok\n");
    return EXIT_SUCCESS;
}
