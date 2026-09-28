# Detailed Implementation Plan

## Project constraints

### Fixed technology choices

- Language: **C++20**
- Build system: **CMake**
- GUI: **FLTK**
- Terminal state / VT handling: **libtsm**
- Unicode processing: **utf8proc**
- Configuration: **YAML via rapidyaml**
- Initial target: **Linux**
- Portability target: **macOS + Windows**
- Primary objectives:
  1. Functionality
  2. Efficiency
  3. Lightweight footprint
  4. Portability

### Architectural rules

1. Keep the terminal core independent of FLTK.
2. Keep OS-specific process/PTY code behind a narrow platform interface.
3. Do not add dependencies without a concrete requirement.
4. Keep ownership explicit and avoid unnecessary heap allocation in terminal hot paths.
5. Add automated tests before implementing complex features.
6. Profile before making performance-driven structural changes.

---

# Stage 0 — Skeleton

## Objective

Create a buildable application with all selected dependencies integrated and a stable project structure.

## Repository structure

Create:

```text
/
├── CMakeLists.txt
├── cmake/
├── include/
│   └── app/
├── src/
│   ├── app/
│   ├── platform/
│   ├── terminal/
│   ├── layout/
│   ├── config/
│   └── ui/
├── tests/
├── resources/
└── docs/
```

Use public headers under `include/` and implementation files under `src/`.

## Tasks

### 0.1 Initialize CMake

Configure:

- `cmake_minimum_required`
- project version
- C++20 requirement
- Debug and Release configurations
- warnings enabled for project code
- testing with CTest

Use targets rather than global compiler/linker flags.

Create at minimum:

```text
terminal_app
terminal_core
terminal_platform
terminal_ui
```

Keep the targets logically separated even if some are initially thin wrappers.

### 0.2 Integrate dependencies

Make the build consume:

- FLTK
- libtsm
- utf8proc
- rapidyaml

Prefer system packages during initial development.

Do not vendor dependencies yet unless portability/build reproducibility later requires it.

### 0.3 Establish error handling

Use exceptions only at application/setup boundaries.

Within core runtime code:

- return explicit status/result values where practical
- treat PTY/process failures as recoverable session errors
- keep fatal startup errors visible and actionable

### 0.4 Establish logging

Create a tiny internal logging abstraction:

```cpp
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

void log(LogLevel, std::string_view);
```

Do not add a logging framework yet.

### 0.5 Create the application shell

Implement:

- `main()`
- FLTK application/window
- clean shutdown
- placeholder central widget
- application event loop

### 0.6 Establish coding rules

Define:

- naming conventions
- ownership conventions
- include ordering
- formatting
- warning policy
- compiler versions supported

Add `.clang-format` and, if desired, `.clang-tidy`.

### 0.7 CI

All CI is self-contained: platform integrations are thin wrappers that call
Make/CMake targets. The repository itself must provide runnable entry points,
for example:

- `cmake` configure + build (Debug and Release)
- `ctest` (all tests)

A CI platform (e.g. GitHub Actions) runs only these targets on Linux at this
stage. The full platform matrix stays in Stage 8.5.

## Tests

Add the first smoke test:

- executable starts
- executable exits cleanly
- test target builds under CTest

## Exit criteria

Stage 0 is complete when:

- a clean build succeeds from an empty build directory
- all four selected dependencies are linked successfully
- the application opens a FLTK window
- CTest executes successfully
- CI runs build + tests through the self-contained targets (0.7)
- no core component depends directly on FLTK

---

# Stage 1 — Terminal Session

## Objective

Create a reliable process/PTY layer and feed terminal output into libtsm.

## 1.1 Define terminal session interface

Create:

```cpp
class TerminalSession {
public:
    virtual ~TerminalSession() = default;

    virtual bool start(const SessionOptions&) = 0;
    virtual std::size_t read(std::span<std::byte>) = 0;
    virtual std::size_t write(std::span<const std::byte>) = 0;
    virtual bool resize(int cols, int rows) = 0;
    virtual void terminate() = 0;
    virtual bool running() const = 0;
};
```

Do not expose Linux-specific types from this interface.

Return values are portable byte counts. `read` returns 0 for EOF; errors are
reported as explicit status values, not via POSIX errno or `ssize_t`.

## 1.2 Define session options

Include:

- executable
- arguments
- environment
- working directory
- initial rows/columns
- TERM value
- login-shell flag

Keep the representation platform-neutral.

## 1.3 Implement Linux PTY backend

Implement a Linux-specific class using the system PTY APIs.

Required behavior:

1. Create master/slave PTY.
2. Fork child.
3. Establish controlling terminal.
4. Set terminal dimensions.
5. Set environment.
6. `exec()` requested shell/program.
7. Parent retains master FD.
8. Set master FD non-blocking.
9. Detect child exit via SIGCHLD + `waitpid()`; this is authoritative for the
   exit status.
10. Treat EOF on the master FD as a hint only. Grandchildren can keep the
    slave FD open after the direct child exits (e.g. `screen`, daemons), so
    EOF alone must not end the session.

Keep child-process setup isolated from application code.

## 1.4 Implement I/O loop

Use a dedicated mechanism for reading PTY output.

For the first implementation, use a single application-owned I/O thread.

Thread responsibilities:

- block waiting for PTY data
- read batches
- deliver byte buffers to terminal core
- detect EOF/process termination

Do not make the FLTK UI thread block on PTY I/O.

## 1.5 Integrate libtsm

Create:

```cpp
class TerminalEmulator {
public:
    void feed(std::span<const std::byte>);
    void resize(int cols, int rows);
};
```

Initialize libtsm with the required callbacks/state.

The first implementation only needs output processing; rendering comes later.

## 1.6 Session lifecycle

Implement these transitions explicitly:

```text
Created
  ↓
Starting
  ↓
Running
  ↓
Stopping
  ↓
Exited
```

Handle:

- normal shell exit
- EOF
- failed exec
- failed PTY creation
- external termination
- application shutdown

A session only leaves `Running` when `waitpid()` confirms the direct child
exited. EOF on the master FD does not end the session by itself.

## 1.7 Threading and synchronization

Terminal state is owned exclusively by the UI thread.

The I/O thread (1.4) never touches terminal state. It:

- reads PTY data
- enqueues byte buffers into a bounded SPSC queue
- calls `Fl::awake()` to notify the UI thread

The UI thread:

- drains the queue and calls `feed()` on the terminal core
- coalesces wakeups; several notifications may trigger one redraw
- performs all rendering and input handling

No locks guard terminal state. The queue is the only shared object.

Queue-full policy: the reader briefly blocks. PTY output is never buffered
without bound.

## Tests

Unit/integration tests:

1. Start shell.
2. Run deterministic command.
3. Capture output.
4. Verify output reaches libtsm.
5. Verify terminal resize.
6. Verify shell termination is detected.
7. Verify repeated start/stop does not leak descriptors/processes.

## Exit criteria

Stage 1 is complete when:

- a shell starts in a PTY
- commands execute
- output reaches libtsm
- resize works
- process exit is detected
- the UI thread never blocks on PTY I/O

---

# Stage 2 — Terminal Widget

## Objective

Create a functional terminal surface backed by libtsm and rendered inside FLTK.

## 2.1 Define terminal widget boundary

Create:

```cpp
class TerminalWidget : public Fl_Widget {
public:
    TerminalWidget(...);

protected:
    void draw() override;
    int handle(int event) override;
};
```

The widget owns presentation state only.

It must not own the PTY.

## 2.2 Define terminal model API

Expose only the data the renderer requires:

- visible rows
- visible columns
- cell attributes
- cursor state
- dirty state
- scroll position

Avoid exposing libtsm structures throughout the application.

## 2.3 Implement font setup

Start with one configurable monospace font.

Requirements:

- fixed character cell metrics
- configurable font size
- high-DPI aware cell sizing
- fallback to a known system monospace font

Do not add HarfBuzz/FreeType as dependencies at this stage.

Font and colour values come from a plain styling struct with defaults
centralized in C++ from day one. Stage 5 only changes where the struct is
filled from, not the widget code.

## 2.4 Implement basic rendering

Render:

1. background
2. terminal cells
3. foreground colours
4. attributes needed by the initial feature set
5. cursor

Use contiguous screen traversal.

Start with full redraws for correctness.

## 2.5 Dirty-region tracking

After correct full rendering works:

- track modified rows/cells
- invalidate only affected areas
- retain full-redraw fallback

Do not optimize beyond this until rendering is functional.

## 2.6 Keyboard input

Map FLTK keyboard events to terminal input.

Implement:

- printable characters
- Enter
- Backspace
- Tab
- Escape
- arrows
- Home/End
- Page Up/Page Down
- function keys
- modifier combinations

Create a translation layer:

```text
FLTK event → terminal key event → encoded terminal sequence
```

Do not put key encoding logic directly inside `TerminalWidget`.

Before finalizing the translation layer, verify FLTK's IME / non-Latin input
support (dead keys, CJK input methods). Where FLTK delivers composed text,
route it through the same path as printable characters.

## 2.7 Mouse input

Implement:

- button press/release
- text selection
- drag
- wheel scrolling
- terminal mouse reporting when enabled by the terminal

## 2.8 Clipboard

Implement:

- copy selected text
- paste clipboard contents
- primary selection where supported

Respect bracketed paste once the terminal state supports it.

## 2.9 Scrollback

libtsm provides scrollback internally (`tsm_screen_set_max_sb`, `tsm_screen_sb_up/down`,
`tsm_screen_sb_page_up/down`, `tsm_screen_sb_reset`, `tsm_screen_sb_get_line_count`,
`tsm_screen_sb_get_line_pos`). Use it; do not build a separate scrollback store.

Wrap it as follows:

- call `tsm_screen_set_max_sb()` with a configurable maximum line count (see Stage 5)
- expose scroll position and line count through the terminal model API (2.2)
- map mouse wheel and Shift/PageUp/PageDown to `sb_up`/`sb_down`/`sb_page_up`/`sb_page_down`
- reset the scroll position on keypress (`sb_reset`) unless the key is a scroll key
- when scrolled up, snap back to the live screen on new output only if the user
  has not locked the view (xterm-style behavior; keep it simple at first)

Avoid unbounded memory growth: the maximum must be finite and configurable.

## 2.10 Wide and combining characters

Rely on libtsm's cell width handling (it bundles wcwidth upstream v4) for
double-width and combining characters.

Requirements:

- reserve two cells for double-width glyphs; never split them across lines
- cursor movement and selection skip the trailing half of a wide char
- expose per-cell width through the terminal model API (2.2); the libtsm draw
  callback already reports it
- tests: CJK text, combining accents, and a wide char placed at the last column

## 2.11 Resize flow

Use one canonical order:

```text
widget resize
  → compute new cols/rows from cell metrics
  → resize libtsm screen
  → PTY resize (SIGWINCH to child)
  → full redraw
```

No reflow in v1; see Deferred Work.

## 2.12 Title handling

Handle the basic `OSC 0;title` / `OSC 2;title` window-title case:

- register `tsm_vte_set_osc_cb()` and parse `OSC 0` / `OSC 2`
- expose the title through the terminal model API (2.2) so tabs (Stage 4)
  and the window can display it
- remain silent on unhandled OSC codes; Stage 6 adds more (see 6.1)

## Tests

Add:

- ANSI colour rendering tests
- cursor movement tests
- keyboard mapping tests
- copy/paste tests
- resize tests
- scrollback tests
- deterministic screenshot/render tests if practical

## Exit criteria

Stage 2 is complete when:

- an interactive shell is usable
- text renders correctly
- typing works
- cursor works
- scrolling works
- copy/paste works
- terminal resize works
- basic colour/attribute handling works

---

# Stage 3 — Layout Engine

## Objective

Implement arbitrary Terminator-style pane layouts independently of the UI toolkit.

## 3.1 Define layout tree

Use a tree such as:

```cpp
struct SplitNode {
    Orientation orientation;
    float ratio;
    NodeId first;
    NodeId second;
};

struct TerminalNode {
    TerminalId terminal;
};

using LayoutNode = std::variant<SplitNode, TerminalNode>;
```

Use stable IDs rather than raw pointers for persistent references.

## 3.2 Implement core operations

Required operations:

- split horizontally
- split vertically
- remove terminal
- focus terminal
- find parent split
- calculate rectangles
- rebalance after removal

## 3.3 Layout calculation

Input:

```text
window rectangle
+
layout tree
```

Output:

```text
TerminalId → rectangle
```

Keep this calculation independent of FLTK widgets.

## 3.4 Integrate with FLTK

Create/update one `TerminalWidget` per terminal node.

The layout manager calculates geometry; FLTK applies geometry.

Do not allow widgets to own layout relationships.

## 3.5 Pane interaction

Implement:

- focus click
- directional focus
- split current pane
- close current pane
- resize split
- equalize splits

## 3.6 Keyboard commands

Create a command abstraction:

```cpp
enum class Command {
    SplitHorizontal,
    SplitVertical,
    ClosePane,
    FocusLeft,
    FocusRight,
    FocusUp,
    FocusDown,
    ResizePane,
};
```

Map shortcuts to commands separately.

## Tests

The layout engine must be heavily unit-tested without FLTK.

Test:

- single-node layout
- nested splits
- repeated splitting
- removal
- rebalancing
- rectangle calculations
- focus navigation

## Exit criteria

Stage 3 is complete when:

- arbitrary nested split layouts work
- panes resize correctly
- pane removal leaves a valid tree
- all layout logic can be tested without launching the GUI

---

# Stage 4 — Tabs and Sessions

## Objective

Combine the terminal session, terminal model, widgets and layout engine into a full multi-tab application.

## 4.1 Define application objects

Use explicit ownership:

```text
Application
 └── Window
      └── TabManager
           └── Tab
                └── Layout
                     └── TerminalInstance
                          ├── TerminalSession
                          └── TerminalEmulator
```

A tab owns its layout and all terminal instances in that layout.

## 4.2 Implement tabs

Required operations:

- create tab
- close tab
- select tab
- next/previous tab
- move tab
- rename tab later if needed

## 4.3 Terminal creation

Create a single factory:

```cpp
std::unique_ptr<TerminalInstance>
create_terminal(const TerminalOptions&);
```

This ensures all terminal creation passes through one path.

## 4.4 Session cleanup

Closing a terminal must:

1. stop its PTY session
2. wait for thread shutdown
3. release terminal state
4. destroy FLTK widget
5. remove layout node
6. rebalance layout

No orphaned child process or reader thread is acceptable.

## 4.5 Window lifecycle

Implement:

- clean shutdown
- close confirmation only where necessary
- close all terminal processes
- join background threads before exit

## Tests

Test:

- multiple simultaneous shells
- multiple tabs
- independent tab layouts
- closing active/non-active tabs
- rapid create/destroy
- application shutdown with active sessions

## Exit criteria

Stage 4 is complete when:

- multiple tabs work
- each tab supports arbitrary splits
- every terminal has an independent PTY
- tab and pane lifecycle is leak-free

---

# Stage 5 — Configuration

## Objective

Add YAML-based configuration without coupling configuration to runtime objects.

## 5.1 Define configuration model

Create strongly typed C++ configuration structures:

```cpp
struct GeneralConfig;
struct FontConfig;
struct ColourConfig;
struct ShortcutConfig;
struct ProfileConfig;
struct LayoutConfig;
struct ApplicationConfig;
```

Do not pass raw YAML nodes through the application.

## 5.2 Define YAML schema

Use one documented schema.

Example structure:

```yaml
general:
  shell: /bin/bash
  scrollback: 10000

font:
  family: monospace
  size: 12

profiles:
  default:
    foreground: "#ffffff"
    background: "#000000"

shortcuts:
  new_tab: "Ctrl+Shift+T"
  close_tab: "Ctrl+Shift+W"
```

Keep the schema versioned from the beginning:

```yaml
version: 1
```

## 5.3 Implement loader

Implement:

```cpp
Config load_config(const std::filesystem::path&);
```

Behavior:

- missing file → defaults
- malformed file → clear error
- unknown keys → warning
- invalid values → error
- missing fields → defaults

## 5.4 Implement writer

Only write configuration when explicitly requested.

Preserve a predictable format.

Do not rewrite user configuration on every startup.

## 5.5 Implement defaults

Define all defaults in C++, not scattered throughout UI code.

## 5.6 Layout persistence

Serialize:

- tab order
- split orientation
- split ratios
- profile assignment
- working directory
- selected/focused pane

Do not persist transient terminal screen contents unless specifically added later.

## Tests

Test:

- empty configuration
- missing configuration
- valid configuration
- malformed YAML
- unknown fields
- invalid values
- round-trip serialization
- layout serialization/deserialization

## Exit criteria

Stage 5 is complete when:

- configuration is loaded deterministically
- defaults are centralized
- profiles/shortcuts are configurable
- layouts can be saved/restored
- malformed configuration cannot crash the application

---

# Stage 6 — Terminator Feature Parity

## Objective

Close functional gaps against the target Terminator workflow.

Implement in this order.

## 6.1 Essential interaction features

- pane broadcast input
- pane zoom/maximize
- pane movement
- tab movement
- terminal renaming
- dynamic title updates beyond the basic OSC 0/2 case (e.g. OSC 7
  working-directory reporting)

## 6.2 Terminal functionality

- alternate screen
- bracketed paste
- application cursor mode
- application keypad mode
- bell
- visual bell
- title/working-directory updates
- environment handling

## 6.3 Search and navigation

Implement terminal search over scrollback.

Requirements:

- forward/backward search
- case sensitivity option
- wrap-around
- highlight matches
- keyboard navigation

Keep search out of the PTY/terminal-state layer.

## 6.4 URL handling

Add URL detection as a presentation feature.

Do not mutate terminal state to implement URL highlighting.

## 6.5 Profile support

Support multiple named profiles for:

- font
- colours
- shell
- environment
- cursor
- scrollback

## 6.6 Layout restore

Restore:

- tab count
- pane tree
- terminal profiles
- working directories where valid

If a restored process cannot be recreated, open a normal shell instead and report the fallback.

## Exit criteria

Stage 6 is complete when a normal Terminator user can reproduce the expected multi-tab/multi-pane workflows without missing core terminal behavior.

---

# Stage 7 — Efficiency Pass

## Objective

Reduce resource usage based on measured bottlenecks rather than assumptions.

## 7.1 Establish benchmarks

Create repeatable measurements for:

- startup time
- idle CPU
- memory at 1 terminal
- memory at 10 terminals
- memory at 50 terminals
- PTY throughput
- parser throughput
- redraw rate
- scrollback memory
- resize latency

## 7.2 Build test workloads

Create deterministic workloads:

1. shell idle
2. sustained text output
3. large `cat`/generated output
4. full-screen application
5. resize storm
6. many simultaneous terminals
7. large scrollback

## 7.3 Profile

Use platform profiling tools such as:

- `perf`
- heap profilers
- sanitizers
- compiler instrumentation

Measure:

- allocations
- CPU hotspots
- lock contention
- redraw frequency
- memory growth

## 7.4 Optimize terminal data path

Prioritize:

1. unnecessary allocations
2. unnecessary copies
3. parser overhead
4. screen updates
5. rendering
6. scrollback representation

Use batch operations wherever possible.

## 7.5 Optimize rendering

Move from full redraws to dirty-region redraws.

Ensure:

- inactive terminals do not repaint continuously
- no timer-driven redraw loop exists without need
- PTY output schedules a redraw rather than forcing repeated redraws

## 7.6 Validate memory behavior

Check for:

- descriptor leaks
- thread leaks
- terminal/session leaks
- unbounded scrollback
- retained FLTK objects
- retained strings/buffers

Run long-duration tests.

## Exit criteria

Stage 7 is complete when:

- benchmark results are recorded
- major hotspots have been addressed
- memory remains bounded
- idle CPU is negligible
- high-output workloads remain responsive

---

# Stage 8 — Portability

## Objective

Move from Linux-only execution to portable platform implementations without destabilizing the core.

## 8.1 Lock down the portable core

Before adding another platform, ensure these modules have no platform-specific code:

- terminal state
- layout
- configuration
- profiles
- commands
- tab management
- search
- rendering model

## 8.2 Formalize platform interface

Create interfaces for:

```cpp
class TerminalSession;
class Process;
class PlatformClipboard;
class PlatformEnvironment;
```

Only add an abstraction when multiple platforms actually need it.

## 8.3 macOS

Implement:

- PTY/session backend
- process lifecycle
- environment handling
- terminal resizing

Keep the existing terminal core unchanged.

## 8.4 Windows

Implement:

- ConPTY backend
- process creation
- input/output pipes
- resize
- process termination
- Windows-specific environment handling

Do not introduce Windows-specific behavior into terminal parsing.

## 8.5 Build matrix

CI should build and test:

- Linux
- macOS
- Windows

At minimum run:

- unit tests
- configuration tests
- layout tests
- core terminal tests

Platform-specific PTY integration tests should run on the relevant operating system.

## 8.6 Packaging

Only after cross-platform builds are stable, define native packaging:

- Linux packages/bundles
- macOS application bundle
- Windows executable/package

Keep runtime dependencies minimal and document which libraries are dynamically linked.

## Exit criteria

Stage 8 is complete when:

- portable core builds on all target platforms
- each platform has a working terminal session backend
- terminal behavior remains consistent across platforms
- packaging does not require unnecessary runtime components

---

# Cross-Stage Testing Strategy

## Unit tests

Test independently:

- configuration
- key mapping
- terminal state
- layout tree
- geometry calculation
- search
- serialization

## Integration tests

Test:

- PTY ↔ terminal core
- terminal core ↔ renderer
- layout ↔ widgets
- tabs ↔ sessions
- configuration ↔ application state

## Manual test matrix

Maintain a small manual checklist covering:

- shell startup
- typing
- colours
- resize
- scrolling
- copy/paste
- fullscreen terminal applications
- tabs
- nested splits
- pane movement
- application shutdown
- restored layouts

---

# Deferred Work

## Reflow on resize

libtsm 4.8.0 does not rewrap lines on resize (`tsm_screen_resize()` extends or
truncates lines only). v1 ships with that behavior.

Reflow is planned as a fork of libtsm in a separate repository, using the VTE
approach:

- add a per-line soft-wrap flag to `struct line`, set when autowrap continues
  a line, cleared by hard newline, CR, EL, CUP, and ICH/DCH at the last column
- on width change, walk screen and scrollback lines and re-wrap them, merging
  soft-wrapped lines and preserving hard-wrapped ones
- skip reflow on the alternate screen
- restore the cursor best-effort
- test with the libvterm reflow case catalogue plus wide/combining characters

Contribute the patch upstream (kmscon/libtsm is active; releases are current)
and integrate the fork once the current implementation is complete.

# Dependency Policy

Initial dependency set:

```text
C++20
CMake
FLTK
libtsm
utf8proc
rapidyaml
```

Do not add another library until:

1. The requirement is concrete.
2. The standard library and existing dependencies are insufficient.
3. The dependency is compatible with the lightweight/portable goals.
4. Its maintenance and licensing are acceptable.
5. Its runtime/build cost is justified.

# Recommended Implementation Order

Implement exactly in this sequence:

```text
Stage 0
  ↓
Stage 1
  ↓
Stage 2
  ↓
Stage 3
  ↓
Stage 4
  ↓
Stage 5
  ↓
Stage 6
  ↓
Stage 7
  ↓
Stage 8
```

Do not begin broad optimization or cross-platform work before Stage 4 is stable.

The critical architectural boundary to preserve throughout is:

```text
FLTK UI
    │
Application model
    │
Terminal core
    │
Platform session backend
    │
PTY / ConPTY
```

The terminal core and layout engine should remain testable without launching the GUI.
