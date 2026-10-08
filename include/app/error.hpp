// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>

namespace consola {

enum class IoStatus { ok, would_block, eof, error };

struct IoResult {
    // count is nonzero only when status is IoStatus::ok.
    std::size_t count = 0;
    IoStatus status = IoStatus::ok;
};

enum class SessionError {
    none,
    pty_failed,
    fork_failed,
    exec_failed,
    resize_failed,
    not_running,
    cancelled,
    reply_overflow,
};

} // namespace consola
