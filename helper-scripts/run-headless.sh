#!/bin/sh
# SPDX-License-Identifier: MIT

set -eu

if [ "$#" -eq 0 ]; then
    printf 'usage: %s <command> [args...]\n' "$0" >&2
    exit 2
fi

: "${XVFB:=Xvfb}"
if ! command -v "$XVFB" >/dev/null 2>&1; then
    printf 'error: %s not found (install xorg-server-xvfb)\n' "$XVFB" >&2
    exit 127
fi

display_num=99
while [ -e "/tmp/.X${display_num}-lock" ] || [ -e "/tmp/.X11-unix/X${display_num}" ]; do
    display_num=$((display_num + 1))
done

"$XVFB" ":${display_num}" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
xvfb_pid=$!

cleanup() {
    kill "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

ready=0
i=0
while [ "$i" -lt 200 ]; do
    if [ -S "/tmp/.X11-unix/X${display_num}" ]; then
        ready=1
        break
    fi
    if ! kill -0 "$xvfb_pid" 2>/dev/null; then
        printf 'error: Xvfb exited before display :%s became ready\n' "$display_num" >&2
        exit 1
    fi
    sleep 0.05
    i=$((i + 1))
done

if [ "$ready" -ne 1 ]; then
    printf 'error: timed out waiting for Xvfb display :%s\n' "$display_num" >&2
    exit 1
fi

DISPLAY=":${display_num}" "$@"
