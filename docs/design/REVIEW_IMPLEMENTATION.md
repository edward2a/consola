# Design Review — Implementation Watchlist

Post-review register of items to re-check during or after implementation.
Produced after the fifth review round (commit `306acfa`), with read-only
spot-checks against the pinned libtsm 4.8.0 source in `tmp/libtsm`.

None of these blocks Stage 0. Each is a check to make at the stage named in
**Review at**, and where possible after the relevant code exists. This file
does not authorize implementation; that still requires explicit user approval.

## Key

- **Files:** `DIP` = [`detailed-implementation-plan.md`](detailed-implementation-plan.md),
  `DEP` = [`../dependencies.md`](../dependencies.md). References use section
  headings, so they stay valid when lines move.
- **Criticality:** the same P2 / P3 / Minor scale as the earlier reviews.
- **State:**
  - **Pending** — new, not yet discussed.
  - **Reviewed** — already in the docs with an explicit "verify" or "defer"
    note; only the check remains.
  - **Settled** — checked and closed. Nothing is in this state yet.

## Watchlist

| # | Finding | Crit. | Summary | Reference | Review at | State |
|---|---|---|---|---|---|---|
| 1 | CI smoke test needs a display | P3 | The test "executable starts / opens a FLTK window" needs an X server in CI. 0.7 names no virtual display (e.g. Xvfb) and no no-window smoke mode. The sanitizer build will probably report X11/fontconfig leaks and need suppressions. | DIP §0.7 CI; DIP Stage 0 › Tests | Stage 0 | Settled |
| 2 | Stage 0 test depends on Stage 1 code | Minor | The libtsm log-level mapping test is listed in Stage 0, but the code it tests is specified in 1.5. Either build the mapping as a plain function in the 0.4 logger, or move the test to Stage 1. | DIP Stage 0 › Tests; §1.5 | Stage 0 | Settled |
| 3 | Build prerequisites and FLTK attribution | Minor | The README (0.8) must list: X11/Xft/fontconfig dev packages for building FLTK; `libutil` for `openpty()` on glibc < 2.34; FLTK's required "based in part on the work of the FLTK project" sentence. | DIP §0.8; DEP › FLTK pin and build, Always dynamic | Stage 0 | Settled |
| 4 | System-dependency mode checks | P3 | When `CONSOLA_USE_SYSTEM_DEPS` is on, the plan doesn't say how configure detects an FLTK built with Wayland/Cairo, or a rapidyaml built without exception callbacks. A system libtsm is a shared library and won't have the fork's features. | DIP §0.2, §5.3; DEP › FLTK, rapidyaml | Stage 0 / 5 | Settled |
| 5 | Concurrency verification | P2 | The design has three threads, lock-free single-producer/single-consumer queues and an atomic wake flag, but CI runs only ASan/UBSan, not ThreadSanitizer. Which thread may call `TerminalSession::running()` / `terminate()` is not stated, and neither is whether `log()` is thread-safe (the UI, I/O loop and startup worker all call it). | DIP §0.4, §0.7, §1.1, §1.7 | Stage 1 | Pending |
| 6 | Parent's copy of the PTY slave | P3 | 1.3 says the parent keeps the master FD but never says it closes its own copy of the slave after `fork()`. If the slave stays open, the Linux end-of-output signal (`EIO`/`POLLHUP`) never arrives. Test 24 would catch it. | DIP §1.3 › Required behavior | Stage 1 | Settled |
| 7 | Checking a process group is still ours | P2 | Before a delayed `kill(-pgid)`, 1.6 requires confirming the group still belongs to the terminal's session, but names no way to do it. Linux has no handle for a process group, and the group leader may already be gone. Pick a method and check it against test 18. | DIP §1.6 › terminate() | Stage 1 | Settled |
| 8 | Bracketed-paste injection | P2 | `tsm_vte_paste()` wraps the pasted text without filtering it. Pasted text containing `ESC[201~` ends the bracketing early, and the rest runs as if typed (source-checked). Decide whether to neutralise that marker before encoding. | DIP §1.7 › Streaming paste; §2.8 | Stage 2 | Pending |
| 9 | Paste framing and mode visibility | P3 | Extends the existing 2.8 "verify" note. Source check: bracketed-paste mode (2004) is a private flag with no getter and is not in `tsm_vte_get_flags()`. Framing can be inferred from capture length = source + 12 bytes, plus an optional leading ESC. | DIP §2.8 | Stage 2 | Reviewed |
| 10 | Cursor must use libtsm's clamped position | P3 | `tsm_screen_get_cursor_x()` returns the raw value, which equals the column count after writing the last column. `tsm_screen_draw()` clamps it to the last column (y likewise). If our cursor code uses the raw value, libtsm's inverted cell is left on screen (source-checked). | DIP §2.4 › Cursor | Stage 2 | Pending |
| 11 | Missing glyphs without Pango | P2 | With Xft and no Pango, FLTK probably draws with a single font and no per-glyph fallback, so CJK, symbols and emoji the font lacks would show as boxes. Verify with 2.10's CJK test. If confirmed, record it as a v1 limitation / font requirement; no new dependencies. | DIP §2.3, §2.10; DEP › FLTK pin and build | Stage 2 | Pending |
| 12 | Fractional display scaling | P3 | Cell origins are rounded in FLTK units, and FLTK then applies the screen scale. At 1.25× or 1.5×, cell backgrounds may show gaps or overlaps. | DIP §2.3, §2.4 | Stage 2 | Pending |
| 13 | Bold/italic font switching | Minor | 2.4 says set the font face once, but bold and italic text need face changes. Check the cost of switching; overly wide bold glyphs are already handled by clipping. | DIP §2.4; §2.2 › Cell | Stage 2 / 7 | Pending |
| 14 | FLTK default window and keyboard behaviour | P2 | By default, FLTK hides a window when its close button is pressed or an unhandled Escape reaches it, and `Fl::run()` returns once no window is shown. That would stop event processing before 1.6's shutdown steps finish. Unhandled Tab/arrow keys move widget focus instead of reaching the terminal. | DIP §2.6; §1.6 › Application shutdown; §4.5 | Stage 2 / 4 | Pending |
| 15 | libtsm types leak into UI code | Minor | 2.6's key translator (UI side) uses `TSM_*_MASK` and libtsm's keysym header. That contradicts 2.2's rule that libtsm types stay inside the emulator. | DIP §2.6, §2.2 | Stage 2 | Pending |
| 16 | Input methods and keyboard layouts | P3 | Existing note: verify FLTK input methods and dead keys on X11, and Ctrl+letter on non-Latin layouts. | DIP §2.6 | Stage 2 | Reviewed |
| 17 | `TERM=xterm-256color` vs what libtsm supports | P3 | The advertised terminal type promises more than libtsm does: no modifyOtherKeys or kitty keyboard protocol, no strikethrough attribute, DECSET 1007 not parsed. Check vim/nvim, tmux, less, htop and mc. | DIP §1.2; §2.6 › v1 limitation; Cross-Stage › Manual test matrix | After Stage 2 / Stage 6 | Pending |
| 18 | Cursor display details | Minor | `ColourConfig` has a cursor colour that 2.4 never uses. How the cursor looks in unfocused panes is not specified. Blink timers should run only for the focused, visible pane to keep idle CPU low. | DIP §2.4 › Cursor; §5.1; §7.5 | Stage 5 / 7 | Pending |
| 19 | Config schema is incomplete | P3 | Settings promised as configurable elsewhere are missing from 5.2: TERM/COLORTERM, login shell, word chars, cancel-paste shortcut, bell mode, cursor style. Precedence between general and profile settings (shell, font, scrollback) is undefined. | DIP §5.2; §1.2, §2.7, §2.8, §6.2, §6.4 | Stage 5 | Pending |
| 20 | Startup worker head-of-line blocking | Minor | There is one sequential startup worker. A launch stuck before exec (e.g. a hung NFS working directory) delays every queued launch until it is cancelled. Most visible when 6.5 restores many panes. | DIP §1.3 › Startup execution; §6.5 | Stage 6 / 7 | Pending |
| 21 | How URL detection reads screen text | P3 | 6.3 doesn't say how text is read. `tsm_screen_draw()` is the only reader and resets libtsm's "redraw everything" flag on first call, so an extra pass can swallow it. This only matters after the age counter overflows, which is realistic on macOS/Windows where it is 32-bit. URLs that wrap across lines are not addressed. | DIP §6.3, §2.5 | Stage 6 | Pending |
| 22 | Fixed tuning constants | P3 | The plan already marks these starting values for verification: 64 KiB / 1 ms I/O batches; 16 inbound slots, resume at 8; ~8 ms UI drain; 100 ms post-exit drain; 2 s kill timeout; 1 MiB outgoing limit with 16 KiB reserve; 16 MiB paste limit. | DIP §1.4 › I/O fairness, Post-exit output drain; §1.6; §1.7 | Stage 7 | Reviewed |
| 23 | Memory per session | P3 | Up to 1 MiB inbound plus 1 MiB outgoing per session. Whether that is allocated up front or on demand is not specified; at 50 sessions, up-front allocation could reach ~100 MiB. A broadcast paste keeps one encoded copy per receiving pane (up to 16 MiB each). | DIP §1.7 › Inbound, Outbound, Streaming paste; §6.1 | Stage 7 | Pending |
| 24 | Reply-overflow rule may kill sessions on binary output | P3 | Binary or random output contains terminal queries libtsm answers, such as `ESC Z` (DECID) and status requests. If the running program doesn't read its input, those replies fill the 1 MiB limit and 1.7 fails the session. Check with the "large cat" workload. | DIP §1.7 › Outbound | Stage 7 | Pending |
| 25 | Scrollback memory | P3 | Each libtsm cell is 32 bytes on x86-64. The default 10,000 lines at 120 columns is about 37 MiB per terminal with full scrollback, which works against the "lightweight footprint" goal. Measure and adjust the default. | DIP §2.9, §5.2, §7.4 | Stage 7 | Pending |
| 26 | Rendering performance checks | P3 | Existing "only if profiling shows it" checks: cost of per-cell clipping, per-cell `fl_draw()`, and a possible glyph atlas. | DIP §2.4, §7.5 | Stage 7 | Reviewed |
| 27 | Windows I/O model | P2 | The `IoLoop` and `TerminalSession` interfaces assume polling for readiness with non-blocking reads. Windows ConPTY uses anonymous pipes, which support neither, and `WaitForMultipleObjects` handles at most 64 objects (the plan tests 50 sessions). Review the interfaces before starting Stage 8. | DIP §1.1, §1.4, §8.2, §8.4 | Stage 8 start | Pending |
| 28 | macOS PTY polling | P3 | macOS `poll(2)` documents that devices are not supported, so the PTY may need `select()` or kqueue. macOS has no eventfd/pidfd, so the self-pipe and SIGCHLD fallbacks become the main path. | DIP §1.4, §8.3 | Stage 8 | Pending |
| 29 | Compiler and build portability | P3 | libtsm's build flags are GCC-style and its `shl-macro.h` uses GCC-specific attributes, `__builtin_expect` and `<unistd.h>`. Building with Microsoft's `cl` is unverified, so Windows needs a MinGW or clang-cl decision. The FLTK options and the "Always dynamic" list cover X11 only. | DEP › libtsm pin and build, Always dynamic; DIP §8.5 | Stage 8 | Pending |
| 30 | Distribution licensing compliance | P2 | Existing deferral: binary releases (libtsm's LGPL hash table needs corresponding source and relinking materials) and any bundle of dependency source (c4core provenance caveats) need separate approval. | DEP › Bundled licenses and distribution, Bundled notices and source redistribution; DIP §8.6 | After v1 | Reviewed |
| 31 | Integrating the libtsm fork | P3 | Existing deferral: reflow on resize and combining characters. When the fork lands, recheck the multi-codepoint `Cell` handling, the 2.10 limitations, render/selection tests and license notices. | DIP Deferred Work › libtsm fork; §2.2, §2.10 | After v1 | Reviewed |

## Totals

- 8 × P2, 17 × P3, 6 × Minor.
- 18 Pending, 7 Reviewed, 6 Settled.
- Source-checked against pinned libtsm: #8, #9, #10, #21, #24, #25.
- Documented-behaviour assumptions to confirm: #11, #14, #27, #28.

## Stage 0 resolutions

- **#1 CI display**: `helper-scripts/run-headless.sh` starts a private `Xvfb`
  display for the smoke test; `.github/workflows/ci.yml` installs `xvfb`, a
  font and the X11 dev packages. The fontconfig process-global leaks are
  suppressed narrowly in `tests/lsan.supp` (applied to the smoke test only).
- **#2 Stage 0 severity test**: the mapping lives in the Stage 0 logger as
  `log_level_from_libtsm_severity()` and is unit-tested by `tests/log_test.cpp`;
  libtsm's callback will call it in Stage 1.
- **#3 prerequisites/attribution**: `docs/dependencies-dev.md` lists the system
  packages (including the conditional `libutil` note); `README.md` carries the
  required FLTK attribution and the bundled-font attribution.
- **#4 system-dependency mode**: `cmake/deps.cmake` fails closed, probing
  FLTK's exported link libraries for X11/Xft and rejecting Wayland/Cairo/Pango,
  and requiring rapidyaml's exception-callback definitions.

Stage 0 implementation status: the fetched build configures, compiles and
passes CTest (link check, logger, headless smoke test) in Debug, Release and
ASan/UBSan configurations on Linux/X11. Dependency source was not modified.

## Stage 1 progress

- **S1-CS1 — session interfaces and libtsm emulator**: `include/app/error.hpp`,
  `session.hpp`, `emulator.hpp` and `src/terminal/emulator.cpp`; pimpl hides
  libtsm, `terminal_core` has no FLTK dependency. Unit test covers default
  size, valid/invalid resize, cursor movement and a device-attributes reply.
- **S1-CS2 — Linux PTY backend and startup worker**: `include/platform/startup.hpp`,
  `src/platform/linux/pty_session.{hpp,cpp}`, `startup_worker.cpp`. `openpty`
  in the parent, explicit `fork` + audited child setup (`setsid`, `TIOCSCTTY`,
  `dup2`, signal reset, `chdir`, `execve`), `O_CLOEXEC` error pipe handshake
  with cancellation wake FD, master set non-blocking, `pidfd_open` via
  `SYS_pidfd_open` with SIGCHLD fallback. One startup worker with bounded
  request/result queues, cancellation and `stop()` resolving every request.
  Application ignores `SIGPIPE` at startup (`src/app/main.cpp`).
- **Post-review fixes (S1-CS2)**: `stop()` wakes result waiters before joining
  (no deadlock on a full result queue); cancellation is published before the
  dequeue-time re-check so a pre-dequeue cancel is not lost, and the handshake
  resolves exec success before a simultaneous cancel; `fcntl` and child
  `setsid`/`TIOCSCTTY`/`dup2`/signal-setup failures are checked and reported;
  PATH search resolves relative components against an absolute base for the
  requested working directory, so a relative working directory plus a relative
  PATH entry produces a path valid after the child's `chdir`.
  `running()` is documented as backend I/O/stop state, not process liveness,
  and `StartupResult`'s descriptors are documented as borrowed views owned by
  the session.
- **#6 Settled**: the parent closes its copy of the slave immediately after
  `fork()` (`pty_session.cpp`); `tests/pty_session_test.cpp` verifies the
  master reports `IoStatus::eof` after the child exits. Additional Stage 1
  tests cover shell I/O, `exec_failed`, invalid dimensions, termination,
  `stop()` resolution and descriptor-leak checks, plus deterministic
  pre-dequeue cancellation and `stop()` with a saturated result queue, and a
  relative-working-directory + relative-PATH launch.
- **S1-CS3 — shared I/O loop and session queues**: `include/app/spsc_ring.hpp`,
  `message.hpp`, `session_queue.hpp`, `wakeup.hpp`, `src/app/session_queue.cpp`,
  `include/platform/io_loop.hpp`, `src/platform/linux/io_loop.cpp`. One
  application-owned loop thread polls a wake `eventfd`, a SIGCHLD self-pipe and
  each session's master FD; `pidfd` is the primary exit notification with the
  SIGCHLD scan as fallback. Commands (`add`, `remove`, `write_pending`,
  `resume`, `shutdown`) arrive through a bounded SPSC ring and are acknowledged
  through a separate ring. Read/write batches stop at 64 KiB or 1 ms, sessions
  are serviced round-robin, a full inbound queue suspends the whole master poll
  entry until `resume`, `POLLHUP`/`POLLERR` drive the read path, EOF retires the
  master, and reaping drains buffered output (100 ms active cap) before
  `exited`. The loop→UI `WakeupFlag` coalesces wakeups; the queues carry data
  batches plus reserved `eof`/`exited` lifecycle events ordered by a shared
  sequence. Tests: `wakeup_test` (coalescing), `session_queue_test` (ordering,
  bounds, SPSC stress), `io_loop_test` (shell exit ordering, write round-trip,
  backpressure suspension/resume, premature-shutdown rejection).
  **Deferred**: the `terminate`/kill-timer/process-group lifecycle (§1.6) and
  streaming paste/outbound budget/reply overflow (§1.7) are later change sets,
  so the loop does not yet expose `terminate`/`cancel-paste` commands.
- **Post-review fixes (S1-CS3)**: `SessionInboundQueue::try_pop` re-reads the
  other ring after selecting the later-published one, so the two-ring merge
  cannot return a newer event before an older one; `resume` triggers a
  post-exit drain before the loop can block, so a reaped session whose
  descendant keeps the PTY open still completes; read and write syscalls are
  capped to the remaining 64 KiB batch allowance; a permanently asserted
  `POLLHUP`/`POLLERR` followed by `would_block` retires the master and
  publishes EOF instead of busy-looping; loop acknowledgements are retained in
  a pending queue instead of being dropped; `signal_loop` retries `EINTR` and
  the loop refuses commands (and does not start) if its wake FD/self-pipe
  setup failed; the SIGCHLD FD is `volatile sig_atomic_t` and `waitpid` retries
  `EINTR`. Tests add a concurrent cross-ring ordering stress and a
  descendant-holds-the-PTY resume/post-exit-drain case. A second pass made
  post-exit completion conditional on an actual terminal drain state (so a
  byte/time-budget yield no longer publishes `exited` early), wakes the loop
  when the UI frees an event slot so retained acknowledgements are delivered,
  and quarantines `POLLERR`-only masters without claiming EOF.
- **S1-CS4 — session lifecycle (§1.6)**: `IoLoop` gains `terminate()` and an
  `add()` close-output flag; the factory takes the kill timeout (default 2 s).
  `terminate` captures the shell's group and the controlling terminal's
  foreground group, verifies each belongs to this terminal's session, dedups,
  sends SIGHUP, and starts a per-session deadline serviced through the `poll()`
  timeout (no timer thread); at the deadline it revalidates and sends SIGKILL.
  Repeated `terminate` does not restart the deadline, and shell exit does not
  cancel foreground escalation. `remove` is deferred until the signal sequence
  completes and the direct child is reaped, then acknowledged. With
  `drain_output_on_close == false`, `terminate` discards newly read output and
  skips the post-exit drain. Tests add SIGHUP-ignoring SIGKILL escalation and
  close-policy discard. **#7 Settled**: `group_in_session()` verifies the
  foreground group via `tcgetpgrp()` and other groups via the group leader's
  session id from `/proc/<pid>/stat`; an unverifiable group is skipped and
  logged. **Deferred**: streaming paste/outbound budget/reply overflow (§1.7),
  and the UI-side asynchronous shutdown orchestration (steps 1–4) belongs to
  the application layer.
- **#5 (concurrency)** remains open; it needs a TSan run and a short written
  ownership note (UI owns terminal state, loop owns registered-session I/O and
  termination, startup worker owns unregistered backends; only the queues and
  wake flag are shared).
