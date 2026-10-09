# Development Dependencies

System/development packages required to build and test consola from source.
This is separate from `docs/dependencies.md`, which records the pinned
third-party libraries fetched by CMake (`FetchContent`).

Status reflects the current development environment (Void Linux, GCC 14.2.1,
CMake 4.2.2, Git 2.55). Package names below are verified against the Void
repositories with `xbps-query -R`. Do not install anything automatically;
system packages are installed by the user.

## Build tools

| Capability | Package | Status |
|---|---|---|
| C/C++ compiler (C++20) | `gcc` (GCC 14.2.1) | present |
| Build system | `cmake` (>= 3.28) | present (4.2.2) |
| Generator | `make` | present |
| Fetch pinned dependencies | `git` | present (2.55) |
| pkg-config probing / system-dependency mode | `pkgconf` / `pkg-config` | present |
| Sanitizer runtime (ASan/UBSan, GCC 14) | `libsanitizer` | present |
| Sanitizer headers/link stubs (`-lasan`, `libasan_preinit.o`) | `libsanitizer-devel` | present |
| C++ formatter and linter (`clang-format`, `clang-tidy`) | `clang-tools-extra` | present |

## FLTK 1.4.5 X11/Xft backend (Stage 0)

FLTK is fetched from source and built with the X11/Xft backend only (Wayland,
Cairo and Pango disabled). These are its build-time dependencies. All are
present: a full fetched FLTK configure/build succeeded, and its configure
found ZLIB, PNG and JPEG.

| Capability | Package | Status |
|---|---|---|
| X11 client library | `libX11-devel` | present |
| Xft font drawing | `libXft-devel` | present |
| Font configuration | `fontconfig-devel` | present |
| Font rendering | `freetype-devel` | present |
| X11 extensions | `libXext-devel`, `libXrender-devel`, `libXinerama-devel`, `libXcursor-devel`, `libXfixes-devel`, `libXrandr-devel`, `libXi-devel` | present |
| XPM images | `libXpm-devel` | present |
| X authority / display | `libXau-devel`, `libXdmcp-devel` | present |
| Image library dependencies | `zlib-devel`, `libpng-devel`, `libjpeg-turbo-devel` | present |

## Headless testing / CI (Stage 0)

| Capability | Package | Status |
|---|---|---|
| Virtual X server for the window smoke test | `xorg-server-xvfb` | present |
| X authority helper for Xvfb | `xauth` | present |
| Font for FLTK/Xft window rendering | `dejavu-fonts-ttf` (any fontconfig-visible font) | present |

## Conditional / later stages

| Capability | Needed by | Package | Status |
|---|---|---|---|
| `openpty()` on glibc < 2.34 | Stage 1 PTY backend | `libutil` (part of libc on newer glibc) | not needed here (glibc 2.41); CMake probes with `check_symbol_exists(openpty)` and links `util` only when absent |
| Ninja generator | optional build speed | `ninja` | not required |

## Notes

- `pkgconf`/`pkg-config` is only needed for `CONSOLA_USE_SYSTEM_DEPS=ON` and
  for probing system packages; the default fetched build does not require it.
- Sanitizers need both `libsanitizer` (runtime `libasan.so.8`, `libubsan.so.1`)
  and `libsanitizer-devel` (the unversioned `libasan.so` symlink and
  `libasan_preinit.o` that GCC links). Both are present; the
  `CONSOLA_SANITIZERS=ON` build now links and runs.
- ThreadSanitizer (`CONSOLA_TSAN=ON`) links GCC's `libtsan` runtime, which is
  provided by the same GCC 14 package here. It is mutually exclusive with
  `CONSOLA_SANITIZERS`.
- CMake 3.28 is the minimum because `FetchContent_Declare(... EXCLUDE_FROM_ALL)`
  (used to keep dependency install rules out of consola's install) requires it.
- FLTK's Xft backend needs at least one fontconfig-visible font. A bare system
  with no fonts makes the window fail to initialize. `dejavu-fonts-ttf` is the
  development/CI choice.
- Headless tests use `helper-scripts/run-headless.sh`, which starts a private
  `Xvfb` display (no `xvfb-run` package required).
- Wayland, Cairo, Pango, libdecor, GTK and libxkbcommon are intentionally not
  required for v1; see `docs/dependencies.md`.
- The Linux PTY backend issues `pidfd_open` through the raw `SYS_pidfd_open`
  syscall rather than the glibc wrapper: on this Void toolchain the versioned
  `pidfd_open@@GLIBC_2.36` symbol exists in `libc.so.6` but is not resolved by
  the linker, so a direct libc call fails to link. The syscall path also keeps
  working on glibc < 2.36, where the loop falls back to SIGCHLD.
- `src/platform/linux` is compiled with `_GNU_SOURCE` (needed for `openpty`,
  `pipe2`, `pidfd_open`/`SYS_pidfd_open` and `environ`).
