// SPDX-License-Identifier: MIT

#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <app/emulator.hpp>

namespace {

std::span<const std::byte> as_bytes(const char *text)
{
    return {reinterpret_cast<const std::byte *>(text), std::strlen(text)};
}

std::string g_output;

void collect_output(void *, std::span<const std::byte> bytes)
{
    g_output.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

} // namespace

int main()
{
    consola::TerminalEmulator emulator;

    if (emulator.columns() != 80 || emulator.rows() != 24) {
        return EXIT_FAILURE;
    }

    emulator.resize(100, 30);
    if (emulator.columns() != 100 || emulator.rows() != 30) {
        return EXIT_FAILURE;
    }

    emulator.resize(0, 10);
    emulator.resize(-1, 10);
    if (emulator.columns() != 100 || emulator.rows() != 30) {
        return EXIT_FAILURE;
    }

    emulator.feed(as_bytes("hello"));
    if (emulator.cursor_x_raw() != 5 || emulator.cursor_y_raw() != 0) {
        return EXIT_FAILURE;
    }

    emulator.feed(as_bytes("\r\nworld"));
    if (emulator.cursor_x_raw() != 5 || emulator.cursor_y_raw() != 1) {
        return EXIT_FAILURE;
    }

    emulator.set_output_callback(&collect_output, nullptr);
    emulator.feed(as_bytes("\x1b[c"));
    if (g_output.empty()) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
