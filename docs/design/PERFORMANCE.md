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
  the redraw workload runs under Xvfb through `helper-scripts/run-headless.sh`.
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
| Startup (first terminal running) | median of 5 | 2.46 ms (min 2.37, max 4.61) |
| Full app startup (`--smoke-test`) | includes fixed 100 ms timer | 208 ms |
| Parser throughput | 256 MiB, 160x48, 1k scrollback | 60.8 MiB/s |
| PTY end-to-end throughput | 64 MiB, 160x48 | 27.5 MiB/s |
| Memory, 1 terminal | RSS delta after manager start | 372 KiB |
| Memory, 10 terminals | RSS delta after manager start | 1.59 MiB (159 KiB/terminal) |
| Memory, 50 terminals | RSS delta after manager start | 6.73 MiB (135 KiB/terminal) |
| Scrollback | 10,000 lines, 160 columns | 52.8 MiB (5.4 KiB/line) |
| Resize | emulator, 1000 alternating resizes | 4.6 us/op |
| Redraw, full | 800x480 widget, 200 frames | 3.0 ms/frame (334 fps) |
| Redraw, incremental | one line per frame, 200 frames | 0.99 ms/frame (1015 fps) |
| Idle CPU, 1 terminal | 2 s, pump every 10 ms | 1.32% |
| Idle CPU, 10 terminals | 2 s, pump every 10 ms | 1.91% |

## Observations and follow-ups

- **Idle CPU is not negligible.** About 1.3% for one idle pane is higher than a
  poll-blocked loop should need. Profile the I/O loop and UI pump wakeups
  before changing anything.
- **PTY throughput is roughly half the parser rate** (27.5 vs 60.8 MiB/s).
  The gap covers the loop read batch, the inbound queue copy, the wakeup and
  the UI pump. Measure the split before optimizing.
- **Scrollback is ~5.4 KiB per line** at 160 columns, so the default 10,000-line
  history costs ~53 MiB. Decide whether the representation or the default is
  the right lever after profiling; do not change the configured default without
  evidence.
- **Redraw cost is dominated by the per-cell text path.** A full 800x480 frame
  takes ~3 ms and an incremental frame still ~1 ms. Both `emulator_.draw()`
  passes traverse every visible cell (URL collection plus paint); incremental
  frames also scan for URLs. Profile before considering a glyph atlas.
- **Per-batch allocation.** `SessionInboundQueue::try_pop()` moves each data
  vector out of its ring slot, so the consumer frees the buffer every batch and
  the producer reallocates on the next push. This is a candidate for a
  borrow-and-pop API. It also triggers a GCC 14 `-Wnull-dereference` false
  positive in `std::vector` move assignment, which currently blocks the
  warnings-as-errors Release build.

## Optimization log

| Change set | Change | Before | After |
| --- | --- | --- | --- |
| S7-CS1 | Harness, workloads, baseline | — | see above |
