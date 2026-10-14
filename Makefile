# consola build wrapper: `make` equivalents without touching cmake directly.
# Targets: help (default), build, test, release, asan, tsan, run, clean.
#
# The configurations live in CMakePresets.json, so these targets stay in sync
# with CI (warnings-as-errors and the sanitizer builds).

JOBS ?= $(shell nproc 2>/dev/null || echo 4)

.DEFAULT_GOAL := help

.PHONY: help build test release asan tsan run clean

help: ## Show this help.
	@echo "Usage: make [JOBS=N] <target>"
	@echo "Targets:"
	@echo "  build    Configure and build the Debug tree (warnings-as-errors)."
	@echo "  test     Build, then run the test suite."
	@echo "  release  Build an optimized Release tree."
	@echo "  asan     Build and test with AddressSanitizer + UndefinedBehaviorSanitizer."
	@echo "  tsan     Build and test with ThreadSanitizer."
	@echo "  run      Build and run the terminal (needs a display)."
	@echo "  clean    Remove the CMake build trees."

build: ## Configure and build the Debug tree.
	cmake --preset debug
	cmake --build --preset debug -j$(JOBS)

test: build ## Build, then run the test suite.
	ctest --preset debug

release: ## Build an optimized Release tree.
	cmake --preset release
	cmake --build --preset release -j$(JOBS)

asan: ## Build and test with AddressSanitizer + UndefinedBehaviorSanitizer.
	cmake --preset asan
	cmake --build --preset asan -j$(JOBS)
	ctest --preset asan

tsan: ## Build and test with ThreadSanitizer.
	cmake --preset tsan
	cmake --build --preset tsan -j$(JOBS)
	ctest --preset tsan

run: build ## Build and run the terminal (needs a display).
	./build/debug/terminal_app

clean: ## Remove the CMake build trees.
	rm -rf build/debug build/release build/asan build/tsan
