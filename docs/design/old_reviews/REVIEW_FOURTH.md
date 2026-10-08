I've read the two core documents, all three review passes, the dependency record, and the wishlist, and spot-checked the key libtsm claims against the supplied 4.8.0 source. Here is my independent review.

## Verdict

The design is mature and unusually disciplined for a pre-implementation plan. The three review rounds have genuinely closed the hard problems (fork safety, exit-output draining, foreground-group termination, backpressure, broadcast-before-encode). The architecture, dependency pins, and licensing record are sound. **The plan is ready to authorize Stage 0.** The remaining issues are documentation-level gaps, concentrated in Stage 2 (rendering), not architectural flaws.

## New findings (not covered by prior reviews)

**P2 — Rendering mechanism is unspecified.** Stage 2 says *what* to draw (2.4: background, cells, fg colours, attributes, cursor, scrollbar) but never *how* glyphs are drawn or how cell metrics are computed. Given the no-Cairo/Pango constraint and the fixed-cell-metrics requirement (2.3), the plan should name the FLTK text path (`fl_font()`/`fl_draw()` per cell vs. a glyph atlas) and the metric source (`fl_measure()`, `fl_height()`, ascent/descent). This is the largest under-specified area relative to Stage 1's depth, and it's the visible product.

**P3 — `TerminalEmulator` (1.5) and the "terminal model API" (2.2) are never reconciled.** 1.5 defines a core class with only `feed()`/`resize()`, while 2.2 defines a separate read API (rows/cols/cells/cursor/dirty/scroll). The plan never states whether these are one class, two views of one class, or two classes. The renderer needs read access that 1.5's interface doesn't provide. This is a structural ambiguity at the core/UI boundary.

**P3 — Cursor rendering is double-specified.** `tsm_screen_draw()` already inverts the cursor cell (tsm-render.c:133-135) unless `TSM_SCREEN_HIDE_CURSOR`. 2.4 lists "cursor" as a separate step and 2.2 exposes "cursor state", but doesn't note that the cell-level cursor arrives via the inverse attribute and only *style* (block/underline/vbar via `tsm_screen_get_cursor_style`) and blink (`tsm_screen_get_cursor_blinking_rate_ms`) need separate handling. Risk of double-drawing.

**P3 — consola's own license is unspecified.** `dependencies.md` exhaustively covers every dependency's license but never states consola's own. For a source-distributed project this should be decided before Stage 0 (it affects the license-install policy and headers).

**P3 — `resize()` thread-safety is implicit.** `TerminalSession::resize()` is called from the UI thread (2.11) while the I/O loop owns the master FD. `ioctl(TIOCSWINSZ)` is thread-safe, so this is fine, but the plan should state that resize is a direct ioctl and does not route through the loop's command queue (1.4 lists no `resize` command).

## Minor

- **SIGPIPE** is never mentioned. The Linux PTY master returns `EIO` (not SIGPIPE) on slave close, which 1.1 already maps — so the main write path is safe. But the exec error pipe (1.3 #11) and the SIGCHLD self-pipe (1.4) are real pipes, and the future ConPTY backend (Stage 8) uses pipes where SIGPIPE is a genuine crash risk. Worth one line: ignore SIGPIPE (or use `MSG_NOSIGNAL`) defensively.
- **SPSC queue message-count bound** is unspecified; only byte limits (1 MiB / 16 MiB) are given.
- **No sanitizers in CI**; 7.3 mentions ASan/UBSan only for profiling. Cheap to add to Stage 0.
- **`std::function<void()>` wake callback** in the per-wakeup hot path; a function pointer or small interface would avoid allocation. Minor.
- **libtsm log callback** (`tsm_vte_new`/`tsm_screen_new` require a `tsm_log_t`) isn't explicitly wired to the 0.4 logger.
- **No README/CONTRIBUTING**; `tsm_vte_set_led_cb` (lock indicators) unmentioned — wishlist material.

## Verified correct against source (affirming prior reviews)

- `tsm_screen_draw()` supplies per-cell `age` and `width` via callback; `tsm_screen_draw2()` returns a cell table without them — 2.4/2.5's choice is right.
- **Scrollback selection works** in libtsm: `selection_set` maps screen coords to scrollback lines via `sb.pos`, and the header comment explicitly states scrollback selection is handled. So 2.7's "use libtsm's API" is sound — no gap there.
- `tsm_vte_write_cb` (mandatory at `tsm_vte_new`), `tsm_vte_handle_keyboard`, `tsm_vte_handle_mouse`, `tsm_vte_paste`, `tsm_vte_set_bell_cb`, `tsm_vte_set_osc_cb`, `tsm_vte_set_backspace_sends_delete`, `tsm_vte_get_mouse_mode` all exist as claimed.
- Combining chars dropped at `tsm-screen.c:1107` — correctly documented as a v1 limitation.

## Recommendation

1. Decide consola's license (finding 5) before Stage 0.
2. Fold findings 1–4 and the minor items into a `REVIEW_FOURTH.md` (or amend the plan) before Stage 2; none block Stage 0.
3. Authorize **Stage 0 only** — skeleton, pinned dependency integration, smoke test, thin CI — and stop at its exit criteria.

Want me to draft the `REVIEW_FOURTH.md` content (as a plan, not yet written) so you can review the exact wording before I apply it?
