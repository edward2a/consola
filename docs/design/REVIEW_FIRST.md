# Design Review — First Pass

Reviewed: `high-level-plan.md` + `detailed-implementation-plan.md`, pre-implementation (repo contains only `docs/`).

## Overall

Solid, disciplined plan. The layering rule (`FLTK UI → Application model → Terminal core → Platform backend`) is the right call, the stage ordering is sane, and the "measure before optimizing" stance is good. The high-level and detailed docs are consistent with each other. Main gaps are around **threading/concurrency** and a few **factual issues with the chosen dependencies**.

## Significant issues

1. **Thread-safety design is missing (biggest gap).** Stage 1.4 introduces an I/O thread delivering bytes to the terminal core, and Stage 2 renders on the FLTK UI thread — but nothing specifies the synchronization contract. Who owns `TerminalEmulator` state? Does `feed()` take a lock while `draw()` reads cells? Is `Fl::lock`/`Fl::awake` used to marshal redraws? This is the #1 source of subtle bugs in terminal emulators and deserves its own subsection (e.g., "terminal state is owned by the UI thread; the reader thread only enqueues byte buffers and posts `Fl::awake`").

2. **`libtsm` realities.** It's effectively unmaintained (no releases since ~2017, C API, threading caveats documented as "not thread safe"). Two concrete consequences the plan should state:
   - **Scrollback is not provided by libtsm** — Stage 2.9 is actually a new subsystem (a line snapshot store above the screen), not a configuration knob. It's the single largest piece of Stage 2 and should be its own numbered task.
   - **Resize/reflow behavior** — whether text reflows on resize is a libtsm limitation you'll either accept or have to implement. The plan silently assumes it works.

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

1. Add a "Threading & synchronization" section to Stage 1/2 specifying state ownership and the wakeup mechanism.
2. Split scrollback out as its own Stage 2 task; document the libtsm no-scrollback/reflow caveat.
3. Replace `ssize_t` in `TerminalSession` with portable types.
4. Specify the child-exit detection mechanism (SIGCHLD reaper) and the EOF-vs-exit policy.
5. Fix the "five components" inconsistency and add wide-char + IME tasks to Stage 2.
