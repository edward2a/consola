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
| 5 | Concurrency verification | P2 | The design has three threads, lock-free single-producer/single-consumer queues and an atomic wake flag, but CI runs only ASan/UBSan, not ThreadSanitizer. Which thread may call `TerminalSession::running()` / `terminate()` is not stated, and neither is whether `log()` is thread-safe (the UI, I/O loop and startup worker all call it). | DIP §0.4, §0.7, §1.1, §1.7 | Stage 1 | Settled |
| 6 | Parent's copy of the PTY slave | P3 | 1.3 says the parent keeps the master FD but never says it closes its own copy of the slave after `fork()`. If the slave stays open, the Linux end-of-output signal (`EIO`/`POLLHUP`) never arrives. Test 24 would catch it. | DIP §1.3 › Required behavior | Stage 1 | Settled |
| 7 | Checking a process group is still ours | P2 | Before a delayed `kill(-pgid)`, 1.6 requires confirming the group still belongs to the terminal's session, but names no way to do it. Linux has no handle for a process group, and the group leader may already be gone. Pick a method and check it against test 18. | DIP §1.6 › terminate() | Stage 1 | Settled |
| 8 | Bracketed-paste injection | P2 | `tsm_vte_paste()` wraps the pasted text without filtering it. Pasted text containing `ESC[201~` ends the bracketing early, and the rest runs as if typed (source-checked). Decide whether to neutralise that marker before encoding. | DIP §1.7 › Streaming paste; §2.8 | Stage 2 | Settled |
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
| 19 | Config schema is incomplete | P3 | Settings promised as configurable elsewhere are missing from 5.2: TERM/COLORTERM, login shell, word chars, cancel-paste shortcut, bell mode, cursor style. Precedence between general and profile settings (shell, font, scrollback) is undefined. | DIP §5.2; §1.2, §2.7, §2.8, §6.2, §6.4 | Stage 5 | Settled |
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
- 15 Pending, 7 Reviewed, 9 Settled.
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
  close-policy discard, plus an early-removal regression (removal is
  acknowledged only after `exited` is published, so output is not truncated)
  and a shell-exits-while-a-foreground-group-ignores-SIGHUP escalation case.
  **#7 Settled (best-effort)**: `group_in_session()` requires the controlling
  terminal to still report this session via `tcgetsid()` before trusting
  `tcgetpgrp()`, else the group leader's session id from `/proc/<pid>/stat`;
  an unverifiable group is skipped and logged. Verification is not atomic with
  `kill()`, and a leaderless non-foreground group is skipped, so this is a
  fail-closed best effort, not an absolute no-reuse guarantee. **Deferred**:
  streaming paste/outbound budget/reply overflow (§1.7); the UI-side
  asynchronous shutdown orchestration (steps 1–4); and reconciling
  `TerminalSession::running()` with loop-owned termination.
- **S1-CS5 — outbound budget (§1.7, loop side)**: `SessionOutboundQueue`
  gains an `OutboundAdmission` class and a reserve (`kDefaultReserve` = 16 KiB):
  `user` bytes (keys, paste payload) are admitted only up to
  `byte_limit - reserve`, while `reserved` bytes (required replies, framing)
  may use the whole limit, so a full user queue cannot make a small reply fail.
  Credits now return only as bytes are written or discarded (`release()`), not
  merely on `pop_front()`; the loop releases each written count and wakes the
  UI when credits free so it can admit deferred input. Tests cover the reserve
  boundary and loop-side credit return. **Deferred to Stage 2 §2.8**: the
  streaming-paste transaction (capture/framing/cancel/`paste_finished`) and the
  `reply_overflow` failure path, because §2.8 must verify libtsm framing before
  implementing capture and cancellation; Stage 1's exit criteria do not require
  paste.
- **Stage 1 exit verification**: `io_loop_test` adds an end-to-end case that
  feeds loop-delivered PTY output into `TerminalEmulator` and checks the libtsm
  cursor, covering the "output reaches libtsm" criterion; the remaining
  criteria (shell start, command execution, resize, exit detection, no
  UI-thread PTY blocking) are covered by the PTY/loop tests and the dedicated
  I/O thread. A `CONSOLA_TSAN` option and `tsan` preset were added for the
  watchlist #5 concurrency check, and the full suite passes under TSan.
- **#5 Settled (concurrency)**: `log()` is mutex-guarded and thread-safe
  (`src/app/log.cpp`). Ownership: the UI thread owns terminal state and calls
  `resize()`; the I/O loop thread calls `read()`/`write()` and owns registered
  session I/O and termination; the startup worker calls `start()` while a
  backend is unregistered; only the SPSC queues and the atomic `WakeupFlag` are
  shared. `running()`/`terminate()` belong to the backend owner — the loop owns
  termination via its `terminate` command, so direct calls must not race it.
  These rules are recorded on `TerminalSession` in `include/app/session.hpp`. A
  `CONSOLA_TSAN` option and `tsan` preset were added (also run from
  `helper-scripts/ci-linux.sh`); the full CTest suite passes under TSan
  (8/8, including the loop/queue/PTY tests and the Xvfb smoke test).

## Stage 2 progress

- **S2-CS1 — widget boundary and terminal model API**: `include/app/emulator.hpp`
  exposes the renderer read surface without leaking libtsm types: `Rgb`,
  `CellAttributes`, `Cell` (codepoints + width 1/2/0 + age), `CursorStyle`,
  `CursorState`, and `TerminalEmulator::draw(CellCallback, void*)` wrapping
  `tsm_screen_draw()` (not `draw2()`), plus `cursor()`, `scrollback_line_count()`
  and `scrollback_line_pos()`. `include/ui/terminal_widget.hpp` +
  `src/ui/terminal_widget.cpp` add a presentation-only `TerminalWidget` with a
  model reference and stub `draw()`/`handle()`; cell rendering lands in S2-CS2.
  `tests/emulator_test.cpp` now traverses the screen and checks cell text/width
  and cursor state.
  **Finding**: pinned libtsm 4.8.0 has no dedicated title accessor; OSC 0/2 must
  be captured through the generic `tsm_vte_set_osc_cb()` callback and parsed by
  us in §2.12 (no separate raw escape parser needed).
  **Deferred**: font setup/basic rendering (S2-CS2), dirty regions (S2-CS3),
  input (S2-CS4/5), paste (§2.8), scrollback/title/bell (S2-CS7).
- **S2-CS2 — font setup and basic rendering (§2.3–§2.4)**:
  `include/app/terminal_style.hpp` adds `TerminalStyle` (16 ANSI colours +
  foreground/background/cursor, font family/size) with C++ defaults; the
  emulator applies it via `tsm_vte_set_custom_palette()` at construction, before
  output. `TerminalWidget` derives cell metrics from FLTK
  (`fl_width("M")`, `fl_height()`, ascent) and renders the background, cells
  (absolute rounded grid origins, per-cell clipping, UTF-8 via `fl_utf8encode`,
  inverse/dim/underline and bold/italic face selection) and the cursor
  (block/underline/vbar; libtsm's cursor-cell inversion neutralised; viewport
  row from scrollback + clamped raw cursor position — watchlist **#10**).
  `application.cpp` now hosts a `TerminalWidget` fed by a `TerminalEmulator`, so
  the Xvfb smoke test exercises rendering. Review fixes: the configured
  `font_family` is mapped to FLTK's base and bold/italic faces; the fractional
  cell advance is kept and grid origins are rounded from it; the cursor glyph
  reuses the cell's face/underline and `TerminalStyle::cursor` is documented as
  reserved (#18); the UTF-8 buffer is sized from the cell's codepoint span; and
  the `tsm_vte_set_custom_palette()` result is checked. **Carry**: #11 (glyph
  fallback), #12 (fractional scaling), #13 (bold/italic face cost/fallback) are
  checked in rendering; blink timing and the scrollbar land in S2-CS3/S2-CS7.
- **S2-CS3 — dirty-region tracking (§2.5)**: `TerminalWidget` keeps the last
  painted content age and repaints only damaged areas. Content damage comes
  from cells whose age exceeds the saved age (a collect pass computes their
  bounding box); explicit invalidation covers the old and new cursor cells,
  FLTK's exposed region, viewport/scrollback changes and resize/style changes,
  which force a full repaint. Damaged regions are cleared to the background
  before cells and overlays are redrawn, so unchanged cells in a damaged area
  are repainted too. The saved age advances only after the paint pass; an age
  reset or overflow (libtsm returns 0) keeps the full-redraw fallback.
  `set_style()`/`resize()` request full repaints. The Xvfb smoke test now feeds
  a second line and triggers a partial redraw so the age-driven path runs under
  sanitizers. **Carry**: selection invalidation (S2-CS5) and visual-bell
  overlays (Stage 6) must invalidate old and new areas; blink timing is still
  to come. Review fixes: an unclipped `fl_clip_box()` now means the whole
  widget is drawable; damage that extends beyond FLTK's clip is kept pending
  (the age is not advanced and a full repaint is requested); the cursor cell is
  flagged for repaint before the wide-glyph continuation early-out; a metric
  change (DPI/font) forces a full repaint; `set_style()` documents that runtime
  palette changes for existing content are out of scope (DIP 2.3).
- **S2-CS4 — keyboard input (§2.6)**: `include/app/key_event.hpp` adds a
  platform-neutral `KeyEvent` (keysym/ascii/modifiers/unicode) with our own
  `KeyModifier` bits; `TerminalEmulator::handle_keyboard()` maps them to
  `TSM_*_MASK` and calls `tsm_vte_handle_keyboard()`, so libtsm types stay
  inside the emulator (watchlist **#15** largely addressed). `backspace` sends
  DEL via `tsm_vte_set_backspace_sends_delete()`. The UI-side
  `translate_key()`/`translate_fltk_key()` (`include/ui/key_translator.hpp`)
  passes special keys through as keysyms, supplies the ASCII keysym for Ctrl
  combinations, decodes `Fl::event_text()` for composed codepoints, and maps
  FLTK modifier state; `TerminalWidget::handle()` consumes `FL_KEYDOWN` and
  takes focus on `FL_PUSH`. Review fixes: `dispatch_fltk_key()` splits composed
  multi-codepoint text into one keysym-less `KeyEvent` per codepoint; the
  Ctrl-ASCII claim is narrowed to a best effort (layout-aware lookup is not
  implemented, #16); tests add backspace, U+0000 and Meta+Shift.
  Tests: `key_translator_test` and libtsm encoding checks in `emulator_test`
  (Ctrl+C, text, left arrow). **Carry**: #14 (FLTK focus/Escape/Tab),
  #16 (IME/layout verification), #17 (capability matrix); modifyOtherKeys and
  the kitty keyboard protocol remain v1 limitations.
- **S2-CS5 — mouse input and selection (§2.7)**: `include/app/mouse_event.hpp`
  adds platform-neutral `MouseEvent`, `MouseButton`, `MouseModifier`,
  `MouseAction` and `MouseTracking`; the emulator wraps
  `tsm_vte_get_mouse_mode/event` (`mouse_tracking()`) and `tsm_vte_handle_mouse`
  (`handle_mouse()`), keeping libtsm types inside the emulator. Selection wraps
  libtsm (`selection_start/target/reset/copy`); `select_word()` computes
  boundaries from a Terminator-style `word_chars` class (literal chars plus
  `a-z` ranges) by reading cell codepoints with `tsm_screen_draw2()` (read-only,
  not rendering). `TerminalWidget` handles press/drag/release/wheel: clicks
  focus the pane; when mouse reporting is enabled it forwards encoded events,
  otherwise it drives selection (double-click selects a word, drag extends);
  selection changes force a full repaint because libtsm inverts selected cells
  in the draw callback and does not age them. Tests cover VT200 press encoding
  and selection/word copy. Review fixes: drag moves encode `button + 32`
  (with a `dragging` flag distinguishing a drag from a hover move); pixel
  coordinates are terminal-local; a double-click release no longer shortens the
  word selection; `select_word()` validates the row; the word-class grammar is
  documented as ASCII-only; the copy buffer is freed with RAII. **Carry**: #14
  (GUI click/focus test), #16, wheel scrollback and Shift+wheel while reporting
  (S2-CS7), copy/paste wiring (S2-CS6), and selection coordinates with
  scrollback.
- **S2-CS6 — clipboard and paste capture (§2.8)**: `TerminalEmulator` adds
  `capture_paste(text, max_bytes)`: it runs `tsm_vte_paste()` once with the
  write callback redirected into a bounded buffer (bracketed-paste markers
  included when mode 2004 is set) and returns `PasteCapture{ok, bytes}`,
  rejecting an oversized source, an over-limit encoded result, or an empty
  capture. `TerminalStyle` gains `max_paste_bytes` (16 MiB). `TerminalWidget`
  copies the selection to the clipboard and primary buffer (`Fl::copy`),
  requests the clipboard for paste (`Fl::paste` -> `FL_PASTE` -> capture ->
  `PasteCallback`), and intercepts Ctrl+Shift+C/V. `IoLoop::fail_session()`
  fails a session through the normal termination/reaping path (for
  `reply_overflow`). Tests: bracketed/unbracketed/oversized/empty capture and
  fail-session termination. **Deferred to Stage 4** (needs the session
  wiring): the streaming paste transport (queue-fed chunks, cancel-paste,
  `paste_finished`, deferred keys/replies, reserve integration) and watchlist
  #8 (bracketed-paste injection) / #9 (framing and mode exposure). Review
  fixes: the capture is exception-safe (source/buffer built before capture
  state is set; allocation failures in the write callback mark the capture
  failed instead of escaping libtsm); `capture_paste()` strips the bracketed
  framing markers from the payload (**#8 Settled**); middle-click pastes the
  primary selection and a rejected paste is logged. **#9** remains partly open:
  bracketed vs unbracketed framing is test-verified, but libtsm exposes no
  mode-2004 getter.
- **S2-CS7 — scrollback, title and bell (§2.9, §2.12, §2.8a)**:
  `TerminalEmulator` exposes scrollback control (`set_max_scrollback`,
  `scroll_lines/page`, `scroll_to_top/bottom`) plus the existing line
  count/position; window title from OSC 0/2 via `tsm_vte_set_osc_cb()` with a
  `TitleCallback`; and a no-op bell callback. `TerminalWidget` maps
  Shift+PageUp/PageDown/Home/End to scrollback, scrolls on the wheel (and on
  Shift+wheel even while mouse reporting is active), and draws a scrollbar
  when scrollback exists (libtsm: position 0 is the top, position == count is
  live). `application.cpp` sets a 10,000-line scrollback and updates the window
  label from the title callback. Tests cover scrollback count/position and
  OSC 0/2 titles. **Carry**: #21 (how URL detection reads screen text), the
  audible/visual bell (Stage 6), and scrollbar dragging/interaction. Review
  fixes: the cursor row is clamped before the viewport-row calculation (libtsm
  clamps the cursor when drawing); a byte-producing key returns the view to
  live; the wheel on the alternate screen with reporting off sends Up/Down
  keys. **Carry**: the scrollbar is drawn over the rightmost cell area and is
  not yet draggable (§2.9), and dynamic scrollback-cap changes are not tracked
  independently.
- **Stage 2 exit verification**: `application.cpp` now wires a single session
  end to end — a `SessionHost` starts the `StartupWorker` and `IoLoop`, launches
  `$SHELL`, drains the inbound queue into the `TerminalEmulator`, routes the
  emulator's encoded output and widget pastes into the session's outbound queue,
  resizes the session on widget resize, and closes the window on `exited`. This
  covers "an interactive shell is usable", "typing works" (keys are encoded by
  the emulator and routed to the session), "scrolling works", "copy/paste
  works", "terminal resize works", "cursor works", and "text renders / basic
  colour and attributes" (rendering + palette). Verified by running the app
  headless against a script shell (prints and exits): the app renders the
  output, detects the exit and exits 0. `terminal_ui` now links
  `terminal_platform`. Review fixes: an orderly `SessionHost::shutdown()` runs
  after the event loop — it cancels the startup worker (resolving late
  successes), terminates the session through the loop (SIGHUP/SIGKILL
  escalation), pumps until the removal acknowledgement, then stops the loop —
  so closing the window mid-session reaps the child instead of leaking it; a
  launch/registration failure returns `EXIT_FAILURE` instead of a silent
  success. A `CONSOLA_EXIT_AFTER_MS` test hook closes the window headlessly;
  verified against a SIGHUP-ignoring shell (escalation + reap, rc 0, no
  lingering child).
- **S3-CS1 — layout tree and core operations (§3.1, §3.2)**:
  `include/layout/layout_tree.hpp` and `src/layout/layout_tree.cpp` add a
  FLTK-independent binary split tree over `TerminalId` leaves. `LayoutNode` is
  a `std::variant<SplitNode, TerminalNode>`; nodes use stable `NodeId` values
  with parent links, never pointers. Operations: `split_terminal`
  (horizontal/vertical, ratio in (0, 1)), `remove` (collapses the parent so the
  sibling takes the freed space), `find_parent`, `focus`, `equalize`, and a
  depth-first `terminals()` walk. `Orientation::horizontal` means a horizontal
  divider (children stacked top to bottom) and `vertical` means a vertical
  divider (children left to right), matching Terminator's `split_horiz`
  (`VPaned`) / `split_vert` (`HPaned`). Rectangle calculation (§3.3) is
  deferred to S3-CS2. Tests: empty/single/nested/repeated splits, removal and
  root collapse, split rejections (absent/duplicate target; ratio 0, 1,
  negative, > 1, NaN), focus, and focus migration. Review fixes: focus migrates
  to the sibling subtree's first depth-first leaf when the sibling is itself a
  split (previously focus was dropped); non-finite split ratios are rejected.
  `layout_test` is wired into CTest; Debug and ASan/UBSan suites pass 10/10;
  clang-format/clang-tidy clean.
- **S3-CS2 — layout rectangle calculation (§3.3)**:
  `include/layout/layout_geometry.hpp` and `src/layout/layout_geometry.cpp`
  turn a content rectangle plus the tree into one rectangle per terminal leaf,
  independent of FLTK. `LayoutMetrics` carries the UI-supplied cell size and
  pane chrome (border, scrollbar, divider); `minimum_size()` returns the
  content-only minimum (two columns by one row per leaf plus chrome, combined
  bottom-up: sum along the split axis plus divider thickness, max on the other
  axis) and the caller adds window/tab chrome. `compute_layout()` tiles the
  content with whole pixels and no gaps/overlaps, clamps divider positions to
  both subtree minimums while leaving stored ratios untouched, and on an
  undersized or zero-area content lays out at max(actual, minimum) per axis and
  clips each pane to the content area anchored top-left. `PaneRect` exposes the
  logical rectangle (for deriving rows/columns, §2.11) and the visible
  intersection (for clipping drawing and hit-testing); `DividerRect` exposes the
  same for split dividers, for §3.5. Geometry uses 64-bit intermediates with
  results clamped to the int range so extreme metrics/origins cannot overflow.
  Subtree minimums are computed once per call and cached. Tests: single,
  vertical/horizontal/nested tiling, boundary-sized layouts, undersized and
  zero-area clipping (including a non-zero origin), ratio preservation across
  resizes, divider clamping, empty tree, negative content, and extreme
  metrics/origins. `layout_geometry_test` is wired into CTest; Debug and
  ASan/UBSan suites pass 11/11; clang-format/clang-tidy clean. Review fixes:
  64-bit intermediate geometry with clamping; per-call minimum caching (removes
  quadratic recomputation); divider `visible` intersection added; added
  non-zero-origin partial-clip and extreme-value tests.
- **S3-CS3 — single-tab ownership and asynchronous factory (§3.0, §4.1,
  §4.3)**: `include/app/terminal_instance.hpp` adds `TerminalInstance` (state
  machine, emulator, session queues, attached backend); `include/app/tab.hpp`
  adds `Tab`, which owns the `LayoutTree` and the `TerminalId ->
  unique_ptr<TerminalInstance>` map; `include/app/terminal_manager.hpp` adds
  `TerminalManager`, which owns the application-wide `StartupWorker` and
  `IoLoop`, owns the single Stage 3 `Tab`, allocates application-wide
  `TerminalId`s and is the single asynchronous factory. `create_root_terminal`
  / `split_terminal` create a `Starting` instance and queue the launch;
  `pump()` consumes results (attaching the backend and registering with the
  loop), inbound data and loop events; input is disabled until startup
  succeeds. `std::hash<TerminalId>` was added. A new `terminal_session` library
  target links `terminal_core` + `terminal_platform`; `terminal_ui` does not use
  it yet (the FLTK application migration is S3-CS5). Tests: independent PTYs
  through the factory, launch failure, send-to-running, root rejection, and
  SIGHUP-ignoring shutdown reaping. Review fixes: the manager now owns its tab
  (the previous external-tab API caused a manager-registry use-after-free caught
  by ASan); `tab()` is read-only so callers cannot destroy instances behind the
  registry; instances enter `starting` before launch and `attach()` sets
  `running`; `release_session()` disables input; EOF disables input; `add_root`
  enforces its empty/no-duplicate precondition. Review follow-ups still open:
  the running application still uses the old `SessionHost` (S3-CS5), and full
  per-pane close (§4.4) remains for S3-CS4. **Deferred to S3-CS4**: per-pane
  `close_terminal` (cancel a starting pane, close a running pane, rebalance).
  `terminal_session`, `tab_test` and `terminal_manager_test` are wired into
  CTest; Debug and ASan/UBSan suites pass 13/13; clang-format/clang-tidy clean.
- **S3-CS3 shutdown barrier (§4.5)**: `TerminalManager::shutdown()` rejects new
  terminals, resolves pending launches (registering a late success and closing
  it, finalizing a failed/cancelled launch), terminates running sessions through
  the loop (SIGHUP/SIGKILL escalation owned by the loop), keeps pumping until
  every child is reaped and every removal acknowledged, then shuts the loop
  down. The destructor calls it. Test: a SIGHUP-ignoring shell is escalated and
  reaped within the 5 s barrier.
- **S3-CS4 — per-pane close (§4.4)**: `TerminalManager::close_terminal()` marks
  the pane closing (which disables input) and either cancels a `Starting`
  launch, terminating a running pane through the loop, or finalizing a failed
  launch. `pump()` consumes the launch result (a late success is registered and
  then closed through the normal steps), waits for `exited`, requests `remove`
  and, on the removal acknowledgement, releases the backend and removes the
  layout node (which collapses/rebalances the tree). A cancelled launch is no
  longer counted as a launch failure. Tests: close a running pane (tree
  collapses to the survivor), close a starting pane before its launch resolves
  (no launch failure), and close under output backpressure with the default
  discard-on-close policy. Review fixes: loop commands are retried until the
  bounded queue accepts them (`remove` is only recorded as requested once
  accepted, and `terminate` is tracked so it is sent once); every attached
  session — including an already-exited one — gets `terminate` so the removal
  acknowledgement covers the foreground signal sequence; and queued data for a
  closing pane is discarded without feeding the emulator when
  `drain_output_on_close` is false. Added a deterministic late-launch-success
  close test. Debug and ASan/UBSan suites pass 13/13; clang-format/clang-tidy
  clean.
- **S3-CS5 — FLTK integration (§3.4)**: `include/ui/terminal_view.hpp` adds
  `TerminalView`, which creates one `TerminalWidget` per terminal node, computes
  pane geometry with the layout engine (`compute_layout`) and applies it to the
  widgets; widgets never own layout relationships. The widget bounds use the
  logical pane rectangle, so the grid derives from it (DIP 2.11) while FLTK
  clips drawing to the window. Per-terminal callbacks route the emulator's
  encoded output, pastes, resizes and titles to the manager. `application.cpp`
  was migrated off the ad-hoc `SessionHost` onto `TerminalManager` +
  `TerminalView`: it owns the manager, starts the `$SHELL` root terminal, pumps
  the manager from the FLTK timer, and shuts down through the barrier.
  `terminal_ui` now links `terminal_session`. `TerminalManager` resizes a
  session to its emulator's grid when the backend attaches. A headless
  `session_smoke_test` runs the app against `/bin/sh` with
  `CONSOLA_EXIT_AFTER_MS`. **Fixed (watchlist #13 class)**: `Fl::set_font()`
  keeps the `const char *` it is given, but `TerminalWidget::apply_font_family`
  passed temporaries; the styled face names are now stored on the widget (a
  heap-use-after-free ASan caught in FLTK's Xft font path). Debug and ASan/UBSan
  suites pass 14/14; clang-format/clang-tidy clean. Review fixes: the styled
  face names now live in process-lifetime storage (FLTK's face table is global
  and holds the pointers past any one widget); `TerminalView::~TerminalView`
  clears emulator output/title callbacks before the callback contexts are
  destroyed; `pump()` relayouts when the terminal topology changes (added or
  removed panes), not only on window resize; the widget reserves the scrollbar
  strip before deriving columns and the manager skips a PTY resize when the grid
  is unchanged (DIP 2.11); `TerminalManager::terminals()` was replaced by the
  constrained `terminal_ids()` + `emulator(id)`.
- **S3-CS6 — pane interaction (§3.5)**: the layout engine gains
  `LayoutTree::set_split_ratio`, `focus_neighbor` (directional focus on the
  logical pane rectangles) and `split_ratio_for_position` (drag position to a
  ratio, clamped to both subtree minimums); `DividerRect` now carries the
  split's `area`. `TerminalManager` forwards `focus`/`focused`/
  `set_split_ratio`/`equalize`. `TerminalView` is now an `Fl_Group` (so it sees
  events in the divider gaps) and implements focus-click (a widget focus
  callback), directional focus, `split_focused` (with a minimum-size preview
  that refuses an unfittable split before launching), `close_focused`, divider
  drag-resize and `equalize`; it keeps the FLTK focus on the focused terminal.
  Tests: core `set_split_ratio`, `focus_neighbor` and
  `split_ratio_for_position`, plus a headless `terminal_view_test` that splits,
  navigates focus, equalizes and closes a pane through the view. Debug and
  ASan/UBSan suites pass 15/15; clang-format/clang-tidy clean. Review note: the
  styled face names are now held in an append-only store, because FLTK's global
  face table keeps the pointers and reassigning a string freed a name FLTK still
  referenced (a second ASan use-after-free in the Xft font path). Review
  fixes: the face names are interned in a node-based map (stored once, stable
  addresses, no growth across pane churn); the view test now asserts the
  equalize result and that an unfittable split is refused before launch.
  Divider grab tolerance over pane widgets and non-zero view origins were
  checked and need no change: the view's `handle` runs before its children and
  FLTK child coordinates are parent-relative.
- **S3-CS7 — command abstraction and shortcuts (§3.6)**: `include/ui/commands.hpp`
  adds `enum class Command` (split/close/focus/resize) and `CommandRequest`
  (command + direction); `command_for_key` maps FLTK keys and modifiers to a
  command, keeping the shortcut table separate from the command set.
  `TerminalWidget` gains a key callback consulted before terminal input;
  `TerminalView::execute` dispatches commands to split/close/directional-focus/
  resize, and `resize_focused` moves the divider adjacent to the focused pane by
  one cell. Default shortcuts follow Terminator: Ctrl+Shift+O/E split, Ctrl+
  Shift+W close, Alt+Arrow focus, Ctrl+Shift+Arrow resize. Tests: `commands_test`
  (mapping and non-matches) and the view test now drives `execute`. Debug and
  ASan/UBSan suites pass 16/16; clang-format/clang-tidy clean. Review fixes: a
  recognized shortcut is consumed even when the command cannot apply, so it
  never leaks to the terminal; `resize_focused` requires perpendicular overlap
  with the focused pane (nested layouts) and refuses to overwrite the stored
  ratio when a nudge is clamped by a subtree minimum
  (  `split_ratio_for_position` now reports `clamped`); shortcut modifier matches
  are exact (no extra Ctrl/Shift/Alt).
- **Stage 3 exit review**: Luna-Six returned FAIL on two blocking items, both
  fixed. (1) The synchronous shutdown waited only 5 s and then force-finalized
  registered sessions, and the application had already left `Fl::run()`. The
  manager now exposes `begin_shutdown()` / `shutdown_complete()` /
  `finish_shutdown()`: the application drives teardown from the FLTK timer
  (window close and `CONSOLA_EXIT_AFTER_MS` request shutdown without hiding the
  window), keeping the UI loop alive through reaping and removal; `shutdown()`
  remains a synchronous convenience for tests and the destructor and only
  releases registered sessions after the loop has stopped (30 s safety net).
  (2) The layout minimum was not advertised to the window manager;
  `TerminalView::layout()` now calls `Fl_Window::size_range()` with the tree
  minimum. Added a shutdown-under-output-backpressure test and a forced-
  undersizing test asserting the live PTY grid stays positive. Debug and
  ASan/UBSan suites pass 16/16; clang-format/clang-tidy clean. A second exit
  pass asked for the timeout path to be removed and metric-only relayout: the
  synchronous `shutdown()` no longer force-finalizes registered sessions (it
  waits for every acknowledgement; the loop's SIGKILL escalation bounds it),
  and `TerminalView::pump()` now detects font/DPI metric changes and relayouts,
  refreshing the advertised window minimum. A third pass found a pre-start
  creation hazard and an undefined loop-failure path: creation now requires
  `start()`, and `IoLoop::running()` reports whether the loop thread is alive
  (false after an unrecoverable poll error), so the synchronous `shutdown()`
  waits only while the loop runs and finalizes remaining instances only once the
  loop has stopped (a defined failure path, not a timeout). A fourth pass asked
  for the fallback to terminate and reap remaining children and for the
  asynchronous path to detect loop failure: `IoLoop::start()` now returns
  whether the loop started, the loop marks a session reaped when it waitpid()s
  it (`TerminalSession::mark_reaped`), and `TerminalManager::fail_remaining()`
  joins the loop and terminates/reaps only un-reaped children before releasing
  them; `pump()` detects a stopped loop and runs the fallback, so the
  application's timer completes instead of hanging. `io_loop_test` covers
  `start()`/`running()`/`stop()`. A fifth pass closed two fallback corners:
  `LinuxPtySession::terminate()` no longer signals a child the loop already
  reaped (reused-pid safety), and any launch result that arrives for an
  untracked instance (loop-failure race) is terminated and reaped before
  release; `fail_remaining()` also rejects further creation. A sixth pass fixed
  the remaining `begin_shutdown()` registration-failure branch (terminate+reap
  before release). **Stage 3 exit review verdict: PASS** — no blocking findings.
  Non-blocking: the registration-failure and unrecoverable-loop-failure branches
  are defensive paths without dedicated failure-injection tests. Stage 4 carries
  the planned multi-tab scope (`TabManager`, independent tab layouts, tab
  lifecycle); no Stage 3 lifecycle blocker carries forward.
- **S4-CS1 — TabManager and multi-tab ownership (§4.1, §4.2)**:
  `include/app/tab_manager.hpp` and `src/app/tab_manager.cpp` add an
  application-wide `TabId` and a `TabManager` that owns the ordered tabs and the
  active selection. It always holds at least one tab (so the active tab is
  always valid); operations are create, select, next/previous (wrapping), move
  (clamped, keeping the active tab), and remove (empty tabs only; removing the
  last replaces it with a fresh empty one). `Tab` gains an optional title for
  the Stage 4 tab bar. `TerminalManager` now owns a `TabManager` instead of a
  single `Tab`, keeps the app-wide `TerminalId` registry plus a
  `TerminalId → TabId` index, and adds `create_tab`, `create_root_terminal(tab,
  …)`, `close_tab`, `select_tab`/`select_next_tab`/`select_previous_tab`,
  `move_tab`, `tab_of`, and app-wide `terminal_count`. `split_terminal` resolves
  its tab from the target id, so a pane in any tab can split. `focus(id)`
  selects the tab that owns the id before focusing, so clicking a pane selects
  its tab. `close_tab` closes every pane through the normal path and removes the
  tab once its last terminal is finalized. `TerminalView::pump()` now reports
  app-wide activity (`terminal_count`), and `layout()` shows only the active
  tab's panes while hiding (but retaining) the other tabs' widgets, so per-tab
  emulator state survives a switch. Tests: a new `tab_manager_test` (start
  state, create/select, wrapping next/previous, move, remove-empty and
  remove-last, remove rejects non-empty) and `terminal_manager_test` additions
  (independent terminals across tabs, split in a non-active tab, close a tab
  with two panes, tab selection/reorder, shutdown across tabs). Debug and
  ASan/UBSan suites pass 17/17; clang-format/clang-tidy clean. Review fixes:
  tab access is const-only (`TabManager`/`Tab` exposed read-only; all mutations
  go through manager methods, with `set_tab_title` added for the tab bar), and
  `TerminalView` tracks the last laid-out tab and relayouts when the active tab
  changes (including after `focus()` selects another tab); `terminal_view_test`
  covers tab-switch widget visibility.
- **S4-CS2 — FLTK tab bar and per-tab view (§4.2)**:
  `include/ui/tab_bar.hpp` + `src/ui/tab_bar.cpp` add `TerminalTabBar`, a
  pure-view FLTK strip (one box per tab, active tab highlighted, a close glyph
  on each) that fires select/close callbacks and exposes hit testing
  (`tab_at`/`close_at`) plus a cheap `sync()` for redraw-on-change.
  `include/ui/terminal_frame.hpp` + `src/ui/terminal_frame.cpp` add
  `TerminalFrame`, a window container that lays out the bar above the view and
  is the window's resizable. `TerminalView` gains
  `new_tab`/`close_tab`/`select_tab`/`select_next_tab`/`select_previous_tab` and
  dispatches the new commands; `Command` gains
  `NewTab`/`CloseTab`/`NextTab`/`PreviousTab` with `command_for_key` mapping
  Ctrl+Shift+T, Ctrl+Shift+X, Ctrl+PageDown and Ctrl+PageUp. The application
  builds the frame, routes the bar's select/close callbacks through the view,
  and syncs the bar each frame. An inactive tab's terminal title no longer
  retitles the window. Tests: `tab_bar_test` (hit testing, close region,
  callbacks, sync), `commands_test` mappings, and `terminal_view_test`
  view-level tab commands. Debug and ASan/UBSan suites pass 18/18;
  clang-format/clang-tidy clean. Review fixes: FLTK widget coordinates are
  window-relative, so `TerminalView::layout()` now translates the view-local
  layout rectangle by the view's window position (panes no longer overlap the
  bar) and the advertised window minimum includes the bar height via
  `set_top_inset`; `TerminalTabBar::sync()` also detects reordering and title
  changes, and `tab_at`/`close_at` reject coordinates outside the bar or past
  the last tab. A new `terminal_frame_test` asserts the bar/view geometry and
  pane window position under resize; suites pass 19/19.
- **S4-CS3 — multi-tab lifecycle (§4.2, 4.4, 4.5)**: hardening and coverage for
  closing and shutting down across tabs. `TerminalView` refreshes the window
  title from the newly active tab's focused terminal on tab switch (carried
  from S4-CS2), so an inactive tab's OSC title update never leaks but selecting
  its tab restores the label. `terminal_manager_test` adds: closing a tab whose
  only pane is still `Starting`, closing a tab racing an already-successful
  (late) launch, closing a tab whose launch failed, closing a non-active tab
  (the active tab is untouched), rapid create/destroy of six tabs, and
  application shutdown with two flooding tabs under both
  `drain_output_on_close` settings. `terminal_view_test` adds the tab-switch
  window-title check. Debug and ASan/UBSan suites pass 19/19;
  clang-format/clang-tidy clean. Review fix: the window title follows the
  *focused* terminal of the active tab — `title_callback` now ignores
  background panes (not just inactive tabs) and `focus_callback` refreshes the
  label on focus change; added a test that closes a tab after one of its panes
  has already entered the close path.
- **Stage 4 exit review**: Stage 4 (Tabs and Sessions) is complete — multiple
  tabs, arbitrary per-tab splits, independent PTYs per terminal, and leak-free
  tab/pane lifecycle. The deferred **streaming paste transport** (DIP 1.7:
  queue-fed chunks, cancel-paste, `paste_finished`, deferred keys/replies,
  reserve integration) is explicitly deferred until after the application is
  complete and is not part of Stage 4's exit criteria.
- **Post-Stage-4 UX fixes**:
  (1) The default font rendered italic because FLTK's Xft driver reads the first
  character of a font name as a style modifier ('I'/'B'/'P'/' '), so the family
  "Inconsolata Condensed" was misread as italic. New `include/ui/font_name.hpp`
  / `src/ui/font_name.cpp` encode the modifier explicitly
  (`fltk_raw_font_name`), `TerminalWidget::apply_font_family()` uses it, and
  `font_name_test` covers the encoding (including families that begin with a
  style letter).
  (2) An exited terminal left a dead pane on screen. `TerminalView::close_exited()`
  now closes any pane whose process has exited (the loop always drains final
  output before `exited`, DIP 1.6) and removes a tab that becomes empty while
  other tabs remain; the last tab is kept so the app can shut down once empty.
  (3) Closing was slow and noisy: terminating an already-exited child waited the
  full kill deadline and logged "skipping termination target not in the terminal
  session". `LinuxIoLoop` now skips a target group that no longer exists
  silently (`group_exists`) and `service_deadlines()` completes the signal
  sequence as soon as no target group survives instead of waiting for the
  deadline; the `session_smoke_test` runtime dropped from ~2.7s to ~0.7s. New
  `io_loop_test::test_terminate_after_exit_is_prompt` uses a 5s kill deadline and
  asserts removal is acknowledged within 1.5s; `terminal_view_test` covers pane
  and empty-tab auto-close. Debug and ASan/UBSan suites pass 20/20;
  clang-format/clang-tidy clean. Review carry: while a signal sequence is in
  flight, `poll_timeout_ms()` now caps the wait at 50 ms so `service_deadlines()`
  re-checks target survival even when no poll wake source remains (e.g. a
  reaped leader whose SIGHUP-terminable group member exits on its own);
  `io_loop_test::test_terminate_after_leader_exit_is_prompt` covers it.
- **Font robustness follow-up**: the italic misparse was only half the font
  bug. When the configured family does not resolve (for example if the bundled
  font is unavailable), FLTK falls back to a *proportional* face; because the
  cell width is measured from "M", every narrow glyph was followed by a wide gap
  (the reported "characters taking two spaces"). `TerminalWidget::apply_font_family()`
  now verifies the resolved face is monospace (`monospace_metrics`) and
  substitutes a monospace family (Inconsolata, DejaVu Sans Mono, …) otherwise,
  and reuses the base/bold face for any style (e.g. italic) the family does not
  provide so styled text keeps the cell grid. Verified by rendering: the bundled
  font gives a 5px glyph + 1px gap, and the fallback path a 7px + 1px gap — both
  monospace, versus a spaced 8px + 4px proportional fallback before. A
  follow-up guards against a *full-width* (CJK) fallback whose ~1em advance
  passes a plain monospace test but doubles Latin spacing: `suitable_metrics`
  rejects an advance greater than 0.85 × font size, and the resolved
  family/face/advance is logged at Info level (`registered bundled font: …`,
  `terminal font: …`, and per-candidate `font probe '…' -> face=…`) for field
  diagnosis, and registration now uses the explicit `FcConfigGetCurrent()` and
  rebuilds fonts. A new headless `font_metrics_test` asserts the widget's cell
  metrics are positive, monospace and not full-width, exercising the real
  registration path; suites pass 21/21. A later report traced the residual
  "two spaces" to the user's font stack (FreeType 2.13.3 / Xft 2.3.6) reading
  the bundled Inconsolata-Condensed `M` advance as 17px at size 14 (≈3× the
  font's real 0.4em) — the file is byte-identical and renders at 6px elsewhere,
  so no consola change can fix it. `DejaVuSansMono.ttf` is now **embedded** in
  the binary (CMake `consola_embed_binary` generates a `std::array` header at
  configure time) and registered at startup as an always-available fallback
  (`register_application_fonts()` in `include/ui/fonts.hpp`; the bytes are
  written to a size-named temp file and added to fontconfig). The preferred
  font remains Inconsolata Condensed; the terminal's fallback chain reaches the
  embedded DejaVu Sans Mono when the stack cannot render the preferred font.
  The DejaVu license is installed under `licenses/dejavu`.
- **S5-CS1 — configuration model and schema (§5.1, §5.2)**: `include/config/config.hpp`
  defines the strongly typed model (`GeneralConfig`, `FontConfig`,
  `ColourConfig`/`ColourOverrides`, `CursorConfig`, `ProfileConfig`,
  `ShortcutConfig`, `ApplicationConfig`) with all defaults centralized
  (`kDefault*`, DIP 5.5). Raw YAML never crosses this boundary. Watchlist #19 is
  addressed: the schema now covers TERM/COLORTERM, login shell, word characters,
  the cancel-paste shortcut, bell mode and cursor shape/blink, and precedence is
  defined — defaults → `general`/`font`/`colours` → the selected profile, where
  a profile stores only the per-pane keys it overrides (shell, arguments, working
  directory, login shell, TERM, COLORTERM, scrollback, font, word characters,
  bell, cursor, colours). `ProfileConfig` uses optionals so a partial palette
  override is lossless; `apply_colour_overrides()` overlays present entries.
  `src/config/config.cpp` adds `default_application_config()` and the limit
  validators the loader will reuse (outgoing ≥ 32 KiB, paste > 0, font size
  1–512, scrollback ≥ 0, wheel lines > 0). `docs/configuration.md` is the single
  documented schema (version 1) and `resources/consola.yml` lists every key at
  its default. `Rgb` gained a defaulted `operator==` so the model tests can
  assert the config defaults match `TerminalStyle` and do not drift. New
  `config_model_test` covers defaults, validation boundaries, the style/config
  consistency and colour-override merging; suites pass 22/22. The remaining
  Stage 5 work — loader, writer, layout persistence and wiring — is tracked in
  the change sets below. **Luna-Six review: PASS WITH FINDINGS**, resolved here:
  (a) a profile's cursor *shape* (`cursor`) and cursor *colour* collided in the
  flat profile syntax — colours now nest under `colours:` in profiles, matching
  the top-level shape, and the doc/example were updated; (b) `LayoutConfig` was
  missing from the 5.1 model — added as data-only types in
  `include/config/layout_config.hpp` (layout-local pane ids, split nodes with
  child indices and ratios, per-tab focus), with persistence/validation still
  deferred to 5.6; (c) limit validators accepted `SIZE_MAX` — added upper bounds
  (`kMaxOutgoingLimitBytes` 64 MiB, `kMaxMaxPasteBytes` 256 MiB); (d) the
  `default_profile` invariant is now explicit (it must name an existing profile,
  documented and enforced by the loader); (e) `valid_kill_timeout()` was added
  (positive, ≤ 10 min) so the loader does not duplicate the check.
- **S5-CS2 — configuration loader (§5.3)**: `include/config/loader.hpp` exposes
  `load_config(path)` / `parse_config(yaml)` / `default_config_path()`, all
  returning a `ConfigLoadResult{config, warnings, error}`. Behavior matches the
  DIP: a missing file (and an empty or comment-only document) keeps the
  defaults; malformed YAML and any value error set `error` and return the
  defaults, so no partial data is applied. rapidyaml is used exception-enabled
  (0.2); the parse is wrapped in `catch (ryml::ExceptionBasic)` so a conversion
  or syntax error becomes a `ConfigLoadResult::error` instead of an abort — the
  same recoverable behavior the system-dependency mode needs. Every string is
  copied out of the YAML arena into `std::string` (a test parses from a buffer
  that is destroyed before the values are read). Unknown keys are collected as
  warnings; `version` is required and must equal `kConfigVersion`; the
  `default_profile` must name a profile that exists (the built-in `default`
  profile is always materialized). Colours accept `#rrggbb` (optional `#`) with
  the 16 ANSI names + `bright_*`; all limit/scalar validators from S5-CS1 are
  reused with key-qualified messages. `ryml` stays private to `terminal_core`.
  New `config_loader_test` covers empty/missing/valid/full configuration,
  malformed YAML, a conversion error, unknown keys, invalid values, version
  handling, the default-profile invariant, root-must-be-a-map, string lifetime,
  the default path, and a load of the shipped `resources/consola.yml`; suites
  pass 23/23. **Luna-Six review: PASS WITH FINDINGS**, resolved here: (a) an
  explicit `{}` or `[]` document bypassed validation — now only a truly empty or
  comment-only document keeps defaults, a sequence root is rejected, and an
  empty map proceeds to the required-`version` check; (b) `c4::from_chars`
  (atoi/atou) wraps on overflow — integer/size parsing now uses
  `std::from_chars`, which rejects out-of-range values (tests cover oversized
  positive values); (c) booleans are restricted to the documented `true`/`false`
  (no `yes/on/1`); string fields still accept any YAML scalar as its text, which
  is YAML semantics; (d) unknown-key coverage was extended across `font`,
  `colours`, `palette`, `cursor`, `shortcuts` and nested profile maps.
