# Dependencies

Build: dependencies are fetched by CMake `FetchContent`, pinned to a git
commit hash, built from source and linked statically by default. The pins and
build options live in `cmake/deps.cmake` and `cmake/libtsm.cmake`.

The integration is implemented and verified for the fetched build: FLTK and
rapidyaml via `FetchContent_MakeAvailable`, and libtsm built from source
without Meson. The Stage 0 build was configured, compiled and smoke-tested
with these pins; the `CONSOLA_USE_SYSTEM_DEPS` mode is fail-closed and less
exercised. Exact pins are recorded below and in `cmake/deps.cmake`.

Distribution is **source-only for now**. Users build locally; distributing
executables, compiled libraries or installers is outside the current scope
and requires a separate compliance decision. This does not authorize binary
distribution.

## Table

| Dep | Purpose | First used | Pin | License | Build | Link | System mode |
|---|---|---|---|---|---|---|---|
| FLTK | GUI toolkit | Stage 0 | 1.4.5 (exact hash below) | LGPL-2 with FLTK exceptions | `FetchContent_MakeAvailable`; core target `fltk::fltk`; options below | static | `find_package(FLTK 1.4.5 CONFIG REQUIRED)` |
| libtsm | VT state machine, scrollback, selection, key/mouse encoding | Stage 1 | 4.8.0 (exact hash below) | MIT core + LGPL hash table; bundled notices below | fetch sources; compiled by `cmake/libtsm.cmake` (no meson) | static for local source builds | pkg-config `libtsm`, >= 4.8.0 |
| rapidyaml (+ bundled c4core) | YAML config | Stage 5 | rapidyaml 0.16.0 / c4core 0.6.0 (exact hashes below) | MIT + bundled notices/provenance caveats below | `FetchContent_MakeAvailable`; standalone `ryml::ryml`; dev targets off | static for local source builds | `find_package(ryml 0.16.0 CONFIG REQUIRED)` |

Pin column shows the version target; the exact hashes are recorded in
`cmake/deps.cmake`.

## FLTK pin and build

Selected release: **1.4.5**, tag `release-1.4.5`, published April 25, 2026.
Repository: `https://github.com/fltk/fltk.git`.
Commit: `a9b1113516ffd15fc7602a6d425a317df30f4720` (the commit behind the
annotated release tag, not the tag object's hash).

Read-only upstream verification on September 30, 2026:

- FLTK requires CMake >= 3.15 and documents `FetchContent_MakeAvailable`
  consumption. Link the static core target `fltk::fltk`; use CONFIG mode for
  system packages, not CMake's legacy `FindFLTK` module. Upstream's package
  version check accepts compatible 1.4.x releases >= 1.4.5.
- OFF options used: `FLTK_BUILD_SHARED_LIBS`, `FLTK_BUILD_TEST`,
  `FLTK_BUILD_EXAMPLES`, `FLTK_BUILD_FLUID`, `FLTK_BUILD_FLTK_OPTIONS`,
  `FLTK_BUILD_FORMS`, `FLTK_BUILD_GL`, `FLTK_BUILD_HTML_DOCS`,
  `FLTK_BUILD_PDF_DOCS` and `FLTK_BUILD_FLUID_DOCS`. Keep
  `FLTK_USE_PTHREADS=ON` on Linux for the `Fl::lock()`/`Fl::awake()`
  path. Generic `BUILD_SHARED_LIBS` does not control FLTK.
- the image library is still an upstream build target; it is not needed by
  the renderer and is not linked into consola. Do not describe the
  upstream build as producing only the core library.
- v1 Linux is X11-only: `FLTK_BACKEND_X11=ON`, `FLTK_USE_XFT=ON` and
  `FLTK_BACKEND_WAYLAND=OFF` are set. Also set `FLTK_USE_PANGO=OFF`,
  `FLTK_GRAPHICS_CAIRO=OFF`, `FLTK_OPTION_CAIRO_WINDOW=OFF` and
  `FLTK_OPTION_CAIRO_EXT=OFF`. Use FLTK's Xlib/Xft drawing path; no native
  Wayland, Cairo/Pango, libdecor or Wayland-specific libxkbcommon dependency
  is authorized for v1. Wayland desktops require XWayland.
- system-mode FLTK must also satisfy this X11-only, non-Cairo/Pango policy;
  a sufficient version alone is not enough. Reject incompatible provider
  features/link dependencies rather than silently accepting a hybrid build.
  Native Wayland and its dependency/license decisions are
  [wishlist-only](design/WISHLIST.md#native-wayland-support).
- `COPYING` exception 3 permits static linking without application-source or
  relinking requirements. FLTK modifications remain subject to the LGPL.
  Exception 4 still requires identifying FLTK use: include
  "consola is based in part on the work of the FLTK project
  (https://www.fltk.org)." in distributed user documentation. Install
  `COPYING` under the project's license-packaging policy as well.

Sources: [release](https://github.com/fltk/fltk/releases/tag/release-1.4.5),
[tag resolution](https://api.github.com/repos/fltk/fltk/git/tags/0d28470949323dedff75444d0011f0b5d38064f1),
[pinned source](https://github.com/fltk/fltk/tree/a9b1113516ffd15fc7602a6d425a317df30f4720)
(`CMakeLists.txt`, `CMake/options.cmake`, `src/CMakeLists.txt`,
`README.CMake.txt`, `COPYING`). The fetched FLTK build was later configured
and compiled during Stage 0 with the X11/Xft backend (no Wayland/Cairo/Pango).

## libtsm pin and build

Release: **4.8.0**, tag `v4.8.0`.
Repository: `https://github.com/kmscon/libtsm.git`.
Commit: `ef2365d0f9f1a370d721a82470d69c3646fe97f0`.
The supplied `tmp/libtsm` checkout matches this commit and has a clean worktree.
This is the planned revision. Source-only distribution settles the current
licensing-policy blocker; binary distribution is not approved.

Read-only source verification on September 30, 2026: upstream builds with
Meson and creates a shared `libtsm`. Our CMake adapter
(`cmake/libtsm.cmake`) creates a static
target instead, without running Meson or building the optional GTK test
terminal. The verified source list is:

- `src/tsm/tsm-render.c`, `tsm-screen.c`, `tsm-selection.c`,
  `tsm-unicode.c`, `tsm-vte-charsets.c`, `tsm-vte.c`
- `src/shared/shl-htable.c`
- `external/wcwidth/wcwidth.c`

Include dirs: `src/tsm`, `src/shared`, `external`, `external/wcwidth`.

Upstream flags, confined to the planned libtsm target:
`-std=gnu99 -ffast-math -fno-strict-aliasing -ffunction-sections
-fdata-sections -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L`.
Upstream force-includes its generated `config.h`; the adapter must preserve
that behavior, not merely generate the file. It contains only
`BUILD_ENABLE_DEBUG`: leave it undefined by default, not defined to 0,
because `tsm-vte.c` checks it with `#ifdef`. Upstream's symbol-version script
is for its shared library, not needed for the planned static archive.

Uses the bundled `external/xkbcommon/xkbcommon-keysyms.h`; libxkbcommon is not
needed or added to the planned link dependencies. GTK is not required for the
terminal core. No new external library or build tool is selected by this
verification.

### Bundled licenses and distribution

The earlier MIT-only description was incomplete. `COPYING` explicitly defers
to individual file notices and identifies third-party code:

| Component | Verified Notice | Packaging Requirement |
|---|---|---|
| libtsm's own code and Solarized palettes | MIT | Preserve `COPYING` and applicable file copyright/license notices |
| `src/shared/shl-htable.c` and `.h` | File headers say `LGPLv2+`; `LICENSE_htable` contains LGPL 2.1 | Preserve notices/license; keep distributed LGPL source and modifications under its applicable terms |
| Unicode helpers | MIT/X11-style notices; BSD-2-Clause conversion code identified in `COPYING` | Preserve applicable file notices and the BSD notice/disclaimer |
| Bundled wcwidth | MIT and Markus Kuhn's permissive notice | Preserve all of `external/wcwidth/LICENSE.txt` |
| Bundled keysym header | Open Group and Digital permissive notices | Preserve the header's copyright/permission/disclaimer blocks in supporting documentation |

**Current decision:** distribute consola as source only and retain static
linking for users' local builds. The hash table remains LGPL-licensed; it is
not covered by FLTK's linking exception. Preserve original licenses and
notices in any redistributed dependency source, keep LGPL-covered changes
under the applicable LGPL terms and identify modifications. Retain exact
dependency pins and build instructions so users can obtain matching source,
rebuild and replace the library. No forced relicensing of consola, hash-table
replacement or switch to dynamic linking is selected.

**Future binary distribution:** separately settle the applicable LGPL
version/terms, corresponding source, relinking materials and distribution
permissions before publishing compiled artifacts. Merely shipping notices
with a statically linked executable is not the approved compliance strategy.

Sources: [tag resolution](https://api.github.com/repos/kmscon/libtsm/git/ref/tags/v4.8.0),
[pinned source](https://github.com/kmscon/libtsm/tree/ef2365d0f9f1a370d721a82470d69c3646fe97f0)
(`meson.build`, `meson.options`, `src/tsm/meson.build`,
`src/shared/meson.build`, `external/wcwidth/meson.build`, and the notices
listed above). The Stage 0 build compiles these sources via
`cmake/libtsm.cmake`; dependency source was not modified.

The planned libtsm fork (reflow, combining chars) replaces the URL/hash only.
Search is [wishlist-only](design/WISHLIST.md); the current plan does not need
an early fork or history-access extension for it.

## rapidyaml and c4core pins and build

Selected release: **rapidyaml 0.16.0**, tag `v0.16.0`, published July 22, 2026.
Repository: `https://github.com/biojppm/rapidyaml.git`.
Commit: `f8ac8dd50f4f7916579d55a05ebf9c6488e52670`.

Bundled **c4core 0.6.0**, tag `v0.6.0`, upstream commit
`1885dca396de5aea84f176eccb92a00c8b85c5ff` in
`https://github.com/biojppm/c4core.git`. rapidyaml's `ext/c4core.mk` records
the origin tag, and its bundled `c4/version.hpp` confirms the version.
These are copied sources under `ext/c4core.src`, not a Git submodule. The
rapidyaml commit pins the actual bundled bytes; do not fetch or build a second
c4core copy or run upstream's developer synchronization scripts.

Read-only upstream verification on September 30, 2026:

- upstream requires CMake >= 3.13 and supports C++11 or later. Keep the
  application's C++20 setting; `RYML_CXX_STANDARD=20` is set as well
- `RYML_STANDALONE=ON`, `RYML_SYSTEM_C4CORE=OFF`,
  `C4_LIBRARY_TYPE=STATIC` and `BUILD_SHARED_LIBS=OFF` are set for the fetched
  build.
  Link `ryml::ryml`; standalone mode compiles the runtime c4core manifest
  directly into that target, without a separate c4core library
- `RYML_DEV`, `RYML_BUILD_TESTS`, `RYML_BUILD_BENCHMARKS`,
  `RYML_BUILD_TOOLS`, `RYML_BUILD_API`, `RYML_FUZZ_DRIVERS`,
  `RYML_EXTRA_ALL` and all `RYML_EXTRA_INTS*` options are OFF. Do not pull
  upstream's test/benchmark dependencies or language bindings into consola
- `RYML_DEFAULT_CALLBACKS=ON` and
  `RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS=ON` are set, with compiler exceptions
  enabled. Default error callbacks otherwise call `abort()`; catch ryml
  errors at the configuration loader boundary (5.3), not in terminal hot
  paths. A malformed configuration must remain a recoverable error
- system mode uses the CONFIG package and `ryml::ryml`, including any c4core
  dependency exported by that provider. Upstream uses `AnyNewerVersion`
  package compatibility. Verify the error-handling policy too; a sufficient
  version alone does not guarantee exception-enabled callbacks

### Bundled notices and source redistribution

| Component | Verified Terms / Notice Location |
|---|---|
| rapidyaml and c4core's own code | MIT; rapidyaml `LICENSE.txt` and upstream c4core `LICENSE.txt` have identical text |
| Bundled fast_float | MIT notice in `ext/c4core.src/c4/ext/fast_float_all.h` |
| Bundled debugbreak | BSD-2-Clause notice in `ext/c4core.src/c4/ext/debugbreak/debugbreak.h` |

Preserve these notices in any redistributed dependency source and under the
planned license-install policy. fast_float and debugbreak are bundled code,
not additional runtime packages or independent fetches.

If dependency source is bundled into a consola source release, notice and
provenance follow-ups remain: `c4/types.hpp` identifies a copied historical
libc++ integer-sequence implementation, but its upstream MIT/NCSA notice is
not bundled; `c4/preprocessor.hpp` and `c4/language.hpp` identify Stack
Overflow-derived snippets; the Apple debugger-detection branch of
`c4/error.cpp` traces to Apple QA1361 without establishing redistribution
permission in the reviewed evidence. These are notice/provenance questions,
not a demonstrated licensing incompatibility or a presumption that trivial
macros are copyrightable. Source-only distribution does not waive applicable
terms when third-party source is redistributed.

The planned FetchContent path obtains dependency source from upstream for
users' local builds; it does not itself bundle that source into consola's
source release. No confirmed incompatibility was found for that path. This
does not approve redistribution of a vendor-source bundle or binary release,
or authorize replacing/relicensing any bundled code.

Sources: [release](https://github.com/biojppm/rapidyaml/releases/tag/v0.16.0),
[rapidyaml tag resolution](https://api.github.com/repos/biojppm/rapidyaml/git/tags/be65769eb0a50d9a468e52b3b354b8fc4fe1152d),
[c4core tag resolution](https://api.github.com/repos/biojppm/c4core/git/tags/1563f055bc1dd7619a8d531f65c3162fd1af8ff9),
[pinned rapidyaml source](https://github.com/biojppm/rapidyaml/tree/f8ac8dd50f4f7916579d55a05ebf9c6488e52670)
(`CMakeLists.txt`, `proj/c4proj/c4Project.cmake`, `ext/README.md`,
`ext/c4core.mk`, `ext/c4core.cmake`, `ext/c4core.src.txt`,
`src/c4/yml/common.cpp`, `src/c4/yml/error.hpp`, and the notices above).
The Stage 0 fetched build configured and compiled rapidyaml; no dependency
checkout was modified.

## Always dynamic

Not built by us:

- libc, libstdc++, pthread
- X11 / Xft / fontconfig / freetype and enabled X11 extension libs required
  by FLTK's v1 X11 backend

Native Wayland dependencies and bundled libdecor are outside v1.

## Build options

| Option | Effect |
|---|---|
| `CONSOLA_USE_SYSTEM_DEPS` (default OFF) | use installed packages; min version = pinned; too old fails configure |
| `FETCHCONTENT_BASE_DIR` | shared fetch cache dir |
| `FETCHCONTENT_FULLY_DISCONNECTED=ON` | offline; reuse already fetched sources |
| `FETCHCONTENT_SOURCE_DIR_<DEP>` | use a local checkout (e.g. libtsm fork) |

## Licenses

consola's own code is licensed under the MIT License; see the root `LICENSE`.
Source files carry an `SPDX-License-Identifier: MIT` header line.

Static linking: install the license text of every static dep under
`share/doc/consola/licenses/`.

## Updating a dep

1. Bump the hash in `cmake/deps.cmake`.
2. Run all tests.
3. Update this table.

## Bundled font

consola bundles a default terminal font as a project resource.

| Resource | Source | License | Notes |
|---|---|---|---|
| Inconsolata Condensed (`resources/fonts/Inconsolata-Condensed.ttf`) | Google Fonts `ofl/inconsolata` (`https://github.com/google/fonts`) | SIL Open Font License 1.1 (`resources/fonts/OFL.txt`) | Copyright 2006 The Inconsolata Project Authors. Registered at runtime with fontconfig; installed under `share/consola/fonts/` and its license under `share/doc/consola/licenses/inconsolata/`. |

The vendored file hash is recorded here for provenance. It is a project
resource, not a `FetchContent` dependency.

- `Inconsolata-Condensed.ttf` SHA-256:
  `7081063a7270dc315d46d55564165b2e134b18f5f82e61c7b8693c819a3d4cda`
- `OFL.txt` SHA-256:
  `29bd0cfd0fb2a45f9b057c834a057724bae1f63b525a8ac83d3e7525706d9f80`

## Dropped

- utf8proc: libtsm does UTF-8 decoding and widths itself. Add back only on a
  concrete need (e.g. Unicode case folding).
