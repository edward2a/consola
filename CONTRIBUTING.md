# Contributing to consola

Thanks for your interest in consola. This document describes the build, coding
rules and workflow expected for contributions.

## Building and testing

See `README.md` for build instructions. In short:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The pinned dependencies are fetched automatically; the required system
packages are listed in `docs/dependencies-dev.md`.

## Coding rules

These rules are established by Stage 0 of the implementation plan.

### Naming

- Types (classes, structs, enums, aliases): `PascalCase`.
- Functions and methods: `snake_case`.
- Local variables and parameters: `snake_case`.
- Private and protected data members: `snake_case` with a trailing underscore
  (`count_`).
- Public data members of plain structs: `snake_case`, no suffix.
- Enumerators: `PascalCase` (for example `LogLevel::Warning`).
- Namespaces: lowercase (`consola`).
- Files: `snake_case` (`log.cpp`, `application.hpp`).
- CMake targets: `snake_case`, component targets prefixed `terminal_`.
- Macros: `CONSOLA_` prefix, `UPPER_CASE`.

### Ownership

- Prefer value semantics and RAII.
- No naked `new`/`delete`. Use `std::unique_ptr` for owned heap objects;
  use `std::shared_ptr` only when shared ownership is genuinely required.
- Wrap C resources (for example libtsm handles) in RAII types.
- Avoid unnecessary heap allocation in terminal hot paths.
- Keep OS-specific process/PTY code behind the `terminal_platform` boundary.

### Includes

Order include blocks as follows, separated by blank lines:

1. The corresponding header (in a `.cpp` file), if any.
2. C++ standard library headers.
3. Third-party library headers.
4. Project headers.

Use angle brackets and include paths relative to `include/` (for example
`<app/log.hpp>`). Do not use `../` relative includes.

### Formatting

- Format C++ with `clang-format` using the repository `.clang-format`
  (4-space indent, 100-column limit).
- Run `clang-tidy` using the repository `.clang-tidy`; keep project code free
  of its warnings.
- Format shell scripts with `shfmt` where available.

### Warnings

- Project code is built with `-Wall -Wextra -Wpedantic` and the additional
  warnings set in `CMakeLists.txt`.
- Keep the build warning-free; the CI configuration builds with
  `CONSOLA_WARNINGS_AS_ERRORS=ON`.
- Third-party headers are included as `SYSTEM` and are not subject to the
  project warning policy.

### Compiler and tool versions

- C++20; GCC 11+ or Clang 14+.
- CMake 3.28 or newer.

## Tests

- Every non-trivial change adds or updates a CTest test.
- Tests must not require manual input or a physical display. GUI tests run
  headless through `helper-scripts/run-headless.sh`, which starts a private
  `Xvfb` display.
- Run the test suite in a normal build and, for changes touching memory or
  threading, in a `CONSOLA_SANITIZERS=ON` build.

## Workflow

1. Create a topic branch from the current development branch.
2. Keep changes focused and commit in reviewable units.
3. Ensure the build and `ctest` pass in Debug and sanitizer configurations.
4. Keep `docs/` up to date when behaviour, dependencies or prerequisites
   change.
5. Do not add a dependency without a concrete requirement; see
   `docs/dependencies.md` for the dependency policy.

## License

By contributing, you agree that your contributions are licensed under the MIT
License (see `LICENSE`). Source files carry an
`SPDX-License-Identifier: MIT` line.
