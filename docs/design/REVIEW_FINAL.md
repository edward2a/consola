# Design Review — Final Pass

Reviewed: `high-level-plan.md` + `detailed-implementation-plan.md`, the four
prior reviews, `docs/dependencies.md` and `WISHLIST.md`, with read-only
spot-checks against the supplied libtsm 4.8.0 source in `tmp/libtsm`. This is
the final review pass. Claims verified against source are marked; dependency
pins and FLTK build behavior were not re-verified.

## Verdict

The plan is mature and internally consistent. The prior four rounds genuinely
closed the hard problems (fork safety, exit-output draining, foreground-group
termination, backpressure, broadcast-before-encode, and the rendering/licensing/
threading items from the fourth round). All fourth-round items are present in
commit `c5d61d0`. **No architectural blockers remain; Stage 0 is safe to
authorize.** The findings below are Stage 2/Stage 5 precision issues.

## New findings (not covered by prior reviews)

**P3 — Cursor styles conflict with libtsm's unconditional inversion.**
`tsm_screen_draw()` flips `attr.inverse` for the cursor cell for *every* cursor
style (`tsm-render.c:133-135`), not just block. 2.4 says only block uses the
inverse cell and underline/vbar add an overlay. As written, underline/vbar
render an inverted block plus the overlay, and the overlay (drawn from
`get_cursor_x/y`) is even drawn when the cursor is scrolled off-screen. Fix:
for non-block styles, undo the callback's inverse on the cursor cell before
drawing and gate the overlay on viewport visibility; or hide libtsm's cursor
(`TSM_SCREEN_HIDE_CURSOR`) and draw all styles ourselves.

**P3 — `Cell` is a symbol, and `len == 0` is the common case.**
The draw callback passes `const uint32_t *ch, size_t len` — a multi-codepoint
symbol (`libtsm.h:211-220`, `tsm-render.c:172-176`). 2.2 defines `Cell` as a
single "codepoint". Blank/space cells and the trailing half of wide cells
arrive with `len == 0` (`tsm-render.c:173-174`; width-0 trailing cells in
`tsm-screen.c:396-400`); 2.4 does not say to skip them. Once the fork adds
combining marks, `len > 1` becomes real. Model `Cell` as a short symbol plus
width/age/attr.

**P3 — Styling/colour config never reaches libtsm.**
2.4 is right that the callback supplies final RGB (`to_rgb()`,
`tsm-vte.c:447-467`), but the configurable fg/bg/16-colour values (5.2, 6.4)
must be pushed into libtsm via `tsm_vte_set_custom_palette()` (or
`tsm_vte_set_palette()`). Neither 2.3 nor 6.4 mentions it, so profile colours
would have no effect.

## Minor

- **`tsm_vte_paste` semantics.** It takes a NUL-terminated `const char*`, does
  its own `malloc(strlen(data)+framing)`, and silently returns on OOM
  (`tsm-vte.c:3523`). 1.7's capture/reject policy should note the transient
  allocation, embedded-NUL truncation, and the silent-drop path (do not assume
  the capture buffer is non-empty).
- **Child signal reset must name SIGPIPE/SIGCHLD.** The app sets
  `SIGPIPE=SIG_IGN` (1.3), and `SIG_IGN` survives `execve`. 1.3's "restore
  child signal dispositions and mask" should name them explicitly; otherwise
  shell pipelines (e.g. `yes | head`) can hang.
- **`remove`/FD handoff wording.** 1.1 says "session removal run on the UI
  thread", but the loop owns the master FD. Clarify that the loop removes the
  poll entry and finishes in-flight I/O before acking `remove`, and the UI
  closes the FD only after the ack.
- **2.2 read-surface omissions.** 2.9 promises "scroll position and line count"
  and 2.12 promises the title "through the terminal model API (2.2)", but 2.2
  lists neither. Add them.
- **Rendering details.** `x = col * cell_w` drifts if `fl_width("M")` is
  fractional — round per cell. Per-cell `fl_draw()` without clipping lets
  glyphs bleed; clip to the cell.
- **Verified external claim.** 5.3 names rapidyaml's base exception
  `ExceptionBasic`; confirmed against 0.16.0 at the pinned commit: `error.hpp`
  defines `struct ExceptionBasic : public std::exception`, with `ExceptionParse`
  and `ExceptionVisit` derived from it, so catching `ExceptionBasic` covers all
  three. No plan change required.
- **Test gaps for the last round.** No test covers the 0.4 `tsm_log_t` severity
  mapping or the coalesced wake callback.

## Verified correct against source (affirming prior reviews)

- Every cited libtsm API exists with the stated shape: `tsm_screen_draw`/
  `draw2` (draw2 lacks age/width, and also drops inverse/dim and
  multi-codepoint symbols), cursor style/blink/hide, `sb_*`, selection,
  `tsm_vte_paste`/`handle_keyboard`/`handle_mouse`/`get_mouse_mode`/
  `set_bell_cb`/`set_osc_cb`/`set_led_cb`/`set_backspace_sends_delete`.
- UTF-8 decoder is stateful across `tsm_vte_input` calls (`vte->mach`), so
  sequences split across reads are safe (`tsm-vte.c:2771`).
- Zero-width drop at `tsm-screen.c:1104-1107`; DECSET 1007 genuinely unparsed.
- `tsm_log_t` severity mapping matches `shl-llog.h` (0-3 err, 4 warn, 5-6 info,
  7 debug); default blink rate 500 ms.
- `dependencies.md` libtsm source list matches `meson.build`; the `config.h`
  force-include requirement and `BUILD_ENABLE_DEBUG` `#ifdef`
  (`tsm-vte.c:738`) are correct.
- `docs/dependencies.md`, `high-level-plan.md`, `LICENSE`, `WISHLIST.md`
  (including Lock Indicators) and Stage 0.8 are consistent.

## Recommendation

1. Fold the findings above into the plan one at a time, as with prior rounds.
2. None block Stage 0.
3. Authorize **Stage 0 only** after the findings are settled.

## Status

| # | Item | Disposition |
|---|---|---|
| 1 | Cursor styles vs unconditional inversion | Done: 2.4 renderer-owned cursor; Stage 2 cursor-style test |
| 2 | `Cell` symbol and `len`/`width` zero cases | Done: 2.2 `Cell` + rules; 2.4 traversal |
| 3 | Colour config never reaches libtsm | Done: 2.3 palette wiring; 5.1/5.2 schema; 6.4 |
| 4 | `tsm_vte_paste` semantics | Done: 1.7 capture/allocation/NUL/empty-capture |
| 5 | SIGPIPE/SIGCHLD reset in child | Done: 1.3 step 4 and SIGPIPE note |
| 6 | `remove` / FD-close handoff | Done: 1.1 and 1.4 |
| 7 | 2.2 read-surface omissions | Done: 2.2 line count and title |
| 8 | Cell origin rounding and glyph clipping | Done: 2.4 |
| 9 | rapidyaml `ExceptionBasic` name | Verified correct against 0.16.0; no change |
| 10 | Tests for log mapping and wake coalescing | Done: Stage 0 and Stage 1 test lists |

All 10 items settled. No application code or dependency source was changed;
tests remain specifications. No architectural blocker was found.

Scope note: no build or runtime test was run, and no dependency source was
modified.
