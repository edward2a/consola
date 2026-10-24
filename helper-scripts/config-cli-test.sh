#!/bin/sh
# SPDX-License-Identifier: MIT
#
# End-to-end check that a --config file reaches the session. The config sets a
# non-existent shell; with the default ($SHELL, pinned to /bin/sh by the caller)
# the app would exit 0, so a non-zero exit plus the specific exec-failure
# message prove the configured value was applied. Checking the message also
# rejects a wrapper/Xvfb failure masquerading as success.
#
# Usage: config-cli-test.sh <app> <config>

set -eu

if [ "$#" -ne 2 ]; then
    printf 'usage: %s <app> <config>\n' "$0" >&2
    exit 2
fi

app=$1
config=$2
log=$(mktemp)
trap 'rm -f "$log"' EXIT

set +e
SHELL=/bin/sh CONSOLA_EXIT_AFTER_MS=500 \
    "$(dirname "$0")/run-headless.sh" "$app" --config "$config" >"$log" 2>&1
status=$?
set -e

if [ "$status" -eq 0 ]; then
    printf 'error: app exited 0; the --config shell was not applied\n' >&2
    cat "$log" >&2
    exit 1
fi

if ! grep -q 'child setup failed before exec' "$log"; then
    printf 'error: expected the configured shell to fail exec\n' >&2
    cat "$log" >&2
    exit 1
fi
