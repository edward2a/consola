# consola

A lightweight, configurable terminal emulator built with C++20, CMake and FLTK.

consola is based in part on the work of the FLTK project (https://www.fltk.org).

## Building

Requirements:

- CMake 3.28 or newer
- A C++20 compiler (GCC 11+ or Clang 14+)
- X11/Xft, fontconfig and FreeType development packages (for the FLTK X11
  backend)
- On glibc older than 2.34, the separate `libutil` library is needed to link
  `openpty()` (required from the PTY stage onward)

The pinned dependencies (FLTK, libtsm, rapidyaml) are fetched and built
automatically with CMake `FetchContent`; no system copies are required. See
`docs/dependencies-dev.md` for the full development dependency list.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the application:

```sh
./build/terminal_app
```

Run the tests:

```sh
ctest --test-dir build --output-on-failure
```

Build with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-asan -DCONSOLA_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

## License

consola is licensed under the MIT License; see `LICENSE`. The license texts
and notices of statically linked dependencies are installed under
`share/doc/consola/licenses/`.

consola bundles the **Inconsolata Condensed** font by The Inconsolata Project
Authors, licensed under the SIL Open Font License 1.1. The license text is at
`resources/fonts/OFL.txt` and is installed under
`share/doc/consola/licenses/inconsolata/`.

## Documentation

- `docs/design/high-level-plan.md`
- `docs/design/detailed-implementation-plan.md`
- `docs/dependencies.md`
- `docs/dependencies-dev.md`
