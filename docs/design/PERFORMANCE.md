# Performance Baseline (Stage 7)

Measured on the Stage 6 code base before the efficiency changes. Keep this
document updated after each optimization change set so regressions and wins are
visible.

## Method

- Build: Release (`-O3 -DNDEBUG`), built in `build/bench` with project warnings
  as errors disabled for the benchmark tree only. `make bench` does this; the
  regular `release` preset still enables warnings-as-errors.
- Harness: `bench/bench_main.cpp`, driven by
  `helper-scripts/run-benchmarks.sh`. Each workload runs in a fresh process;
  the redraw workload and the app smoke run under one Xvfb through
  `helper-scripts/run-headless.sh`. Startup samples are warm-process (five
  managers in one process), not cold process startup.
- Workloads are deterministic. The parser/PTY stream is generated in-process
  (coloured words, cursor home, occasional clear), the full-screen workload
  feeds alternate-screen frames, and the PTY child re-executes the benchmark
  binary in `--emit` mode, so external tools do not affect the numbers. Churn
  repeatedly creates and closes terminals for leak checks.
- `make bench-run` reproduces the table. Results are machine-specific; compare
  runs on the same host and build type.

## Baseline (2026-10-26)

Environment: Linux 6.18.49_1 x86_64, 8 CPUs, AMD Ryzen AI 9 HX 370.

| Workload | Setting | Result |
| --- | --- | --- |
| Startup (first terminal running) | warm process, median of 5 | 2.50 ms (min 1.57, max 3.02) |
| Full app smoke (`--smoke-test`) | inside Xvfb, includes fixed 100 ms timer | 147 ms |
| Parser throughput | 256 MiB, 160x48, 1k scrollback | 66.0 MiB/s |
| PTY end-to-end throughput | 64 MiB, 160x48 | 26.8 MiB/s |
| Memory, 1 terminal | RSS delta after manager start | 388 KiB |
| Memory, 10 terminals | RSS delta after manager start | 1.59 MiB (159 KiB/terminal) |
| Memory, 50 terminals | RSS delta after manager start | 6.70 MiB (134 KiB/terminal) |
| Scrollback | 10,000 lines, 160 columns | 51.1 MiB (5.24 KiB/line) |
| Resize | emulator, 1000 alternating resizes | 5.2 us/op |
| Redraw, full | 800x480 widget, 200 frames | 2.73 ms/frame (366 fps) |
| Redraw, incremental | one line per frame, 200 frames | 1.33 ms/frame (750 fps) |
| Idle CPU, 1 terminal | 2 s, pump every 10 ms | 1.25% |
| Idle CPU, 10 terminals | 2 s, pump every 10 ms | 1.63% |
| Full-screen TUI (added in S7-CS5) | 2,000 frames, 80x24 alt screen | ~29-36k fps (median ~32k), 60-74 MiB/s |

Run-to-run variance is roughly 10% for throughput and redraw; compare trends,
not single samples.

## Observations and follow-ups

- **Idle CPU was driven by UI wakeups.** The initial ~1.3% came from
  `consume_inbound()` resuming every running pane on every pump; `strace` showed
  192 polls in 2 s. S7-CS3 resumes only after the UI consumed data, and a 2 s
  run now shows 9 polls. The remaining ~0.56% is the harness's 100 Hz pump; the
  real UI is event-driven.
- **PTY throughput is roughly half the parser rate** (26.4 vs 57-66 MiB/s,
  run-to-run variance). The gap covers the loop read batch, the inbound queue
  copy, the wakeup and the UI pump. The borrow-and-pop change removed the
  per-batch reallocation but not the unavoidable read-buffer copy.
- **Scrollback is ~5.2 KiB per line** at 160 columns, so the default 10,000-line
  history costs ~51 MiB. Decide whether the representation or the default is
  the right lever after profiling; do not change the configured default without
  evidence.
- **Redraw cost is dominated by the per-cell text path.** S7-CS4 replaced the
  per-cell clip with one clip per painted row, caches the font/colour state and
  removes the redundant URL `tolower` scan; a full 800x480 frame is now
  ~2.2-2.4 ms and an incremental frame ~0.9-1.0 ms. The Xlib request path still
  dominates. Both `emulator_.draw()` passes traverse every visible cell (URL
  collection plus paint); a glyph atlas remains the evidence-gated next step if
  more rendering speed is needed.
- **Per-batch allocation (fixed in S7-CS3).** `SessionInboundQueue` now exposes
  `try_peek()`/`pop()`: the consumer feeds directly from the ring slot and the
  producer reuses its capacity, so the per-batch free/reallocation and the GCC
  14 `std::vector` move-assignment false positive are gone. Production Release
  targets build clean; a few test-only `get_if`/null-check false positives from
  GCC 14 remain in the Release test build.

## Profiling (S7-CS2)

Tools: `perf record -g -e cpu-clock` (hardware counters are unavailable in this
VM) and `strace -f -c`. Samples are Release builds.

- **Parser.** libtsm dominates: `tsm_screen_write`, `parse_data`,
  `move_cursor`, `screen_erase_region`, `to_rgb`, `wcwidth`. The consola
  wrapper is negligible. A faster parser needs a libtsm fork (the deferred
  fork work); it is not a Stage 7 wrapper change.
- **PTY end-to-end.** libtsm plus ~10.6% syscalls, ~4.5% malloc and ~3.9%
  `clock_gettime` (the 1 ms batch-budget checks). The malloc is consistent with
  the inbound queue moving/freeing each batch's vector.
- **Idle CPU.** `strace` over a 2 s idle run shows 192 `poll()` calls and 196
  `clock_nanosleep` calls. The sleeps are the harness's 10 ms pump; the polls
  are the loop waking for UI commands. `TerminalManager::consume_inbound()`
  calls `loop_->resume()` for every running pane on every `pump()`, and each
  resume enqueues a command and signals the eventfd. That is the idle CPU
  source, not polling of the idle PTY. Fix: only resume when a full inbound
  queue became non-full during the pump.
- **Redraw.** The Xlib request path dominates (~35% syscall time under Xvfb).
  The remaining user-space cost is `paint_cell` (~3.4%), the URL prefix scan
  (`tolower`, ~2.9%), libtsm draw/trampoline (~4%) and FLTK/X11 mutexes
  (~2.6%). The widget traverses every visible cell twice per frame (URL collect
  plus paint). Candidate wins, in order: batch same-attribute cells into fewer
  text/clip calls, remove redundant `tolower` calls in the URL prefix scan, and
  only then consider a glyph atlas.
- **Scrollback.** ~5.2 KiB per line is libtsm's cell representation; the
  default history is the lever, not a wrapper change.

## Optimization log

| Change set | Change | Before | After |
| --- | --- | --- | --- |
| S7-CS1 | Harness, workloads, baseline | — | see above |
| S7-CS3 | Inbound borrow-and-pop; resume only after consuming; discard-mode unsuspend | idle 1.25% / 1.63%; PTY 26.8 MiB/s | idle 0.56% / 0.65%; PTY 26.4 MiB/s |
| S7-CS4 | One clip per painted row, cached font/colour, ASCII URL prefix fast path | full 2.73 ms, incremental 1.33 ms | full ~2.2-2.4 ms, incremental ~0.9-1.0 ms |
| S7-CS5 | Lifecycle churn, long-output and scrollback-bound validation | — | no leaks; bounded RSS |

## Memory validation (S7-CS5)

- **Lifecycle churn.** 100 cycles of creating and closing two terminals
  (`/bin/sleep 30`, two tabs per cycle): descriptors 7 → 7 → 7, threads
  3 → 3 → 1 (after shutdown), RSS peak +544 KiB sampled while both terminals
  are active. The workload fails on descriptor or thread growth; no leak was
  detected.
- **Long output.** The PTY workload finishes 256 MiB in 9.4 s at 27.1 MiB/s.
  The final RSS is the same after 64 MiB and 256 MiB (~32.7 MiB), so memory is
  bounded by scrollback, not by total bytes written. The residual delta is the
  emulator's retained scrollback/allocator, not growth per byte.
- **Scrollback bound.** With a 10,000-line limit, 10,000 fed lines store 9,953
  lines (the visible screen holds the rest) and the workload fails if the
  stored count exceeds the configured maximum.
- **Run-to-run.** `terminal_manager_test` exercises the close/backpressure paths
  repeatedly; the full Debug and ASan/UBSan suites pass 32/32.

## Stage 7 exit criteria

| Criterion | Status |
| --- | --- |
| Benchmark results recorded | Baseline, profiles and after-numbers in this document; `make bench-run` reproduces them |
| Major hotspots addressed | Idle wakeups, per-batch queue allocation, per-cell clips and URL `tolower` fixed; libtsm parser and glyph atlas deferred with evidence |
| Memory remains bounded | Churn leaks none; 64 MiB and 256 MiB runs end at the same RSS; scrollback bound enforced |
| Idle CPU negligible | ~0.53% / 0.63% under the harness's 100 Hz pump (9 polls/2 s); the real UI is event-driven |
| High-output workloads responsive | 27 MiB/s end-to-end PTY, 256 MiB in 9.4 s; 413 fps full redraw, 1126 fps incremental |

## Remaining follow-ups

- A glyph atlas remains the evidence-gated rendering step if the Xlib request
  path is still too costly on the target hardware.
- Parser throughput is libtsm-bound; improving it requires the deferred libtsm
  fork.
- The 10,000-line default costs ~51 MiB at 160 columns; revisit the default or
  the scrollback representation only with a product decision.
