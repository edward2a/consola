# Design Review — First Pass

Reviewed: `high-level-plan.md` + `detailed-implementation-plan.md`, pre-implementation (repo contains only `docs/`).

## Overall

Solid, disciplined plan. The layering rule (`FLTK UI → Application model → Terminal core → Platform backend`) is the right call, the stage ordering is sane, and the "measure before optimizing" stance is good. The high-level and detailed docs are consistent with each other. Main gaps are around **threading/concurrency** and a few **factual issues with the chosen dependencies**.

## Significant issues

1. **Thread-safety design is missing (biggest gap).** Stage 1.4 introduces an I/O thread delivering bytes to the terminal core, and Stage 2 renders on the FLTK UI thread — but nothing specifies the synchronization contract. Who owns `TerminalEmulator` state? Does `feed()` take a lock while `draw()` reads cells? Is `Fl::lock`/`Fl::awake` used to marshal redraws? This is the #1 source of subtle bugs in terminal emulators and deserves its own subsection (e.g., "terminal state is owned by the UI thread; the reader thread only enqueues byte buffers and posts `Fl::awake`").

2. **`libtsm` realities.** It's effectively unmaintained (no releases since ~2017, C API, threading caveats documented as "not thread safe"). Two concrete consequences the plan should state:
   - **Scrollback is not provided by libtsm** — Stage 2.9 is actually a new subsystem (a line snapshot store above the screen), not a configuration knob. It's the single largest piece of Stage 2 and should be its own numbered task.
   - **Resize/reflow behavior** — whether text reflows on resize is a libtsm limitation you'll either accept or have to implement. The plan silently assumes it works.

   **Correction (second review):** libtsm (kmscon/libtsm) is active (4.8.0) and has built-in scrollback. License is MIT-like. Reflow is still missing; planned in the libtsm fork (see "Deferred Work").

   **Dependency verification follow-up:** the MIT-like description applies to
   libtsm's own code, not all bundled code. Its compiled hash table is
   LGPL-licensed. Source-only distribution now settles the current policy;
   binary compliance requires a separate decision. See
   [`../dependencies.md`](../dependencies.md#bundled-licenses-and-distribution).

3. **`ssize_t` in a "platform-neutral" interface.** `TerminalSession::read/write` (detailed plan, 1.1) return `ssize_t` — a POSIX type that contradicts "Do not expose Linux-specific types from this interface" and complicates the Windows ConPTY backend in Stage 8. Use `std::size_t` counts / a result enum instead.

4. **Child-exit detection strategy unspecified.** 1.3 lists "Detect EOF and child exit" but EOF on the master is *not* reliably equivalent to child exit (grandchildren keep the slave open — e.g., `screen`, daemons started from the shell). The plan needs a decision: SIGCHLD + `waitpid` handling, or explicit policy for the EOF-but-still-running case. Terminator-family users will hit this immediately.

## Minor issues

- **Stage 0 exit criteria says "all five selected components"** but only four dependencies are listed (FLTK, libtsm, utf8proc, rapidyaml). Off-by-one or a missing item?
- **CJK / wide-character / combining-char handling** is never mentioned. utf8proc gives you width data, but cell grid, selection, and cursor movement across double-width chars is real work — worth an explicit task in Stage 2.
- **IME / non-Latin input**: 2.6 only covers printable keys and control keys. If CJK input matters at all, FLTK's IME story should be checked before Stage 2, not after.
- **Resize ordering** (widget resize → PTY resize → emulator resize) can cause garbage render or lost SIGWINCH; one sentence on the canonical order would help.
- **Config arrives at Stage 5**, but Stages 2–4 hard-code font/colours. Fine, but a note that widget styling goes through a struct from day one would avoid refactoring.
- **CI first appears in Stage 8.5.** Even minimal CI (Linux build + CTest) from Stage 0 is cheap and matches rule 5 ("add tests before features").
- **OSC title handling** is deferred to Stage 6.1, yet the window/tab title is visible from Stage 2 usage; consider pulling the simple `OSC 0;title` case into Stage 2.

## Strengths worth keeping

- Stable `NodeId`s in the layout tree instead of pointers.
- Single factory for terminal creation (4.3) — makes lifecycle auditing tractable.
- Explicit session state machine (1.6).
- Layout engine fully testable headless, with the FLTK boundary at "calculate geometry vs. apply geometry."

## Suggested edits (summary)

- [x] Threading & synchronization section (UI-thread ownership, SPSC queue, `Fl::awake()`) — detailed plan 1.7
- [x] Scrollback via libtsm built-in, bounded — detailed plan 2.9
- [ ] Reflow — deferred; libtsm fork planned, see "Deferred Work"
- [x] `ssize_t` → portable types — detailed plan 1.1
- [x] SIGCHLD reaper + EOF policy — detailed plan 1.3/1.6
- [x] "five components" fixed — Stage 0 exit criteria
- [x] Wide/combining chars task — detailed plan 2.10
- [x] IME verification — detailed plan 2.6
- [x] Resize ordering — detailed plan 2.11
- [x] OSC 0/2 title — detailed plan 2.12
- [x] Styling-from-struct — detailed plan 2.3
- [x] CI in Stage 0 — detailed plan 0.7
- [x] Consistency touches — high-level plan

Parser-swap question resolved: keep libtsm (kmscon/libtsm active, current releases).
