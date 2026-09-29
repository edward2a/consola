# Detailed Implementation Plan

## Project constraints

### Fixed technology choices

- Language: **C++20**
- Build system: **CMake**
- GUI: **FLTK**
- Terminal state / VT handling: **libtsm**
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
- rapidyaml

Full details: `docs/dependencies.md`.

- CMake `FetchContent` pulls every dep at configure time
- each dep pinned to a git commit hash (tag in a comment), all in
  `cmake/deps.cmake`
- all deps built from source, linked statically by default
- libtsm is meson-only upstream: fetch its sources and compile them with our
  own `cmake/libtsm.cmake` (no meson). `-ffast-math` stays on the libtsm
  target only.
- FLTK pinned to 1.4.x
- `CONSOLA_USE_SYSTEM_DEPS` (default OFF): use installed packages instead;
  minimum version = pinned version; too old fails configure with a clear
  message
- install the license texts of all static deps under
  `share/doc/consola/licenses/`

### 0.3 Establish error handling

Use exceptions only at application/setup boundaries.

Within core runtime code:

- return explicit status/result values where practical: plain enums and small
  structs (e.g. `IoStatus`/`IoResult`, `SessionError` in 1.1). C++20 has no
  `std::expected`, and no extra dependency is added for one.
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

CI uses the same pinned FetchContent deps (fetch cache optional; no meson
needed).

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
- all three selected dependencies are linked successfully
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
enum class IoStatus { ok, would_block, eof, error };

struct IoResult {
    std::size_t count;   // bytes moved; nonzero only with ok
    IoStatus    status;
};

enum class SessionError { none, pty_failed, fork_failed, exec_failed,
                          resize_failed, not_running };

class TerminalSession {
public:
    virtual ~TerminalSession() = default;

    virtual SessionError start(const SessionOptions&) = 0;
    virtual IoResult read(std::span<std::byte>) = 0;
    virtual IoResult write(std::span<const std::byte>) = 0;
    virtual SessionError resize(int cols, int rows) = 0;
    virtual void terminate() = 0;
    virtual bool running() const = 0;
};
```

Do not expose Linux-specific types from this interface. The result types live
in the error-handling module (0.3).

Result semantics:

- `read`:
  - `ok` with `count > 0`: data was read
  - `would_block`: no data ready (the master FD is non-blocking)
  - `eof`: a hint only; see 1.3 #10
  - `error`: the read failed
- `write`:
  - `ok`: `count` bytes were written; a short write is still `ok`
  - `would_block`: nothing was written (`count == 0`)
  - the caller keeps the unwritten remainder
- OS error codes (errno, GetLastError) are logged inside the backend and never
  cross the interface. No POSIX errno or `ssize_t` in the API.

## 1.2 Define session options

Include:

- executable
- arguments
- environment
- working directory
- initial rows/columns
- TERM value
- login-shell flag

Defaults: `TERM=xterm-256color`, `COLORTERM=truecolor`. Both can be
overridden by config.

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
9. Detect child exit with `waitpid(pid, WNOHANG)`; this is authoritative for
   the exit status. How the I/O loop is notified that a child exited is
   described in 1.4. The SIGCHLD disposition must not be `SIG_IGN`, or the
   kernel reaps children itself and the exit status is lost.
10. Treat EOF on the master FD as a hint only. Grandchildren can keep the
    slave FD open after the direct child exits (e.g. `screen`, daemons), so
    EOF alone must not end the session.
11. Report exec failure synchronously. Before forking, create a pipe with
    `O_CLOEXEC`. If `exec()` fails, the child writes its errno to the pipe
    and calls `_exit()`. The parent reads the pipe: EOF means `exec()`
    succeeded; data means `start()` returns `exec_failed` (errno is logged).

Keep child-process setup isolated from application code.

## 1.4 Implement I/O loop

Use one application-owned I/O thread for all sessions. It starts once at
application startup and stops at shutdown. The UI thread never blocks on PTY
I/O.

The loop waits in `poll()` on:

- each session's master FD: `POLLIN`, plus `POLLOUT` while writes are pending
- one wake FD: `eventfd` on Linux, a self-pipe on other POSIX systems
- one exit-notification FD per child

Exit notification:

- Linux: call `pidfd_open()` on the child right after `fork()`. This is
  race-free: the pid cannot be reused until we reap it.
- Fallback (other POSIX, Linux < 5.3): a SIGCHLD handler writes a byte to a
  self-pipe; the loop then calls `waitpid(pid, WNOHANG)` for each known pid.
- Never call `waitpid(-1)`; it would reap children the application does not
  own.

Commands from the UI arrive through a UI→loop command queue, followed by a
signal on the wake FD:

- `add`: register a session
- `remove`: unregister a session; the loop acknowledges it
- `terminate`: send SIGHUP and start the kill timer (1.6)
- `write-pending`: a session has outgoing bytes (1.7)
- `resume`: a session's inbound queue has room again (1.7)
- `shutdown`: stop the loop

Event handling:

- readable: `read()` until `would_block`; push `data` messages to that
  session's inbound queue
- EOF: push `eof`, stop polling `POLLIN` on that FD, keep waiting for the exit
  notification (1.3 #10)
- exit notification: `waitpid(pid, WNOHANG)`, then push `exited{status}`
- writable: write from the session's outgoing queue until `would_block` or
  empty; poll `POLLOUT` only while bytes remain

The implementation lives in `platform/linux`, behind a narrow, platform-neutral
`IoLoop` interface, so the ConPTY backend (Stage 8) can work differently
underneath.

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
- EOF (`IoStatus::eof`; hint only)
- failed exec (`SessionError::exec_failed`, via the 1.3 #11 pipe)
- failed PTY creation (`SessionError::pty_failed`)
- failed fork (`SessionError::fork_failed`)
- external termination
- application shutdown

A session only leaves `Running` when the UI thread receives `exited{status}`
from the I/O loop, i.e. after `waitpid()` confirmed the direct child exited
(1.4). EOF on the master FD does not end the session by itself.

`terminate()`:

- returns at once; never blocks the UI
- session goes `Running` → `Stopping`; `Exited` when `exited` arrives
- sends the `terminate` command; the loop does
  `kill(-pgid, SIGHUP)` on the child's process group (child is session
  leader, 1.3 #3, so pgid = pid)
- kill timer: if `exited` has not arrived after the timeout (default 2 s,
  configurable in Stage 5), the loop sends `kill(-pgid, SIGKILL)`. The timer
  uses the `poll()` timeout; no extra thread.
- reaping stays on the normal path: pidfd → `waitpid` → `exited{status}`

Application shutdown: terminate all sessions, wait until all report `exited`
or the timeout passes, SIGKILL the rest, then quit.

## 1.7 Threading and synchronization

Ownership:

- Terminal state is owned exclusively by the UI thread.
- The I/O loop (1.4) never touches terminal state.
- No locks guard terminal state. The queues below are the only shared
  objects.

Inbound (loop → UI):

- one bounded SPSC queue per session; messages are `data`, `eof` and
  `exited{status}`
- one queue per session guarantees that all of a session's output arrives
  before its `exited` message
- the UI thread drains the queues, calls `feed()` on the terminal core and
  performs all rendering and input handling
- backpressure: when a session's queue is full, the loop stops polling that FD
  for `POLLIN`. Unread output stays in the kernel PTY buffer, and eventually
  the child blocks on write. When the UI has drained the queue below a
  low-water mark, it sends `resume`. Other sessions keep flowing. PTY output
  is never buffered without bound.

Drain budget (UI side):

- per wakeup, the UI calls `feed()` for at most ~8 ms, then stops
- sessions are drained round-robin, so one flood cannot starve other panes
- data left over: schedule the next drain with `Fl::add_timeout(0)`; FLTK
  handles pending input and redraws in between, so keys still get through
- one redraw per drain, not per chunk
- backpressure unchanged: queues stay full, the loop stops reading, the child
  blocks

Outbound (UI → loop):

- the libtsm write callback, key input, pastes and automatic replies (device
  attributes, cursor-position reports) all run on the UI thread
- they append to that session's bounded SPSC byte queue, then the UI sends
  `write-pending`
- the loop is the only thread that writes to the master FD
- if the outgoing queue is full, the remainder goes to a UI-side overflow
  buffer (bounded by the paste size); the loop calls the wake callback when
  space frees up, and the UI moves more bytes into the queue
- byte order is preserved

Wakeups:

- core and platform code receive an injected `std::function<void()>` wake
  callback. Only the UI layer implements it with `Fl::awake()`, which keeps
  architectural rule 1.
- `Fl::lock()` is called once at startup, before the loop starts.
- wakeups are coalesced behind one global `std::atomic<bool>` flag, because
  FLTK's awake queue has a fixed size. The loop calls the wake callback only
  if it changes the flag from false to true. The UI clears the flag first and
  then drains every non-empty queue, so no wakeup is lost. Several
  notifications may trigger one redraw.

Child exit: the decision "SIGCHLD + `waitpid()` is authoritative" stands. The
pidfd and the SIGCHLD self-pipe (1.4) only notify the loop; `waitpid()`
provides the exit status.

## Tests

Unit/integration tests:

1. Start shell.
2. Run deterministic command.
3. Capture output.
4. Verify output reaches libtsm.
5. Verify terminal resize.
6. Verify shell termination is detected.
7. Verify repeated start/stop does not leak descriptors/processes.
8. Run 50 sessions on the single I/O thread.
9. Send a large paste under backpressure; verify byte order is preserved.
10. A grandchild keeps the PTY open after the direct child exits; verify the
    session still exits.
11. Force the SIGCHLD self-pipe fallback in a test build; repeat tests 6–10.
12. A child that ignores SIGHUP is killed after the kill timeout.
13. During `cat /dev/urandom | base64`, Ctrl+C gets through promptly.

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
6. scrollbar (2.9)

Use contiguous screen traversal (`tsm_screen_draw2()`).

Colours: the libtsm draw callback gives RGB per cell (truecolor included).
No palette code on our side.

Start with full redraws for correctness.

## 2.5 Dirty-region tracking

After correct full rendering works:

- track modified cells with libtsm ages: the draw callback reports each
  cell's `tsm_age_t`; keep the age returned by the last draw and redraw only
  newer cells
- invalidate only affected areas
- retain full-redraw fallback (age 0 or age overflow means redraw all)

Do not optimize beyond this until rendering is functional.

## 2.6 Keyboard input

libtsm encodes keys itself: `tsm_vte_handle_keyboard(vte, keysym, ascii,
mods, unicode)` handles Ctrl+letter, Alt (ESC prefix), cursor-key mode, keypad
application mode, modified arrows/F-keys, and falls back to UTF-8 for text.
Do not write a separate encoder.

Key path:

```text
FLTK event → KeyEvent{keysym, ascii, mods, unicode} → tsm_vte_handle_keyboard()
```

The translation module lives outside `TerminalWidget` and contains no
encoding logic. It only:

- passes special keys through unchanged: FLTK key codes are X11 keysym values
  (`FL_Left` = `XKB_KEY_Left`, `FL_F + n`, `FL_KP + c`, etc.)
- maps FLTK Shift/Ctrl/Alt/Meta state to `TSM_*_MASK`
- sets `unicode` by decoding `Fl::event_text()` with `fl_utf8decode()`
- sets `ascii` for Ctrl combinations, so Ctrl+C works on non-Latin layouts

The keysym constants come from libtsm's bundled
`external/xkbcommon/xkbcommon-keysyms.h`; libxkbcommon is not a runtime
dependency.

IME and dead keys: composed text from FLTK is sent with
`keysym = XKB_KEY_NoSymbol` and the composed codepoint(s) as `unicode`.
Verify FLTK's IME support (dead keys, CJK input methods) before finalizing.

Backspace sends DEL (0x7f), like xterm, VTE and Terminator:
`tsm_vte_set_backspace_sends_delete(vte, true)`. It becomes a config option in
Stage 5.

v1 limitation: libtsm does not support modifyOtherKeys or the kitty keyboard
protocol.

Tests: vim and less, covering arrows in both cursor-key modes, Ctrl, Alt and
function keys.

## 2.7 Mouse input

Implement:

- button press/release
- text selection (double-click selects a word, defined by a configurable
  "word chars" set, as in Terminator)
- drag
- wheel scrolling
- terminal mouse reporting when enabled by the terminal

Mouse reporting is encoded by libtsm: check `tsm_vte_get_mouse_mode()` and
pass events to `tsm_vte_handle_mouse()` (X10, VT200, SGR and pixel modes).
Do not write a separate mouse encoder.

Selection uses libtsm's API (`tsm_screen_selection_start/target/copy/reset`);
no own selection store. Word boundaries for double-click are computed by us
from the "word chars" set, then passed to `selection_start/target`.

## 2.8 Clipboard

Implement:

- copy selected text
- paste clipboard contents
- primary selection where supported

Send pastes through `tsm_vte_paste()`. It adds the bracketed-paste markers
itself when the program enabled mode 2004.

## 2.8a Bell

Register `tsm_vte_set_bell_cb()` with a no-op callback now. Audible and
visual bell are implemented in 6.2.

## 2.9 Scrollback

libtsm provides scrollback internally (`tsm_screen_set_max_sb`, `tsm_screen_sb_up/down`,
`tsm_screen_sb_page_up/down`, `tsm_screen_sb_reset`, `tsm_screen_sb_get_line_count`,
`tsm_screen_sb_get_line_pos`). Use it; do not build a separate scrollback store.

Wrap it as follows:

- call `tsm_screen_set_max_sb()` with a configurable maximum line count (see Stage 5)
- expose scroll position and line count through the terminal model API (2.2)

Keys:

- Shift+PageUp/PageDown scroll one page (`sb_page_up`/`sb_page_down`);
  Shift+Home/End jump to the oldest line / back to live (`sb_up(count)` /
  `sb_reset`). The terminal consumes these; the program never sees them.
- plain PageUp/PageDown/Home/End always go to the program (vim, less, shells
  need them)
- any other key that sends bytes to the program first returns the view to
  live (`sb_reset`), then is sent. Modifier-only presses and the scroll keys
  above do not.

Mouse wheel (3 lines per notch; configurable in Stage 5):

| State | Wheel does |
|---|---|
| mouse reporting on (`tsm_vte_get_mouse_mode() != 0`) | goes to the program via `tsm_vte_handle_mouse()` (buttons 4/5); Shift+wheel scrolls our scrollback instead |
| alternate screen, reporting off | sends plain Up/Down arrows via `tsm_vte_handle_keyboard()`, one per line (xterm "alternateScroll", as in VTE/Terminator), so less/man scroll. Can be disabled in Stage 5. |
| normal screen, reporting off | scrolls the scrollback (`sb_up`/`sb_down`) |

The alternate screen is detected with
`tsm_screen_get_flags() & TSM_SCREEN_ALTERNATE`. It never adds lines to
scrollback. libtsm does not parse DECSET 1007, so programs cannot toggle
alternate scroll; only the config switch can.

New output while scrolled back:

- the view stays where it is. libtsm already anchors it (`sb.pos`); if the
  scrollback cap drops the line the view is on, the view moves to the oldest
  line. There is no auto-snap to live and no separate "locked view" setting.
- each pane has an FLTK scrollbar showing position within scrollback;
  dragging it scrolls. It can be hidden via config in Stage 5.
- switching to or from the alternate screen returns the view to live

Tests:

- plain PageUp reaches the program; Shift+PageUp scrolls
- a keypress while scrolled back returns to live
- the view stays put while output streams in
- the scrollback cap drops the line the view is on
- wheel behaviour in each of the three states above

Avoid unbounded memory growth: the maximum must be finite and configurable.

## 2.10 Wide and combining characters

Character widths come from libtsm's bundled wcwidth (the termux C port of
jquast/wcwidth).

Requirements:

- reserve two cells for double-width glyphs
- cursor movement and selection skip the trailing half of a wide char
- expose per-cell width through the terminal model API (2.2); the libtsm draw
  callback already reports it
- tests: CJK text, and a wide char placed at the last column

v1 limitations (accepted):

- combining characters are dropped: `tsm_screen_write()` ignores zero-width
  codepoints. The fix is planned in the libtsm fork (see Deferred Work).
- a wide char written at the last column is clipped, not wrapped to the next
  line.

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
                ├── Layout      (tree of TerminalIds only)
                └── terminals: map<TerminalId, unique_ptr<TerminalInstance>>
                     └── TerminalInstance
                          ├── TerminalSession
                          └── TerminalEmulator
```

A tab owns its layout and its terminal instances. The layout refers to
terminals only by `TerminalId` (3.1), never by pointer.

`TerminalId` is unique across the whole application, not per tab.

Moving a pane to another tab (6.1):

1. remove the node from the old tab's layout
2. move the `unique_ptr` into the new tab's map
3. insert a node in the new tab's layout

The session keeps running; only the widget is re-parented.

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

1. call `terminate()` and wait (asynchronously) for `exited` (1.6)
2. send `remove` to the I/O loop and wait for its acknowledgement (1.4)
3. release terminal state
4. destroy FLTK widget
5. remove layout node
6. rebalance layout

No orphaned child process or stale loop registration is acceptable.

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
  scrollbar: true              # 2.9
  wheel_lines: 3               # 2.9
  alternate_scroll: true       # 2.9: wheel sends arrows on alternate screen
  backspace_sends_delete: true # 2.6
  kill_timeout_ms: 2000        # 1.6: SIGHUP → SIGKILL delay

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

- audible bell and visual bell (via the 2.8a bell callback)
- title/working-directory updates
- environment handling

Alternate screen, bracketed paste, application cursor mode and application
keypad mode are provided by libtsm and verified in Stage 2 (2.6, 2.8).

## 6.3 Search and navigation

Implement terminal search over scrollback.

Requirements:

- forward/backward search
- case sensitivity option (case-insensitive matching is ASCII only; no
  Unicode case folding)
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
- queue-full / backpressure events and drain-budget overruns (1.7)
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

Dirty-region redraws come from 2.5. Here, ensure:

- inactive terminals do not repaint continuously
- no timer-driven redraw loop exists without need
- redraws stay coalesced: one per drain (1.7), not per output chunk

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

Every OS runs the same 0.7 targets; CI platforms only wrap them. Deps come
from pinned FetchContent (0.2).

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

## libtsm fork

libtsm 4.8.0 lacks the two features below. Both are planned for a fork of
libtsm in a separate repository; v1 ships without them. Integrate the fork
once the current implementation is complete, and contribute the patches
upstream (kmscon/libtsm is active; releases are current).

libtsm uses an MIT-like license. The fork is written fresh: it follows VTE's
approach, but no VTE code (LGPL-3) is copied.

### Reflow on resize

libtsm does not rewrap lines on resize (`tsm_screen_resize()` extends or
truncates lines only).

Follow the VTE approach:

- add a per-line soft-wrap flag to `struct line`
- set the flag only when a printed character actually wraps onto the next
  line; a newline arriving at the right margin does not set it
- clear the flag on EL of that line; cursor moves (CUP, CR), overprinting,
  ICH and ECH do not clear it
- on width change, walk screen and scrollback lines and re-wrap them, merging
  soft-wrapped lines and preserving hard-wrapped ones
- skip reflow on the alternate screen
- restore the cursor best-effort
- tests: the libvterm reflow case catalogue A–I with VTE's results (reflow A,
  C, E–I; no reflow B, D), plus wide characters

### Combining characters

`tsm_screen_write()` returns early for zero-width codepoints
(`tsm-screen.c:1107`), so combining marks are dropped. `tsm_symbol_append()`
exists but nothing calls it.

Fix: append a zero-width codepoint to the previous cell's symbol with
`tsm_symbol_append()` and mark that cell changed.

Tests: single accent (`e` + U+0301), multiple marks on one base, a mark after
a wide char.

# Dependency Policy

Initial dependency set:

```text
C++20
CMake
FLTK
libtsm
rapidyaml
```

utf8proc was dropped: libtsm does UTF-8 decoding and character widths itself.
Add it back only if a concrete need appears (e.g. Unicode case folding).

All deps: fetched by the build, pinned to a commit hash, built from source,
linked statically by default. See `docs/dependencies.md`.

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
