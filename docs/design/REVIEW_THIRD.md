# Design Review — Third Pass

The layering and dependency restraint are sound. The main remaining risks are lifecycle and backpressure contracts, including gaps in recently closed review items.

Findings and line/section references describe the pre-fix plan. The status
table below tracks current decisions.

## Findings

1. **P1: Exit can discard unread output.** [Detailed plan, lines 328–334](detailed-implementation-plan.md#L328-L334), [412–421](detailed-implementation-plan.md#L412-L421). One FIFO orders already-enqueued messages, not bytes still in the PTY. If exit arrives while the queue is full, `exited` can trigger cleanup before the final output is read. Retain the exit status, drain buffered output, then publish completion. Define a bounded policy when descendants keep the PTY open.

2. **P1: Killing the shell's process group misses jobs.** [Lines 386–399](detailed-implementation-plan.md#L386-L399), [959–966](detailed-implementation-plan.md#L959-L966). Interactive shells put foreground jobs in separate process groups. Killing `-shell_pid` does not guarantee those jobs terminate, especially if they ignore SIGHUP. Include foreground-group handling and explicitly define whether background/detached descendants may survive.

3. **P1: Shutdown quits before termination is complete.** [Lines 398–399](detailed-implementation-plan.md#L398-L399). SIGKILL is asynchronous. "SIGKILL the rest, then quit" can stop the loop before `waitpid()` and removal acknowledgements. The timeout should enter an escalation phase; reaping and unregistering must finish before loop shutdown.

4. **P1: Fork safety is unspecified in a multithreaded application.** [Lines 273–291](detailed-implementation-plan.md#L273-L291), [297–299](detailed-implementation-plan.md#L297-L299). The I/O thread already exists when new terminals fork. Allocating, logging, or using `setenv()` in the child can deadlock on inherited locks. Prepare argv/environment before forking, restrict child setup to safe operations, and keep the blocking exec handshake off the UI thread.

5. **P1: The SIGCHLD fallback can miss fast exits.** [Lines 311–320](detailed-implementation-plan.md#L311-L320). A child can exit before its `add` command is processed; the loop may consume the signal while that PID is unknown. Check `waitpid(new_pid, WNOHANG)` during registration, not only after notifications.

6. **P2: Disabling `POLLIN` does not silence hangups.** [Lines 301–334](detailed-implementation-plan.md#L301-L334), [418–422](detailed-implementation-plan.md#L418-L422). `POLLHUP`/`POLLERR` can keep waking `poll()` even when reads are disabled, causing a busy loop during backpressure or EOF. Specify their handling, Linux PTY terminal `EIO`, and when the master FD leaves the poll set.

7. **P2: Outbound overflow has no total bound.** [Lines 434–444](detailed-implementation-plan.md#L434-L444). "Bounded by the paste size" does not cover repeated pastes, subsequent keys, or automatic replies while the child stops reading. Define a per-session pending-byte limit and overflow policy. New bytes must remain behind existing overflow.

8. **P2: Only the UI has a fairness budget.** [Lines 328–334](detailed-implementation-plan.md#L328-L334), [424–432](detailed-implementation-plan.md#L424-L432). Read/write-until-`would_block` can monopolize the shared I/O thread when producer and consumer keep pace. Budget work per session and service commands, exit notifications, and kill deadlines between batches.

9. **P2: Cell ages are not a complete repaint contract.** [Lines 567–575](detailed-implementation-plan.md#L567-L575). Exposure and local font/theme changes can require painting unchanged terminal cells. Specify full-redraw triggers separately from libtsm content ages, plus invalidation for presentation overlays.

10. **P2: Search lacks a history-access boundary.** [Lines 519–530](detailed-implementation-plan.md#L519-L530), [1150–1163](detailed-implementation-plan.md#L1150-L1163). Search must cover offscreen history, but the model exposes renderer data and scroll position only. Define read-only history access and match invalidation when scrollback is evicted.

11. **P2: Broadcast input must happen before encoding.** [Lines 1131–1139](detailed-implementation-plan.md#L1131-L1139). Panes can have different cursor-key and bracketed-paste modes. Broadcast user actions through each destination's encoder, not one pane's encoded bytes. Automatic replies must stay session-local.

## Smaller Gaps

- Stage 3 promises working split terminals, but their owner, factory, and asynchronous cleanup arrive in Stage 4.
- Layout needs minimum pane dimensions and an undersized-window policy before forwarding resize requests.
- Stage 5 already promises profiles and layout restore; Stage 6 repeats both. Separate configuration round trips from runtime reconstruction.

## Status

| Item | State |
|---|---|
| 1 | Done: 1.4 post-exit drain and pending events; 1.6 reaping vs UI completion; 1.7 ordering; Stage 1 tests 14-15 |
| 2 | Done: 1.6 shell/foreground scope, independent escalation and target identity; 1.4/4.4 removal barrier; Stage 1 tests 16-18 |
| 3 | Done: 1.6 asynchronous shutdown barriers and configurable close-output policy; 1.4 shutdown guard; 4.4/4.5 cleanup order; 5.2 option; Stage 1 tests 19-20 |
| 4 | Done: 1.1/1.3 startup worker, prepared launch data and safe child path; 1.6/1.7/4.3/4.4 ownership and cancellation; Stage 1 tests 21-22; high-level plan |
| 5 | Done: 1.4 registration-time `waitpid` probe, shared exit path and reaped-record guard; Stage 1 test 23 |
| 6 | Done: 1.1 Linux terminal `EIO`; 1.4 hangup/error draining and poll-entry retirement; 1.7 full-entry backpressure suspension; Stage 1 tests 24-25 |
| 7 | Done: 1.7 bounded transport/deferred bytes and one streamed paste; admission, cancellation and reply-overflow policies; 2.8 framing verification; 5.2 limits; Stage 1 tests 26-27 |
| 8 | Done: 1.4 round-robin read/write budgets, inter-batch control servicing and blocking idle poll; Stage 1 tests 28-29; high-level plan |
| 9 | Done: 2.2 content vs presentation damage; 2.5 forced repaint, old/new overlay damage and paint-age completion; Stage 2 repaint tests |
| 10 | Deferred: search moved to WISHLIST.md, outside the current stages; no history adapter or early fork scheduled |
| 11 | Done: 6.1 action-level broadcast, per-destination encoding/admission and local replies; 2.6/2.8 routing; Stage 6 broadcast regression tests |
| Smaller gap: Stage 3/4 ownership | Done: 3.0 schedules single-tab ownership, shared factory/cleanup and shutdown barriers before live splits; Stage 3 lifecycle tests; Stage 4 reuses them |
| Smaller gap: pane minimum dimensions | Done: 3.3 leaf/subtree minimums, split admission and clipped undersizing; 1.1/2.11 positive resize guards; Stage 2/3 regression tests |
| Smaller gap: configuration vs runtime restore | Done: Stage 5 profile/layout data validation and round trips; 6.4 per-pane runtime profiles; 6.5 asynchronous reconstruction and fallbacks; distinct regression tests |
| Drawing API follow-up | Corrected: 2.4 uses `tsm_screen_draw()` for callback widths/ages, not `draw2`; verified against supplied libtsm 4.8.0 |

## Pause State

Paused after resolving configuration round trips vs runtime reconstruction.
Items 1-9 and 11 and all three smaller gaps are documented as resolved;
item 10 is deferred to the wishlist, not implemented. No review items remain
open. No application code or dependency source has been changed;
regression tests are specifications only. No commit was made during this pass.

## Assessment

Resolve lifecycle findings before Stage 1 implementation. The overall architecture need not change.

The initial review was document-based. Follow-up read-only inspection of the
supplied libtsm 4.8.0 source verified the public history/drawing API limits;
dependency build/licensing claims were not reverified. No runtime tests were
run and no implementation exists to test yet.
