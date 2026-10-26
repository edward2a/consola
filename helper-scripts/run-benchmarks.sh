#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Runs the Stage 7 benchmark suite and prints environment details followed by
# key=value results. The output is meant to be pasted into
# docs/design/PERFORMANCE.md with the date and build type.
#
# Usage: run-benchmarks.sh [bench-binary] [app-binary]

set -eu

bench=${1:-build/bench/consola_bench}
app=${2:-$(dirname "$bench")/terminal_app}

if [ ! -x "$bench" ]; then
    printf 'error: benchmark binary not found: %s\n' "$bench" >&2
    printf 'build it with: make bench\n' >&2
    exit 1
fi

if [ ! -x "$app" ]; then
    printf 'warning: app binary not found: %s (skipping startup)\n' "$app" >&2
    app=
fi

printf 'date_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf 'kernel=%s\n' "$(uname -srm)"
printf 'cpus=%s\n' "$(nproc 2>/dev/null || echo unknown)"
if [ -r /proc/cpuinfo ]; then
    model=$(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | head -n 1)
    if [ -n "$model" ]; then
        printf 'cpu=%s\n' "$model"
    fi
fi
printf 'bench_binary=%s\n' "$bench"
printf '\n'

# Each workload runs in a fresh process so RSS measurements are not polluted by
# allocations from an earlier workload. Only the redraw workload needs X.
"$bench" parser 256
"$bench" pty 64
"$bench" startup
"$bench" memory 1
"$bench" memory 10
"$bench" memory 50
"$bench" scrollback 10000
"$bench" resize 1000
"$bench" idle 2 1
"$bench" idle 2 10
"$(dirname "$0")/run-headless.sh" "$bench" redraw 200

if [ -n "$app" ]; then
    # The smoke test exits after its fixed 100 ms timer, so this includes that
    # delay; it is a full process/window/FLTK startup measurement.
    start=$(date +%s%N)
    SHELL=/bin/sh "$(dirname "$0")/run-headless.sh" "$app" --smoke-test >/dev/null 2>&1
    end=$(date +%s%N)
    printf 'app_smoke_startup_ms=%s\n' "$(((end - start) / 1000000))"
fi
