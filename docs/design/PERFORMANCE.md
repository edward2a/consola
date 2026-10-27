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
  (coloured words, cursor home, occasional clear) and the PTY child re-executes
  the benchmark binary in `--emit` mode, so external tools do not affect the
  numbers.
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
- **Redraw cost is dominated by the per-cell text path.** A full 800x480 frame
  takes ~2.8 ms and an incremental frame still ~1.3-1.6 ms. Both
  `emulator_.draw()` passes traverse every visible cell (URL collection plus
  paint); incremental frames also scan for URLs. Profile before considering a
  glyph atlas.
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
