# Dependencies

All deps are fetched by the build (CMake `FetchContent`), pinned to a git
commit hash, built from source and linked statically by default. Pins live in
`cmake/deps.cmake`.

## Table

| Dep | Purpose | First used | Pin | License | Build | Link | System mode |
|---|---|---|---|---|---|---|---|
| FLTK | GUI toolkit | Stage 0 | 1.4.x (TBD at Stage 0) | LGPL-2 with static-linking exception | `FetchContent_MakeAvailable`; shared libs, tests, examples off | static | `find_package(FLTK)`, >= pinned |
| libtsm | VT state machine, scrollback, selection, key/mouse encoding | Stage 1 | 4.8.0 | MIT | fetch sources; compiled by `cmake/libtsm.cmake` (no meson) | static | pkg-config `libtsm`, >= pinned |
| rapidyaml (+ c4core) | YAML config | Stage 5 | TBD at Stage 0 | MIT | `FetchContent_MakeAvailable`; tests off | static | `find_package(ryml)`, >= pinned |

Pin column shows the tag; the exact commit hash is in `cmake/deps.cmake`.

## libtsm build

Upstream builds with meson only. `cmake/libtsm.cmake` compiles:

- `src/tsm/tsm-render.c`, `tsm-screen.c`, `tsm-selection.c`,
  `tsm-unicode.c`, `tsm-vte-charsets.c`, `tsm-vte.c`
- `src/shared/shl-htable.c`
- `external/wcwidth/wcwidth.c`

Include dirs: `src/tsm`, `src/shared`, `external`, `external/wcwidth`.

Flags (libtsm target only): `-std=gnu99 -ffast-math -fno-strict-aliasing
-D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L`, plus a generated `config.h`
(only `BUILD_ENABLE_DEBUG`).

Uses the bundled `external/xkbcommon/xkbcommon-keysyms.h`; libxkbcommon is not
needed.

The planned libtsm fork (reflow, combining chars) replaces the URL/hash only.

## Always dynamic

Not built by us:

- libc, libstdc++, pthread
- X11 / Wayland / fontconfig / freetype libs required by FLTK

## Build options

| Option | Effect |
|---|---|
| `CONSOLA_USE_SYSTEM_DEPS` (default OFF) | use installed packages; min version = pinned; too old fails configure |
| `FETCHCONTENT_BASE_DIR` | shared fetch cache dir |
| `FETCHCONTENT_FULLY_DISCONNECTED=ON` | offline; reuse already fetched sources |
| `FETCHCONTENT_SOURCE_DIR_<DEP>` | use a local checkout (e.g. libtsm fork) |

## Licenses

Static linking: install the license text of every static dep under
`share/doc/consola/licenses/`.

## Updating a dep

1. Bump the hash in `cmake/deps.cmake`.
2. Run all tests.
3. Update this table.

## Dropped

- utf8proc: libtsm does UTF-8 decoding and widths itself. Add back only on a
  concrete need (e.g. Unicode case folding).
