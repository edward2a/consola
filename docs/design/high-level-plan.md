# High-Level Implementation Plan

v1 on Linux uses FLTK's X11 backend only, without Cairo/Pango. Wayland
desktops require XWayland; native Wayland support is wishlist-only.

Distribution is source-only for now, with static linking for local builds.
Binary releases/packaging require separate approval and licensing compliance.
Implementation requires all reviews to be settled and explicit user
authorization.

## Stage 0 — Skeleton
- C++20 project
- CMake
- FLTK application/window
- Basic logging/error handling
- Dependency integration:
  - FLTK
  - libtsm
  - rapidyaml
  - fetched + pinned (FetchContent), built from source, static
- Establish core/module boundaries
- CI: self-contained build/test targets

## Stage 1 — Terminal session
- PTY abstraction
- Linux implementation: `openpty` + `fork`; audited post-fork child setup
- Shell/process lifecycle; asynchronous teardown, configurable close-output policy
- Single startup worker; prepared argv/environment, cancellable exec handshake
- Non-blocking I/O
- Bounded outgoing data; one streamed paste per session, configurable size limits
- UI-thread ownership of terminal state
- Single poll I/O thread (pidfd/wake FD); per-session queues both directions;
  injected wake callback
- Budgeted round-robin I/O batches; blocking poll when idle, no periodic timer
- SIGCHLD-based child exit detection (EOF is a hint only)
- Terminal resize
- Wire PTY → libtsm

**Milestone:** one working shell terminal.

## Stage 2 — Terminal widget
- Custom FLTK terminal widget
- libtsm callback rendering; content ages plus explicit pixel damage
- Font handling
- Colours/attributes
- Cursor
- Basic scrolling
- Scrollback (libtsm built-in, bounded); Shift+PageUp, wheel routing, scrollbar
- OSC 0/2 window title
- Wide characters (combining characters and reflow: libtsm fork, deferred)
- Keyboard/mouse input via libtsm encoders (keysyms, mouse modes)
- Clipboard; bracketed paste via libtsm; alternate screen

**Milestone:** usable standalone terminal.

## Stage 3 — Layout engine
- Single-tab session owner; shared asynchronous terminal factory and cleanup
- Independent PTYs per pane; window shutdown waits for lifecycle barriers
- Split-tree data structure
- Horizontal/vertical splits
- Pane focus/navigation
- Pane resize
- Pane/subtree minimums; reject splits that cannot fit, clip forced undersizing
- Close/rebalance panes

**Milestone:** Terminator-style split terminals.

## Stage 4 — Tabs and sessions
- Multiple tabs
- Tab lifecycle
- Active pane/tab state
- New/close/move operations
- Reuse Stage 3's independent sessions, factory and cleanup across tabs

**Milestone:** functional multi-tab/multi-pane emulator.

## Stage 5 — Configuration
- YAML schema
- Global settings
- Named profile schema, validation and round trips
- Keyboard shortcuts
- Layout capture and data round trips, without runtime reconstruction
- Working directory / shell settings
- Load/save configuration with rapidyaml
- Apply global/default settings and shortcuts to the existing application

**Milestone:** persistent user configuration.

## Stage 6 — Terminator feature parity
Implement selected high-value functionality; wishlist features are not required:
- pane broadcasting
- zoom/font sizing
- title handling
- URL detection
- bells (audible/visual)
- environment configuration
- Per-pane runtime profile selection
- Rebuild live tabs/sessions from saved layout data; visible launch fallbacks

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

## Wishlist

Search and native Wayland support are outside the current implementation
stages. Optional ideas are
tracked in [`WISHLIST.md`](WISHLIST.md).

## Dependency evolution

Start with exactly:

**C++20 + FLTK + libtsm + rapidyaml**

Add dependencies only when a concrete requirement cannot reasonably be met by those components or the standard library.
