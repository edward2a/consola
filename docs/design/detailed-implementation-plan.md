# Detailed Implementation Plan

## Project constraints

### Fixed technology choices

- Language: **C++20**
- Build system: **CMake**
- GUI: **FLTK**
- Terminal state / VT handling: **libtsm**
- Configuration: **YAML via rapidyaml**
- Initial target: **Linux, X11-only FLTK backend**; Wayland desktops require
  XWayland. Native Wayland support is wishlist-only, not part of v1.
- Portability target: **macOS + Windows**
- Distribution: **source-only for now**; static linking is for local builds.
  Binary releases and their compliance/packaging policy require separate
  approval. Implementation still requires explicit authorization after all
  reviews are settled.
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
- libtsm 4.8.0 revision: `ef2365d0f9f1a370d721a82470d69c3646fe97f0`.
  Its compiled hash table is LGPL-licensed; source-only distribution and
  preservation of applicable source notices/terms settle the current policy
  decision. See `docs/dependencies.md`; binary distribution is not approved.
- FLTK pinned to 1.4.5, commit `a9b1113516ffd15fc7602a6d425a317df30f4720`;
  v1 uses its X11/Xft backend with Wayland and Cairo/Pango disabled, including
  in system-dependency mode. Verified build options and attribution are
  recorded in `docs/dependencies.md`
- rapidyaml pinned to 0.16.0, commit
  `f8ac8dd50f4f7916579d55a05ebf9c6488e52670`; its bundled c4core 0.6.0
  originates at `1885dca396de5aea84f176eccb92a00c8b85c5ff`. Use standalone
  mode, without a separate c4core fetch, and exception-enabled error callbacks.
  Build options and bundled-source notice/provenance caveats are recorded in
  `docs/dependencies.md`
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
                          resize_failed, not_running, cancelled,
                          reply_overflow };

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

`start()` is synchronous for its caller, but only the startup worker (1.3)
calls it. Neither the UI thread nor the shared I/O loop runs the exec
handshake. The backend stays worker-owned until the startup result is handed
to the UI and registered with the I/O loop.

Result semantics:

- `read`:
  - `ok` with `count > 0`: data was read
  - `would_block`: no data ready (the master FD is non-blocking)
  - `eof`: a hint only; see 1.3 #10. The Linux PTY backend maps the master's
    slave-hangup `EIO` to this status, not to a generic read failure.
  - `error`: the read failed
- `write`:
  - `ok`: `count` bytes were written; a short write is still `ok`
  - `would_block`: nothing was written (`count == 0`)
  - the caller keeps the unwritten remainder
- OS error codes (errno, GetLastError) are logged inside the backend and never
  cross the interface. No POSIX errno or `ssize_t` in the API.

Initial dimensions must be positive before PTY creation. `resize()` rejects
non-positive columns/rows with `resize_failed`, without issuing an OS resize.

## 1.2 Define session options

Include:

- executable
- arguments
- environment
- working directory
- initial rows/columns
- TERM value
- login-shell flag
- close-output policy (`drain_output_on_close`, default false; 1.6)

Defaults: `TERM=xterm-256color`, `COLORTERM=truecolor`. Both can be
overridden by config.

Keep the representation platform-neutral.

## 1.3 Implement Linux PTY backend

Implement a Linux-specific class using the system PTY APIs.

Startup execution:

- one application-owned startup worker handles a bounded queue of launch
  requests; no thread per terminal. A blocking launch must not stall the UI
  or the shared I/O loop.
- requests contain owned, immutable session options and `TerminalId`; results
  return through a bounded worker-to-UI queue and the injected wake callback
- before `fork()`, prepare executable/PATH resolution relative to the requested
  working directory, argv (including login-shell argv[0]), envp, signal setup
  and error-report storage. Keep PTY/error-pipe descriptors above 2 so child
  stdio duplication cannot overwrite the error pipe. Do not mutate the
  parent's environment.
- use `openpty()` in the parent, then explicit `fork()` and audited child
  setup rather than an opaque post-fork helper
- queued launches can be cancelled before forking. Cancellation also wakes
  the worker's exec-pipe wait. A successful launch completed during closure
  must still be handed to the UI, registered and closed normally (1.6).

Required behavior:

1. Create master/slave PTY with initial dimensions in the parent; mark both
   close-on-exec before forking.
2. Fork child.
3. Establish a new session and controlling terminal in the child.
4. Connect the slave to stdin/stdout/stderr, restore child signal dispositions
   and mask, and change to the prepared working directory.
5. Use the prepared argv and envp; no `setenv()` or argument construction in
   the child.
6. `execve()` the prepared executable.
7. Parent retains master FD.
8. Set master FD non-blocking.
9. Detect child exit with `waitpid(pid, WNOHANG)`; this is authoritative for
   the exit status. How the I/O loop is notified that a child exited is
   described in 1.4. The SIGCHLD disposition must not be `SIG_IGN`, or the
   kernel reaps children itself and the exit status is lost.
10. Treat EOF on the master FD as a hint only. Grandchildren can keep the
    slave FD open after the direct child exits (e.g. `screen`, daemons), so
    EOF alone must not end the session.
11. Report child-setup/exec failure synchronously to the startup worker.
    Before forking, create a pipe with `O_CLOEXEC`. On failure, the child
    writes a fixed-size setup-stage/errno record and calls `_exit()`; no
    logging in the child. The worker waits on the pipe and its cancellation
    wake FD, not an uncancellable blocking read. EOF without an error record
    is the exec-success handshake; a record means `start()` returns
    `exec_failed` and the parent logs the stage and errno. Handle interrupted
    and partial I/O. Abnormal pre-exec child death may also close the pipe;
    normal exit detection handles it rather than claiming the program ran.

Post-fork child contract:

- use only async-signal-safe operations such as `setsid`, `dup2`, `close`,
  `chdir`, `sigaction`, `sigprocmask`, `execve`, `write` and `_exit`
- Linux controlling-terminal setup uses an audited, non-allocating syscall
  path; do not assume higher-level PTY helpers are post-fork safe
- no allocation, C++ exceptions/destructors, stdio, logging, FLTK calls,
  environment mutation or application locks in the child
- close unused pipe ends promptly. Only standard streams reach the executed
  program; other application descriptors must be close-on-exec or closed
  through the audited child path. The error-pipe write end stays open until
  failure or exec closes it.
- on failed or cancelled startup, the worker closes its descriptors and
  kills/reaps any private child before returning an error/cancellation
  result (`SessionError::cancelled` for cancellation). Before registration
  the worker alone owns cleanup; after handoff the normal I/O-loop lifecycle
  owns it. Never reap the same child twice.

Keep child-process setup isolated from application code.

## 1.4 Implement I/O loop

Use one application-owned I/O thread for all sessions. It starts once at
application startup and stops at shutdown. The UI thread never blocks on PTY
I/O.

The loop waits in `poll()` on:

- each active session master FD: `POLLIN`, plus `POLLOUT` while writes are
  pending; omit suspended or terminally closed PTYs as described below
- one wake FD: `eventfd` on Linux, a self-pipe on other POSIX systems
- one exit-notification FD per child

Exit notification:

- Linux: the startup worker calls `pidfd_open()` on the child right after
  `fork()` and transfers the FD with the successful backend. This is
  race-free: the pid cannot be reused until we reap it.
- Fallback (other POSIX, Linux < 5.3): a SIGCHLD handler writes a byte to a
  self-pipe; the loop then calls `waitpid(pid, WNOHANG)` for each known pid.
- on every `add`, first take ownership and insert the session in the
  registry, then immediately call `waitpid(pid, WNOHANG)` (retry `EINTR`).
  SIGCHLD may have been consumed before registration, especially for an
  instant-exit child. A confirmed exit enters the same reaped/status and
  post-exit output path as an exit notification; otherwise keep monitoring.
- later exit scans/notifications skip already-reaped records; one child exit
  produces one `exited` event, even if registration and notification both
  observe it
- Never call `waitpid(-1)`; it would reap children the application does not
  own.

Commands from the UI arrive through a UI→loop command queue, followed by a
signal on the wake FD:

- `add`: register a session and probe for an already-exited child as above
- `remove`: unregister a session; acknowledge only after any pending
  termination signal sequence (1.6) completes
- `terminate`: capture the foreground group, send SIGHUP to the termination
  targets and start the kill timer; apply the session's close-output policy
  (1.6)
- `write-pending`: a session has outgoing bytes (1.7)
- `cancel-paste`: cancel the active transaction's unsent payload without
  cutting a partly written protocol marker (1.7)
- `resume`: a session's inbound queue has room again (1.7); restore its master
  poll entry only if the PTY is still usable
- `shutdown`: stop the loop only after all sessions have been removed and
  their acknowledgements delivered (1.6); reject premature shutdown

Event handling:

- readable: run one bounded read batch as below; push `data` messages to that
  session's inbound queue, unless its explicit-close policy discards data (1.6)
- EOF: retain a pending `eof` event and remove the master from the poll set;
  keep waiting for the separate child-exit notification (1.3 #10)
- exit notification: `waitpid(pid, WNOHANG)`; on confirmed exit, record the
  child as reaped, retire its exit-notification FD and retain its status.
  Finish the output drain below before publishing `exited{status}`.
- writable: run one bounded write batch from the outgoing queue; poll
  `POLLOUT` only while bytes remain and the master is active

I/O fairness:

- visit ready sessions round-robin, retaining the cursor across `poll()`
  returns; restarting at the first FD can starve later sessions
- each read or write batch stops at 64 KiB transferred or 1 ms elapsed,
  whichever comes first, or earlier on `would_block`, EOF, error, queue-full
  or queue-empty. These are initial fixed limits, not configuration knobs.
- check the monotonic-clock budget between syscalls, including interrupted
  attempts, and cap each operation to the remaining byte allowance. The time
  budget is a cooperative bound, not a hard real-time guarantee.
- give each ready session at most one read and one write batch per round;
  service queued commands, ready exit notifications and expired termination
  deadlines before the next batch. Neither continuous reads nor writes can
  monopolize the loop.
- apply the same batching to hangup and post-exit drains. The post-exit
  100 ms active-drain allowance accumulates across batches; yielding does
  not reset it.
- after yielding, continue ready work without a sleep; level-triggered
  `poll()` reports remaining readiness again. A `would_block` or suspended
  master waits for real readiness or `resume`, not repeated empty retries.
- when idle, block in `poll()` indefinitely unless an actual termination
  deadline requires a timeout. Compute that timeout from the nearest pending
  deadline. The 1 ms work budget is not a periodic timer or idle wakeup.

Hangups and PTY poll suspension:

- `POLLHUP` or `POLLERR` drives the read path even without `POLLIN`; buffered
  output may still be present. Drain it before publishing EOF, respecting
  backpressure and the explicit-close output policy.
- a full inbound queue suspends the entire master poll entry, not just its
  `POLLIN` bit or requested event mask: `poll()` reports hangups/errors even
  when those events were not requested. Retain the FD and pending writes.
- `resume` restores eligible read/write polling and continues any hangup
  drain. `write-pending` must not bypass suspension. Explicit-close discard
  mode can resume reads without waiting for inbound queue capacity.
- Linux master slave-hangup `EIO` is EOF. `POLLHUP` followed by
  `would_block` also ends the hangup drain: no buffered bytes remain and a
  permanently asserted hangup must not cause repeated empty reads.
- terminal EOF permanently retires the master poll entry and publishes
  `eof` once, after accepted data. Pending input is abandoned with a
  session-local diagnostic; the UI disables further terminal input on EOF.
  Retain the FD for termination bookkeeping until normal removal closes it.
- `POLLERR` alone is not proof of EOF. Inspect the I/O result; unexpected
  poll/read/write failures are logged and enter session-local termination/
  reaping cleanup. Quarantine the unusable master instead of repeatedly
  polling it; `POLLNVAL` is likewise a backend failure.
- suspension, EOF and I/O failure never disable the separate pidfd/SIGCHLD
  exit path or termination timers. A child can close its tty and remain
  alive; only `waitpid()` confirms its exit.

Post-exit output drain:

- keep reading buffered PTY output after reaping; FIFO ordering alone does
  not cover bytes still in the PTY
- respect inbound backpressure and resume when the UI makes room; a full
  queue is not a reason to discard output
- finish at EOF or `would_block`; do not wait for descendants to close the
  PTY. A read error also ends draining and is logged.
- continuously writing descendants get at most 100 ms of cumulative active
  drain time, measured with a monotonic clock. Count time servicing this
  session's reads and enqueues, not queue-full waits or other sessions' work.
  At the limit, stop reading and log possible output truncation. This initial
  fixed budget must be verified by the tests below.
- finish queuing already-read data and any pending `eof` before queuing
  `exited{status}`. Retain lifecycle events in fixed-size per-session pending
  state until queue space is available; never drop them or block the shared
  loop to publish them.
- the session stays registered until the UI consumes `exited` and sends
  `remove`; reaping and UI completion are separate facts

This drain is mandatory for ordinary child exit. Explicit pane/app closure
may discard display output under the 1.6 policy; lifecycle events are never
discarded.

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
and its output drain/discard policy completed (1.4, 1.6). The loop records
reaping at once; UI completion may be delayed by backpressure. EOF on the
master FD does not end the session by itself.

`terminate()`:

Scope: normal terminal-close semantics. Target the shell's process group and
the foreground job's group, not the entire descendant tree. Background and
detached jobs are not enumerated or separately targeted and may survive;
the shell or terminal hangup may still signal them. This keeps deliberate
detachment possible without adding platform-specific process containment.

- returns at once; never blocks the UI
- session goes `Running` → `Stopping`; `Exited` when `exited` arrives
- the loop captures `tcgetpgrp(master_fd)` before sending signals: the shell
  may exit and lose the controlling-terminal metadata. The shell's initial
  group is its pid (session leader, 1.3 #3). Target only positive group IDs
  belonging to this terminal session, and deduplicate identical groups.
- send `kill(-pgid, SIGHUP)` to both verified groups
- kill timer: after the timeout (default 2 s, configurable in Stage 5), send
  `kill(-pgid, SIGKILL)` to surviving, verified target groups. The timer uses
  the `poll()` timeout; no extra thread. Shell exit does not cancel foreground
  escalation: a job ignoring SIGHUP may still be running.
- preserve target identity independently of UI delivery of `exited`.
  Revalidate original-session membership before delayed group signals; if
  identity cannot be established, skip and log rather than risk signaling a
  reused, unrelated PID/PGID. A saved number alone is not proof of identity.
- repeated `terminate` commands do not restart the deadline. The signal
  sequence finishes when the targets are gone or the final signal attempts
  complete; signaling errors are logged. Removal must not cancel it.
- reaping stays on the normal path: pidfd -> `waitpid` -> post-exit output
  drain (or explicit-close discard) -> `exited{status}`. `exited` reports the
  direct child and completion of its output policy, not completion of the
  separate foreground signal sequence.

Close-output policy:

- `general.drain_output_on_close` (default false) applies to explicit pane,
  tab, window and application closure; ordinary child exit always drains
- false: the UI consumes queued `data` without `feed()` or output-driven
  redraws. Once the loop processes `terminate`, it discards newly read data
  instead of enqueuing it and skips the post-exit drain once the child is
  reaped. This avoids invisible output work and queue-backpressure delays.
- true: keep the normal output drain and feed accepted data before consuming
  `exited`; the existing descendant-output cap still applies
- the UI and loop use the same session policy (1.2). Both settings retain and
  process `eof`, `exited` and removal acknowledgements; neither changes the
  termination timeout or reaping requirements.

Application shutdown is asynchronous:

1. enter closing state, reject new terminal creation, cancel queued/in-flight
   startup requests and mark sessions closing before issuing `terminate`
2. keep the UI event loop and I/O loop alive to apply each output policy,
   service termination deadlines, reap direct children and deliver `exited`.
   Keep consuming startup results too: a late success is registered and
   immediately closed; a failed/cancelled launch completes without a loop
   registration.
3. on each `exited`, request `remove`; wait for every removal acknowledgement,
   including completion of pending foreground signal sequences. Wait for all
   startup requests/results to resolve before stopping and joining the startup
   worker; an empty session registry alone is not enough.
4. only then send `shutdown`, stop and join the I/O thread, release remaining
   application resources and exit

The termination deadline starts SIGKILL escalation, not application exit:
SIGKILL is asynchronous. Waiting for lifecycle events must not block the UI
thread or stop queue consumption. Reaping, output-policy completion and
removal acknowledgement are required with either output setting.

## 1.7 Threading and synchronization

Ownership:

- Terminal state is owned exclusively by the UI thread.
- The I/O loop (1.4) never touches terminal state.
- No locks guard terminal state. The queues below are the only shared
  objects.

Startup (UI -> worker -> UI):

- bounded request/result queues carry immutable launch options and ownership
  handoff, not mutable terminal state
- the worker alone operates an unregistered backend; the UI attaches a
  successful result and sends `add`, after which the I/O loop owns its I/O
- close/destroy waits for outstanding startup results; a worker result never
  references a destroyed instance or UI widget

Inbound (loop → UI):

- one bounded SPSC queue per session; messages are `data`, `eof`,
  `exited{status}` and `paste_finished{status}` (1.7)
- one queue per session orders accepted output before `exited`; the
  post-exit drain (1.4) covers unread PTY output, subject to the documented
  descendant-output limit
- the UI thread drains the queues, calls `feed()` on the terminal core and
  performs all rendering and input handling; explicitly closing sessions
  with `drain_output_on_close=false` discard display data but still consume
  lifecycle events (1.6)
- backpressure: when a session's queue is full, the loop suspends its entire
  master poll entry (1.4). Unread output stays in the kernel PTY buffer, and
  eventually the child blocks on write. Pending input waits until `resume`;
  child-exit monitoring and termination timers stay active. When the UI has
  drained the queue below a low-water mark, it sends `resume`. Other sessions
  keep flowing. PTY output is never buffered without bound.

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
- encoded bytes go through the session's bounded SPSC outgoing queue, then
  the UI sends `write-pending`. Paste chunks carry bounded transaction/
  framing metadata so the loop can track partial writes and cancellation.
- the loop is the only thread that writes to the master FD
- `general.outgoing_limit_bytes` (default 1 MiB) bounds pending encoded bytes
  across the queue, loop-held short-write remainders and UI-deferred keys/
  replies. Credits return only when bytes are written or explicitly
  discarded, not merely popped from the queue. No unbounded overflow buffer.
- reserve 16 KiB of that budget for required replies/cancellation framing;
  user-key admission and the paste pump cannot consume this reserve. A full
  user-input queue must not make an ordinary small reply fail immediately.
- key actions are encoded and admitted as whole actions; if they cannot fit,
  reject with a visible busy indication before queueing any of their bytes
- required automatic replies share this bound and stay session-local. If a
  reply cannot fit, record `SessionError::reply_overflow` and visibly fail
  that session through normal termination/reaping cleanup; silent truncation
  would corrupt the protocol. Do not throw through the libtsm callback.
- new keys/replies remain behind older deferred bytes. The loop wakes the UI
  when space becomes available; the UI resumes the admitted action stream
  in FIFO order. Chunks of an already-admitted paste are not newer actions:
  finish that transaction before input deferred behind it.

Streaming paste:

- retain at most one active, owned encoded paste per session.
  `general.max_paste_bytes` defaults to 16 MiB, including framing; it is a
  separate finite limit, not part of the 1 MiB transport budget. This permits
  pastes larger than the queue while bounding their retained memory.
- reject oversized source input before capture; call `tsm_vte_paste()` once
  into a size-limited capture buffer, not once per transport chunk. Admit
  only after capture succeeds within the encoded-size limit, before sending
  any bytes. Retain the encoded image rather than a second owned source copy.
- captured libtsm-generated framing belongs to one transaction. Stream its
  bytes as queue credits become available; a stalled reader retains only
  this one bounded paste, bounded transport/deferred data and bounded metadata.
- reject a second paste with a visible busy indication until the first
  completes or is cancelled; do not accumulate paste payloads
- later keys and automatic replies are deferred within the outgoing-byte
  budget until the paste finishes; they must not appear inside its payload.
  Existing bytes precede the paste. Replies that cannot fit use the failure
  policy above, rather than interleaving or growing another buffer.
- explicit cancellation discards only unsent paste payload. Complete any
  partially written framing marker and, if bracketed paste has begun, send
  its captured closing marker before releasing later input. Already written
  bytes cannot be recalled; cancellation may wait for a writable PTY.
- the loop publishes `paste_finished{status}` (completed, cancelled or
  aborted) after its final write/discard decision. Retain this event when the
  inbound queue is full, like other lifecycle events. The UI releases the
  active payload and resumes deferred input only after completion; EOF or
  session closure aborts/releases paste state without waiting indefinitely
  to write a closing marker to a dead or closing terminal.
- closing a session never waits for paste completion before starting its
  termination deadline. Normal reaping/removal still owns backend teardown.

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
14. A child writes a known final payload and exits while its inbound queue
    is full. Verify the UI receives every byte, in order, before `exited`;
    queue-full waits must not consume the post-exit drain budget. No lifecycle
    event is dropped and another session keeps flowing.
15. A descendant continuously writes after the direct child exits. Verify
    the active-drain cap ends reading, logs possible truncation and delivers
    accepted data before `exited`. Also verify an idle descendant holding the
    PTY open does not delay completion after `would_block`.
16. A foreground job in a different process group ignores SIGHUP while the
    shell exits. Verify foreground SIGKILL escalation still occurs at the
    original deadline; early `exited` delivery and `remove` do not cancel it.
17. A background job in a separate non-foreground group and a detached
    daemon are not separately targeted. Verify surviving jobs do not prevent
    session removal; the test harness cleans them up.
18. A saved termination target no longer belongs to the original session.
    Verify delayed signaling skips it and logs the identity failure rather
    than targeting an unrelated group.
19. Repeat explicit close/shutdown with both `drain_output_on_close` values
    and a full inbound queue. False discards display data without `feed()`;
    true feeds accepted final data before `exited`. Both deliver lifecycle
    events, complete foreground escalation, reap direct children and receive
    removal acknowledgements before stopping/joining the loop. Ordinary child
    exit still preserves output when the configured close policy is false.
20. A shutdown child ignores SIGHUP. Verify the deadline sends SIGKILL but
    does not stop the loop; delayed exit notification, reaping and removal
    acknowledgement complete before application exit. Premature `shutdown`
    with registered sessions is rejected.
21. Repeatedly launch while other threads are active. Verify prepared argv,
    environment, working directory and initial size; invalid executable and
    child-setup failures return errors without child/descriptors leaking.
    Fault-inject a stalled pre-exec setup: the UI and existing sessions stay
    responsive, and cancellation wakes startup cleanup. Audit the child path
    for unsafe library calls; stress alone does not prove fork safety.
22. Close a pane/app with queued and in-flight launches, including a success
    racing cancellation. Verify each request resolves once, private failed
    children are reaped by the worker, late successes go through normal loop
    cleanup and no startup worker/result survives application teardown.
23. Force the SIGCHLD self-pipe fallback. An instant-exit child writes a known
    payload; delay `add` until its SIGCHLD notification has been consumed
    while the child is still unregistered. Without another SIGCHLD, verify
    registration reaps it, delivers the payload before exactly one `exited`
    event and permits removal. Repeat with bursts of short-lived children;
    no zombies or stuck sessions remain.
24. A child writes a final payload, closes all slave tty descriptors and
    remains alive. Verify payload-before-EOF ordering, Linux terminal `EIO`
    handling and retirement of the master poll entry without a busy loop.
    The session must not report `exited` until the child actually exits.
25. Combine a full inbound queue with `POLLHUP` and pending outgoing input.
    Verify the entire master entry is suspended, other sessions keep flowing
    and `resume` drains the remaining output before exactly one EOF event.
    No `write-pending` or later `resume` reinstates a terminally closed PTY.
    Also inject an unexpected poll/I/O failure: cleanup remains session-local
    and child reaping still completes.
26. Stream a paste larger than the outgoing-byte limit under short writes
    and `would_block`; verify exact byte order and one framing pair. Include
    deferred keys/replies, repeated paste attempts and an oversized paste.
    Retained transport/deferred bytes and the one paste stay within their
    separate limits; admission rejection emits no partial action.
27. Cancel a stalled paste, including a partly written opening/closing marker;
    verify no marker is cut and later input follows completion. Test EOF and
    app close with an active paste: memory is released and cleanup does not
    wait for the reader. Force required-reply overflow; only that session
    fails visibly, without silent truncation or unbounded buffering.
28. Keep one session continuously readable/writable while its queues are
    concurrently drained/refilled. Verify bounded batches and round-robin
    progress for other sessions; commands, child exits, paste cancellation
    and termination deadlines are serviced between batches. Include hangup
    and post-exit drains; their active-time allowance must not reset on yield.
29. With idle sessions and no deadlines, verify the loop blocks without
    periodic 1 ms wakeups. With a termination deadline, verify `poll()` uses
    the nearest actual deadline; ready work resumes without an artificial
    inter-batch delay, and `would_block` does not cause a spin.

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
- content dirty state; presentation/pixel damage remains widget-owned (2.5)
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

Use contiguous screen traversal via the `tsm_screen_draw()` callback, which
supplies cell widths and ages. `tsm_screen_draw2()` does not expose those
fields; do not use it for the age/width-based renderer.

Colours: the libtsm draw callback gives RGB per cell (truecolor included).
No palette code on our side.

Start with full redraws for correctness.

## 2.5 Dirty-region tracking

After correct full rendering works:

- distinguish content changes from pixel invalidation. libtsm ages identify
  changed cells, not every reason their displayed pixels need repainting.
- for ordinary output, use each cell's `tsm_age_t` and the age of the last
  completed paint to find content-damage regions
- exposure, first show/tab activation, resize, font/DPI/theme changes and
  viewport/alternate-screen changes force a full repaint regardless of ages;
  unchanged content may need new pixels or may occupy different coordinates
- moving, changing or removing a cursor/selection invalidates both old and
  new areas. Apply the same rule to visual-bell overlays in Stage 6 and any
  future presentation overlays; otherwise stale pixels can remain beneath
  them.
- repaint every damaged area from its background through cells and current
  overlays, including unchanged cells in that area. Ages select content
  damage; they must not suppress painting explicitly invalidated pixels.
- retain pending damage until painted. Advance the saved content age only
  after all corresponding content damage is painted, not merely after
  `feed()` or while a pane is hidden.
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

Stage 6 broadcast routing (6.1) sits between `KeyEvent` translation and each
destination's `tsm_vte_handle_keyboard()` call, never after byte encoding.

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

Encode each admitted paste once per destination through its own
`tsm_vte_paste()` into the bounded capture described in 1.7. It adds
bracketed-paste markers when that destination has mode 2004 enabled; the
transport streams that one encoded transaction, not separately framed
chunks. Broadcast shares source text, never an encoded paste (6.1).

Verify framing boundaries and mode exposure against the pinned libtsm before
implementing capture/cancellation. Do not infer framing from pasted text or
assume an unverified chunk API. Provide an explicit cancel-paste action;
shortcut configuration arrives in Stage 5.

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
  → compute positive cols/rows from the logical content rectangle and cell metrics
  → resize libtsm screen
  → PTY resize (SIGWINCH to child)
  → full redraw
```

Subtract pane chrome before converting pixels to whole cells; cell metrics
must be positive. Send a resize only when the resulting grid changes. If no
complete row or column fits, retain the last valid grid (initially the session
dimensions) and clip painting; never forward zero or negative dimensions to
libtsm or the PTY. In Stage 3, use the logical pane rectangle from 3.3, not its
visible intersection with the window. Font/DPI changes recompute layout
minimums before applying this flow.

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
- sub-cell/zero-sized widget exposure retains a positive grid without sending
  invalid libtsm/PTY resizes; unchanged grids do not trigger another resize
- scrollback tests
- unchanged-screen exposure/show, font/DPI/theme and viewport changes repaint
  correctly even when cell ages do not advance
- cursor/selection move or removal restores underlying pixels; use a test
  presentation overlay in Stage 2 and repeat with visual bell in Stage 6
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

## 3.0 Establish single-tab session ownership

Before integrating live splits, implement the single-tab subset of the
ownership model in 4.1: the window owns one `Tab`, which owns its layout and
`TerminalInstance` map. Use application-wide `TerminalId` values from the
start; introduce `TabManager` only in Stage 4.

Implement the asynchronous factory (4.3), pane cleanup (4.4) and window
shutdown barriers (4.5) in this stage, reusing Stage 1's launch/I/O lifecycle.
Those sections define shared contracts, not work deferred until Stage 4.
All initial and split terminals use the same factory and have independent
PTYs; retain starting/closing instances through the required completion and
removal acknowledgements.

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

Minimums and undersized windows:

- each leaf requires two columns by one row, plus its padding, borders and
  scrollbar. The UI supplies these pixel minimums from current cell metrics;
  the layout engine does not query FLTK
- combine subtree minimums bottom-up: along the split axis, sum both child
  minimums and the divider thickness; on the other axis, take their maximum
- allocate whole-pixel rectangles without gaps or overlaps. Clamp divider
  positions to both subtree minimums; preserve the stored split ratio when
  temporary size constraints clamp its effective position
- reject a new split visibly, before launching its terminal, if the proposed
  tree cannot fit the current content area. Split dragging/equalizing must
  respect the same minimums
- advertise the tree's minimum window size, including window/tab chrome, to
  the window manager. If a forced resize or font/DPI change still makes the
  content area too small, lay out at `max(actual, minimum)` on each axis and
  clip that logical canvas to the actual content area, anchored at top-left
- clipping never collapses/removes panes or changes their sessions. Derive
  terminal rows/columns from logical pane rectangles (2.11); clip drawing and
  mouse hit-testing to visible intersections. Recompute normally when space
  returns, without changing stored ratios

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
- nested horizontal/vertical minimum propagation includes pane chrome and
  dividers; boundary-sized layouts have no overlaps or negative rectangles
- refused splits launch no terminal; divider movement respects both minimums
- forced undersized/zero-area windows and font/DPI changes clip safely while
  logical panes and PTY grids stay positive; growing restores stored ratios
- focus navigation
- live split creation uses independent PTYs through the shared factory
- close a starting pane, including a late launch success; verify cleanup
  completes before destroying its instance/widget
- close a running pane and the single-tab window under output backpressure;
  verify reaping/removal barriers and a valid remaining layout

## Exit criteria

Stage 3 is complete when:

- arbitrary nested split layouts work
- every live pane has an independently owned terminal session
- panes resize correctly
- pane removal leaves a valid tree
- pane/window cleanup obeys the asynchronous lifecycle barriers
- all layout logic can be tested without launching the GUI

---

# Stage 4 — Tabs and Sessions

## Objective

Extend Stage 3's single-tab ownership and lifecycle foundation into a full
multi-tab application. The factory, pane cleanup and window shutdown
contracts below are already implemented in Stage 3 (3.0); reuse them.

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

A tab owns its layout and its terminal instances, as established in 3.0.
Stage 4 adds `TabManager` around that owner. The layout refers to terminals
only by `TerminalId` (3.1), never by pointer.

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

This UI-thread factory creates a `Starting` instance and queues the launch
with the startup worker (1.3); it never calls synchronous `start()` itself.
The worker privately owns the backend until its result is consumed on the
UI thread. Success attaches the backend and sends `add`; failure displays
the session error without registering a backend. Disable terminal input
until startup succeeds.

This ensures all terminal creation passes through one asynchronous path.

## 4.4 Session cleanup

Closing a terminal must:

If it is still `Starting`, cancel the request and retain the instance until
the result is consumed. Failed/cancelled launches have worker-owned cleanup;
a late success must be registered and follow the normal close steps below.

1. mark the pane closing, apply `drain_output_on_close`, call `terminate()`
   and wait asynchronously for `exited` (1.6); keep consuming its queue even
   when display data is discarded
2. send `remove` to the I/O loop and wait asynchronously for acknowledgement
   (1.4); retain termination state until the foreground signal sequence is
   finished, even if the direct child already exited
3. release terminal state
4. destroy FLTK widget
5. remove layout node
6. rebalance layout

The direct child must be reaped, the shell/foreground termination signal
sequence completed and the loop registration removed. Background/detached
descendants may survive under the 1.6 policy; this is not an all-descendants
cleanup guarantee.

## 4.5 Window lifecycle

Implement:

- clean shutdown
- close confirmation only where necessary
- terminate shell/foreground groups using the 1.6 scope
- keep lifecycle processing alive through reaping and removal acknowledgements;
  the 1.6 output setting controls display data, not cleanup
- resolve pending launches and remove all sessions before stopping/joining
  background threads (1.6)

## Tests

Test:

- multiple simultaneous shells
- multiple tabs
- independent tab layouts
- closing active/non-active tabs
- rapid create/destroy
- application shutdown with active sessions
- shutdown under output backpressure with both close-output settings; verify
  no stop/join or object destruction precedes session removal acknowledgement

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

This stage defines, validates and round-trips named profile/layout data, and
wires global/default settings and shortcuts into the existing application.
Per-pane profile selection and reconstruction of live tabs/sessions from saved
layouts belong to 6.4/6.5, not this stage.

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
  drain_output_on_close: false # 1.6: explicit close only; lifecycle always processed
  outgoing_limit_bytes: 1048576 # 1.7: queue + in-flight/deferred encoded bytes
  max_paste_bytes: 16777216     # 1.7: one retained encoded paste, framing included

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

The pinned rapidyaml's default callbacks abort on parse/visit errors unless
`RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS=ON` (0.2). Keep exceptions enabled and
catch its `ExceptionBasic` base at this application/setup boundary, returning
a clear configuration error without applying partial data. System-dependency
mode must provide the same recoverable behavior. Configuration strings must
own their data before the local YAML tree/source buffer is destroyed.

Outgoing/paste byte limits must be finite, positive and safe for framing and
allocation. The outgoing limit must be at least 32 KiB, leaving capacity
beside its 16 KiB control reserve. Validate limits before constructing
buffers; no unlimited setting.

## 5.4 Implement writer

Only write configuration when explicitly requested.

Preserve a predictable format.

Do not rewrite user configuration on every startup.

## 5.5 Implement defaults

Define all defaults in C++, not scattered throughout UI code.

## 5.6 Layout data persistence

Capture the current layout as plain `LayoutConfig` data and serialize/parse:

- tab order
- split orientation
- split ratios
- profile assignment
- working directory
- selected/focused pane

Validate tree structure, finite ratios strictly between 0 and 1, profile
references and saved focus references before accepting the data. Saved pane
references are layout-local, not live application `TerminalId` values.
Deserialization returns configuration data only: it does not create tabs,
widgets or processes, or modify the current live layout. Runtime reconstruction
is implemented in 6.5.

Do not persist transient terminal screen contents unless specifically added later.

## Tests

Test:

- empty configuration
- missing configuration
- valid configuration
- malformed YAML
- malformed YAML and conversion errors are caught without aborting the
  application or applying partial configuration, including in system mode
- loaded strings remain valid after the YAML tree/source buffer is destroyed
- unknown fields
- invalid values
- round-trip serialization
- layout serialization/deserialization
- named profile/layout round trips preserve values, assignments and focus;
  invalid trees, ratios and references are rejected without runtime creation
- outgoing/paste limits: defaults, configured values and rejection of unsafe,
  zero or overflowing sizes

## Exit criteria

Stage 5 is complete when:

- configuration is loaded deterministically
- defaults are centralized
- global/default settings and shortcuts are applied; named profile data can
  be loaded, validated and saved
- layouts can be captured and round-tripped as configuration data without
  reconstructing runtime sessions
- malformed configuration cannot crash the application

---

# Stage 6 — Terminator Feature Parity

## Objective

Close selected functional gaps against the target Terminator workflow.
Wishlist features are outside the current scope; search is listed in
[`WISHLIST.md`](WISHLIST.md), not scheduled for Stage 6.

Implement in this order.

## 6.1 Essential interaction features

- pane broadcast input
- pane zoom/maximize
- pane movement
- tab movement
- terminal renaming
- dynamic title updates beyond the basic OSC 0/2 case (e.g. OSC 7
  working-directory reporting)

Broadcast contract:

- broadcast keyboard input and explicit paste actions to the selected set of
  live, input-enabled panes, including the focused pane exactly once
- resolve application shortcuts and local UI actions before broadcast. Mouse
  reporting, selection and scrollback navigation remain local to their pane
- fan out translated `KeyEvent` values or unencoded paste text on the UI
  thread. Each destination uses its own libtsm VTE and current modes (2.6,
  2.8); never duplicate one pane's write-callback bytes to other sessions
- automatic replies generated by `feed()` remain session-local regardless of
  broadcast state, as required by 1.7
- each destination applies 1.7's admission, ordering and paste limits
  independently. A busy or closing pane does not block other destinations;
  report rejected destinations visibly. Delivery is not all-or-nothing

Broadcast regression tests:

- panes in normal/application cursor-key modes receive their respective arrow
  sequences from one key action, with no duplicate delivery to the focus
- panes with bracketed paste enabled/disabled receive independently framed
  copies of the same text, including a paste larger than the transport queue
- a cursor-position/device-attributes query in one pane produces a reply only
  in that session, even while broadcast is active
- a busy or closing destination rejects input visibly while another receives
  it in order; shortcuts, mouse events and local scrolling are not broadcast

## 6.2 Terminal functionality

- audible bell and visual bell (via the 2.8a bell callback)
- title/working-directory updates
- environment handling

Alternate screen, bracketed paste, application cursor mode and application
keypad mode are provided by libtsm and verified in Stage 2 (2.6, 2.8).

## 6.3 URL handling

Add URL detection as a presentation feature.

Do not mutate terminal state to implement URL highlighting.

## 6.4 Runtime profile support

Apply the named profile data already defined and loaded in Stage 5 to newly
created panes, including explicit profile selection and restored assignments.
Resolve it into the existing styling/session options before calling the shared
factory (4.3); do not introduce another schema or loader here. Settings cover:

- font
- colours
- shell
- environment
- cursor
- scrollback

Profile selection affects new terminals; restarting or live-reconfiguring
existing sessions is not required by this stage.

## 6.5 Layout restore

Reconstruct live application objects from validated Stage 5 `LayoutConfig`
data, on an explicit restore request. This is a new launch, not resumption of
old processes or replay of their input/output. Rebuild:

- tab count
- pane tree
- terminal profiles
- working directories where valid
- saved split ratios, selected tab and focused pane

Allocate fresh application-wide terminal IDs and map saved pane references to
them. Create every pane through 4.3's asynchronous factory and use 4.4/4.5 for
cancellation and cleanup. Apply 3.3's clipping policy if the saved tree cannot
fit the current window; do not discard saved panes just because it is smaller.

If a saved directory is unavailable, use the normal default directory and
report the fallback. If a configured shell cannot launch, attempt the normal
default shell once and report the fallback; if that also fails, retain a
visible failed pane rather than retrying indefinitely. Do not attempt to
recreate arbitrary foreground jobs.

Runtime profile/restore regression tests:

- two new panes with different named profiles receive independent styling,
  shell/environment and scrollback settings through the shared factory
- a saved multi-tab nested layout recreates ratios, profile assignments,
  directories and focus with fresh terminal IDs and independent PTYs
- missing directories and shell launch failures follow the visible fallback
  policy, including failure of the fallback shell
- restoring into an undersized window preserves every pane; closing during
  asynchronous restore obeys launch cancellation and removal barriers

## Exit criteria

Stage 6 is complete when the selected multi-tab/multi-pane workflows above
work. Wishlist features, including search, are not exit requirements.

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

## 8.6 Source distribution; binary packaging deferred

Current distribution remains source-only. Provide documented source
build/install steps for the supported platforms and preserve dependency
licenses/notices under the policy in `docs/dependencies.md`.

Native binary packaging is outside the current scope. Only after separate
approval, cross-platform build stability and LGPL binary-compliance decisions
are settled, consider:

- Linux packages/bundles
- macOS application bundle
- Windows executable/package

Keep runtime dependencies minimal and document which libraries are dynamically linked.

## Exit criteria

Stage 8 is complete when:

- portable core builds on all target platforms
- each platform has a working terminal session backend
- terminal behavior remains consistent across platforms
- source build/install instructions cover supported platforms; deferred binary
  packaging is not an exit requirement

---

# Cross-Stage Testing Strategy

## Unit tests

Test independently:

- configuration
- key mapping
- terminal state
- layout tree
- geometry calculation
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

# Wishlist

Optional features outside the current stages are tracked in
[`WISHLIST.md`](WISHLIST.md). Search and native Wayland support are
wishlist-only. No history adapter or early libtsm fork is required for search;
no native Wayland dependencies are authorized for v1.

# Deferred Work

## libtsm fork

libtsm 4.8.0 lacks the two features below. Both are planned for a fork of
libtsm in a separate repository; v1 ships without them. Integrate the fork
once the current implementation is complete, and contribute the patches
upstream (kmscon/libtsm is active; releases are current).

libtsm's own code is MIT-licensed, but its bundled hash table is LGPL-licensed;
see the source-only distribution policy in `docs/dependencies.md`.
The fork is written fresh: it follows VTE's approach, but no VTE code (LGPL-3)
is copied. Fresh feature code does not remove existing third-party obligations.

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
