# Design Review — Second Pass

Reviewed: `high-level-plan.md` + `detailed-implementation-plan.md` after the
first-pass fixes (see `REVIEW_FIRST.md`). Claims verified against the libtsm
4.8.0 source in `tmp/libtsm`. Some issues are in text added during the
first-pass fixes.

## Significant issues

1. **libtsm drops combining characters.** `tsm_screen_write()` returns early
   when the width is 0 (`tsm-screen.c:1107`). `tsm_symbol_append()` exists but
   nothing calls it. The 2.10 statement "rely on libtsm for combining
   characters" is therefore wrong. Either fix it in the fork (alongside
   reflow) or accept the limitation in v1.

2. **The Deferred Work reflow rules are wrong for VTE.** In the libvterm reflow
   test cases, VTE still reflows after CUP (C, E), after moving to column 1
   (F), after ICH (G, H) and after ECH (I). Only a hard newline (B) and EL (D)
   stop it. So the soft-wrap flag is set by autowrap and cleared only by EL
   or a hard line break.
   Licensing: libtsm uses an MIT-like license (not LGPL); VTE is LGPL-3. The
   fork must be written fresh, with no VTE code copied.

3. **`read()` returning `std::size_t` can't signal "no data yet" or an
   error.** 1.1 says errors are "explicit status values" but gives them
   nowhere to go. `start` and `resize` still return `bool`, which goes against
   0.3. Fix: return a small result type, e.g. `IoResult{count, status}`.

4. **1.4 contradicts 1.7.** 1.4 says the thread "delivers bytes to the
   terminal core" and "detects termination". It also doesn't say whether
   there is one I/O thread per session or one for all. Given the 50-terminal
   benchmark (7.1), one `poll` thread for all sessions is better, plus an
   eventfd/self-pipe to wake it for shutdown and writes.

5. **Many sessions share one SIGCHLD.** Needs one process-wide reaper
   (signalfd or self-pipe) that calls `waitpid` on each known pid and routes
   the result to its session. It must not use `waitpid(-1)`, which would reap
   other children.

6. **1.7 breaks architectural rule 1.** An I/O thread calling `Fl::awake()`
   makes platform/core code depend on FLTK. Fix: pass in a wake callback that
   the UI layer supplies. Also: FLTK needs `Fl::lock()` called once at
   startup, and wakeups should be combined behind an atomic flag because
   FLTK's awake queue has a fixed size.

7. **The write path is missing.** Keys, pastes and libtsm's automatic replies
   (device attributes, cursor-position reports) all go through the vte write
   callback on the UI thread. On a non-blocking FD a big paste will hit
   `EAGAIN`, so writes need an outgoing buffer that the I/O thread drains.

8. **libtsm already encodes keys.** `tsm_vte_handle_keyboard()` takes keysyms
   and handles application cursor/keypad modes. Writing our own encoder in 2.6
   duplicates that; better to map FLTK keys to keysyms and hand them to
   libtsm. This also makes most of 6.2 out of date: alternate screen,
   bracketed paste, application cursor and keypad modes already exist in
   libtsm, and vim/less need them in Stage 2 anyway.

9. **Scrollback key mapping clashes (2.9).** Plain PageUp must go to the
   running program; only Shift+PageUp should scroll. On the alternate screen,
   or when the program has turned on mouse reporting, the wheel goes to the
   program. "Locked view" is never defined. libtsm already keeps the view in
   place while scrolled back, so: stay put while scrolled, jump back to the
   live screen on a keypress.

10. **Getting the dependencies (0.2).** Distros often ship the older Aetf
    libtsm 4.0.x, which lacks `tsm_screen_draw2` and DECSCUSR (check your
    targets). rapidyaml is rarely packaged. "Prefer system packages" needs
    minimum versions and a FetchContent fallback. libtsm builds with meson, so
    consume it via pkg-config. The later fork needs this switch anyway.

11. **utf8proc has no job in the plan.** libtsm has its own UTF-8 decoder and
    wcwidth, so keeping utf8proc breaks architectural rule 3. Either give it a
    job (search normalization in 6.3, grapheme handling) or drop it until
    needed.

## Minor issues

12. 2.10 "wcwidth upstream v4" is wrong: it is the termux C port of
    jquast/wcwidth. Also, a wide character in the last column appears clipped
    rather than wrapped (`tsm-screen.c:1117-1130`, `395-400`); compare with
    xterm.
13. Use what libtsm already has: selection API for 2.7, per-cell age values
    for 2.5 dirty tracking, and truecolor. Set TERM/COLORTERM defaults in 1.2.
14. FLTK version isn't pinned; 1.4 is needed for HiDPI scaling and Wayland.
15. 4.1 says Layout owns `TerminalInstance`, but 3.1 uses IDs, and moving panes
    between tabs (6.1) needs terminals owned elsewhere. Better: Tab owns the
    terminals, layout refers to them by ID.
16. `terminate()` is undefined. Suggest: SIGHUP to the process group, then
    SIGKILL after a timeout.
17. The UI thread needs a per-tick byte limit when draining the queue, so
    input stays responsive during `cat` floods.
18. Out-of-date text: 7.3 "lock contention" (there are no locks now); 7.5
    repeats 2.5; 8.5 should point at the 0.7 targets; issue #2 in
    `REVIEW_FIRST.md` still says "unmaintained / no scrollback".

## Status

| # | Item | State |
|---|------|-------|
| 1–18 | all | open |
