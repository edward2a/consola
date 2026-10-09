// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace consola {

enum class PasteStatus : std::uint8_t { completed, cancelled, aborted };

// One loop -> UI message for a session (DIP 1.7). Data messages carry one
// bounded read batch; lifecycle messages are reserved outside the data slot
// count and are never dropped by backpressure.
struct InboundMessage {
    enum class Kind : std::uint8_t { data, eof, exited, paste_finished };

    Kind kind = Kind::data;
    std::vector<std::byte> bytes;                      // kind == data
    int exit_status = 0;                               // kind == exited
    PasteStatus paste_status = PasteStatus::completed; // kind == paste_finished
};

} // namespace consola
