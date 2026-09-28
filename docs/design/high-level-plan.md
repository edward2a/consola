# High-Level Implementation Plan

## Stage 0 — Skeleton
- C++20 project
- CMake
- FLTK application/window
- Basic logging/error handling
- Dependency integration:
  - FLTK
  - libtsm
  - utf8proc
  - rapidyaml
- Establish core/module boundaries
- CI: self-contained build/test targets

## Stage 1 — Terminal session
- PTY abstraction
- Linux implementation: `forkpty`/`openpty`
- Shell/process lifecycle
- Non-blocking I/O
- UI-thread ownership of terminal state: bounded SPSC queue + `Fl::awake()`
- SIGCHLD-based child exit detection (EOF is a hint only)
- Terminal resize
- Wire PTY → libtsm

**Milestone:** one working shell terminal.

## Stage 2 — Terminal widget
- Custom FLTK terminal widget
- libtsm screen rendering
- Font handling
- Colours/attributes
- Cursor
- Basic scrolling
- Scrollback (libtsm built-in, bounded)
- OSC 0/2 window title
- Wide/combining characters
- Keyboard/mouse input
- Clipboard

**Milestone:** usable standalone terminal.

## Stage 3 — Layout engine
- Split-tree data structure
- Horizontal/vertical splits
- Pane focus/navigation
- Pane resize
- Close/rebalance panes

**Milestone:** Terminator-style split terminals.

## Stage 4 — Tabs and sessions
- Multiple tabs
- Tab lifecycle
- Active pane/tab state
- New/close/move operations
- Independent terminal sessions

**Milestone:** functional multi-tab/multi-pane emulator.

## Stage 5 — Configuration
- YAML schema
- Global settings
- Profiles
- Keyboard shortcuts
- Layout persistence
- Working directory / shell settings
- Load/save configuration with rapidyaml

**Milestone:** persistent user configuration.

## Stage 6 — Terminator feature parity
Implement remaining high-value functionality:
- pane broadcasting
- zoom/font sizing
- title handling
- URL detection
- search
- alternate screen
- bracketed paste
- bells
- environment configuration
- session/layout restore

## Stage 7 — Efficiency pass
Profile before optimizing:
- PTY throughput
- libtsm processing
- screen updates
- rendering
- scrollback
- allocations
- startup time
- idle CPU
- memory per terminal

Optimize only measured bottlenecks.

## Stage 8 — Portability
Keep the core unchanged and add platform adapters:

```text
core
 ├── terminal
 ├── layout
 ├── config
 └── application

platform
 ├── Linux PTY
 ├── macOS PTY
 └── Windows ConPTY
```

FLTK remains the common UI layer.

## Dependency evolution

Start with exactly:

**C++20 + FLTK + libtsm + utf8proc + rapidyaml**

Add dependencies only when a concrete requirement cannot reasonably be met by those components or the standard library.
