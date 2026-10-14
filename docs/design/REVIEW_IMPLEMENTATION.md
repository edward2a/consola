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
- 16 Pending, 7 Reviewed, 8 Settled.
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
