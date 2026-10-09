#!/bin/sh
# SPDX-License-Identifier: MIT

set -eu

for preset in debug release asan tsan; do
    printf '=== configure %s ===\n' "$preset"
    cmake --preset "$preset"
    printf '=== build %s ===\n' "$preset"
    cmake --build --preset "$preset"
    printf '=== test %s ===\n' "$preset"
    ctest --preset "$preset"
done
