// SPDX-License-Identifier: MIT

#include <platform/io_loop.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <span>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <app/log.hpp>
#include <app/spsc_ring.hpp>
#include <app/wakeup.hpp>

namespace consola {
namespace {

constexpr std::size_t kReadChunk = std::size_t{64} * 1024;
constexpr std::size_t kWriteChunk = std::size_t{64} * 1024;
constexpr auto kBatchBudget = std::chrono::milliseconds(1);
constexpr auto kDrainBudget = std::chrono::milliseconds(100);
constexpr std::size_t kCommandSlots = 64;
constexpr std::size_t kEventSlots = 64;

// Set before installing the SIGCHLD handler; the handler only writes a byte to
// the loop's self-pipe (async-signal-safe). The loop scans all known PIDs
// after the wake, so a coalesced or dropped byte loses no exit notification.
volatile sig_atomic_t g_sigchld_write_fd = -1;

extern "C" void consola_sigchld_handler(int signal_number)
{
    static_cast<void>(signal_number);
    const int fd = static_cast<int>(g_sigchld_write_fd);
    if (fd < 0) {
        return;
    }
    const char byte = 0;
    const ssize_t ignored = ::write(fd, &byte, 1);
    (void)ignored;
}

int decode_wait_status(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return status;
}

pid_t waitpid_retry(pid_t pid, int *status)
{
    pid_t result = 0;
    do {
        result = ::waitpid(pid, status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    return result;
}

struct LoopCommand {
    enum class Kind : std::uint8_t { add, remove, write_pending, resume, shutdown };

    Kind kind = Kind::add;
    TerminalSession *session = nullptr;
    SessionInboundQueue *inbound = nullptr;
    SessionOutboundQueue *outbound = nullptr;
    int master_fd = -1;
    int exit_fd = -1;
    std::int64_t process_id = -1;
    TerminalId id;
};

class LinuxIoLoop final : public IoLoop {
public:
    LinuxIoLoop(WakeCallback wake, void *wake_context);
    ~LinuxIoLoop() override;

    LinuxIoLoop(const LinuxIoLoop &) = delete;
    LinuxIoLoop &operator=(const LinuxIoLoop &) = delete;

    void start() override;
    void stop() override;
    bool add(TerminalSession &session, int master_fd, int exit_fd, std::int64_t process_id,
             TerminalId id, SessionInboundQueue &inbound, SessionOutboundQueue &outbound) override;
    bool remove(TerminalId id) override;
    bool write_pending(TerminalId id) override;
    bool resume(TerminalId id) override;
    bool shutdown() override;
    bool try_pop_event(LoopEvent &out) override;

private:
    struct Registered {
        TerminalId id;
        TerminalSession *session = nullptr;
        SessionInboundQueue *inbound = nullptr;
        SessionOutboundQueue *outbound = nullptr;
        int master_fd = -1;
        int exit_fd = -1;
        std::int64_t process_id = -1;
        bool reaped = false;
        bool exit_notified = false;
        bool eof_queued = false;
        bool master_retired = false;
        bool suspended = false;
        bool drain_capped = false;
        bool drain_complete = false;
        std::size_t write_offset = 0;
        std::chrono::steady_clock::duration drain_active{0};
        int exit_status = 0;
    };

    enum class RefKind : std::uint8_t { wake, sigchld, master, exit };
    struct PollRef {
        RefKind kind = RefKind::wake;
        Registered *session = nullptr;
    };

    void install_sigchld();
    void run();
    void drain_commands();
    void build_pollfds();
    void handle_wake() const;
    void handle_sigchld();
    void service_ready();
    void service_read(Registered &session, bool hangup, bool poll_error);
    void service_write(Registered &session);
    void service_exit(Registered &session);
    void reap(Registered &session, int status);
    void finish_exits();
    void retire_master(Registered &session);
    void queue_eof(Registered &session);
    void finish_exit_if_drained(Registered &session);
    [[nodiscard]] Registered *find(TerminalId id);
    void push_event(LoopEvent event);
    void flush_events();
    void signal_loop() const;

    WakeupFlag wakeup_;
    std::vector<Registered> sessions_;
    SpscRing<LoopCommand, kCommandSlots> commands_;
    SpscRing<LoopEvent, kEventSlots> events_;
    std::deque<LoopEvent> pending_events_;
    std::vector<struct pollfd> pollfds_;
    std::vector<PollRef> refs_;
    std::vector<std::byte> read_buffer_;
    std::size_t rotation_ = 0;
    int wake_fd_ = -1;
    int sigchld_read_ = -1;
    int sigchld_write_ = -1;
    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
    bool running_ = false;
    bool available_ = false;
    bool sigchld_installed_ = false;
    struct sigaction previous_sigchld_{};
};

LinuxIoLoop::LinuxIoLoop(WakeCallback wake, void *wake_context) : wakeup_(wake, wake_context)
{
    read_buffer_.resize(kReadChunk);

    wake_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wake_fd_ < 0) {
        log(LogLevel::Error, "failed to create I/O loop wake eventfd");
    }

    std::array<int, 2> pipe_fds{-1, -1};
    if (::pipe2(pipe_fds.data(), O_CLOEXEC | O_NONBLOCK) == 0) {
        sigchld_read_ = pipe_fds[0];
        sigchld_write_ = pipe_fds[1];
    } else {
        log(LogLevel::Error, "failed to create SIGCHLD self-pipe");
    }

    available_ = wake_fd_ >= 0 && sigchld_read_ >= 0 && sigchld_write_ >= 0;
    if (!available_) {
        log(LogLevel::Error, "I/O loop is unavailable; commands will be rejected");
    }
}

LinuxIoLoop::~LinuxIoLoop()
{
    stop();
    g_sigchld_write_fd = -1;
    if (sigchld_installed_) {
        ::sigaction(SIGCHLD, &previous_sigchld_, nullptr);
    }
    if (wake_fd_ >= 0) {
        ::close(wake_fd_);
    }
    if (sigchld_read_ >= 0) {
        ::close(sigchld_read_);
    }
    if (sigchld_write_ >= 0) {
        ::close(sigchld_write_);
    }
}

void LinuxIoLoop::install_sigchld()
{
    if (sigchld_installed_ || sigchld_write_ < 0) {
        return;
    }
    g_sigchld_write_fd = static_cast<sig_atomic_t>(sigchld_write_);
    struct sigaction action{};
    action.sa_handler = &consola_sigchld_handler;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (::sigaction(SIGCHLD, &action, &previous_sigchld_) == 0) {
        sigchld_installed_ = true;
    } else {
        g_sigchld_write_fd = -1;
        log(LogLevel::Error, "failed to install SIGCHLD handler");
    }
}

void LinuxIoLoop::start()
{
    if (running_ || !available_) {
        return;
    }
    install_sigchld();
    stop_requested_.store(false);
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void LinuxIoLoop::stop()
{
    if (!running_) {
        return;
    }
    stop_requested_.store(true);
    signal_loop();
    if (thread_.joinable()) {
        thread_.join();
    }
    running_ = false;
}

void LinuxIoLoop::signal_loop() const
{
    if (wake_fd_ < 0) {
        return;
    }
    const std::uint64_t one = 1;
    ssize_t result = 0;
    do {
        result = ::write(wake_fd_, &one, sizeof(one));
    } while (result < 0 && errno == EINTR);
}

bool LinuxIoLoop::add(TerminalSession &session, int master_fd, int exit_fd, std::int64_t process_id,
                      TerminalId id, SessionInboundQueue &inbound, SessionOutboundQueue &outbound)
{
    if (!available_) {
        return false;
    }
    LoopCommand *slot = commands_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    *slot = LoopCommand{};
    slot->kind = LoopCommand::Kind::add;
    slot->session = &session;
    slot->inbound = &inbound;
    slot->outbound = &outbound;
    slot->master_fd = master_fd;
    slot->exit_fd = exit_fd;
    slot->process_id = process_id;
    slot->id = id;
    commands_.commit_push();
    signal_loop();
    return true;
}

bool LinuxIoLoop::remove(TerminalId id)
{
    if (!available_) {
        return false;
    }
    LoopCommand *slot = commands_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    *slot = LoopCommand{};
    slot->kind = LoopCommand::Kind::remove;
    slot->id = id;
    commands_.commit_push();
    signal_loop();
    return true;
}

bool LinuxIoLoop::write_pending(TerminalId id)
{
    if (!available_) {
        return false;
    }
    LoopCommand *slot = commands_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    *slot = LoopCommand{};
    slot->kind = LoopCommand::Kind::write_pending;
    slot->id = id;
    commands_.commit_push();
    signal_loop();
    return true;
}

bool LinuxIoLoop::resume(TerminalId id)
{
    if (!available_) {
        return false;
    }
    LoopCommand *slot = commands_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    *slot = LoopCommand{};
    slot->kind = LoopCommand::Kind::resume;
    slot->id = id;
    commands_.commit_push();
    signal_loop();
    return true;
}

bool LinuxIoLoop::shutdown()
{
    if (!available_) {
        return false;
    }
    LoopCommand *slot = commands_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    *slot = LoopCommand{};
    slot->kind = LoopCommand::Kind::shutdown;
    commands_.commit_push();
    signal_loop();
    return true;
}

bool LinuxIoLoop::try_pop_event(LoopEvent &out)
{
    const LoopEvent *event = events_.front();
    if (event == nullptr) {
        return false;
    }
    out = *event;
    events_.pop();
    // The loop may be holding acknowledgements in its pending queue while this
    // ring was full; wake it so flush_events() can deliver them.
    signal_loop();
    return true;
}

LinuxIoLoop::Registered *LinuxIoLoop::find(TerminalId id)
{
    for (Registered &session : sessions_) {
        if (session.id == id) {
            return &session;
        }
    }
    return nullptr;
}

void LinuxIoLoop::push_event(LoopEvent event)
{
    pending_events_.push_back(event);
    if (pending_events_.size() > 1024) {
        log(LogLevel::Error, "I/O loop acknowledgement backlog is large");
    }
    flush_events();
}

void LinuxIoLoop::flush_events()
{
    while (!pending_events_.empty()) {
        LoopEvent *slot = events_.prepare_push();
        if (slot == nullptr) {
            break;
        }
        *slot = pending_events_.front();
        events_.commit_push();
        pending_events_.pop_front();
    }
    wakeup_.notify();
}

void LinuxIoLoop::run()
{
    while (!stop_requested_.load(std::memory_order_acquire)) {
        drain_commands();
        if (stop_requested_.load(std::memory_order_acquire)) {
            break;
        }
        // Complete any post-exit drain that a just-processed `resume` unblocked,
        // before the loop can block in poll() with no readiness to report.
        finish_exits();
        flush_events();

        build_pollfds();
        const int ready = ::poll(pollfds_.data(), pollfds_.size(), -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            log(LogLevel::Error, "I/O loop poll failed");
            break;
        }

        if (!pollfds_.empty() && (pollfds_[0].revents & POLLIN) != 0) {
            handle_wake();
        }
        if (pollfds_.size() > 1 && (pollfds_[1].revents & POLLIN) != 0) {
            handle_sigchld();
        }

        service_ready();
        finish_exits();

        if (!sessions_.empty()) {
            rotation_ = (rotation_ + 1) % sessions_.size();
        } else {
            rotation_ = 0;
        }
    }
}

void LinuxIoLoop::drain_commands()
{
    LoopCommand command;
    while (commands_.try_pop(command)) {
        switch (command.kind) {
        case LoopCommand::Kind::add: {
            Registered session;
            session.id = command.id;
            session.session = command.session;
            session.inbound = command.inbound;
            session.outbound = command.outbound;
            session.master_fd = command.master_fd;
            session.exit_fd = command.exit_fd;
            session.process_id = command.process_id;
            sessions_.push_back(session);

            // Registration-time probe (DIP 1.4): a SIGCHLD may have been
            // consumed before this session was registered.
            int status = 0;
            const auto pid = static_cast<pid_t>(command.process_id);
            const pid_t result = waitpid_retry(pid, &status);
            if (result == pid) {
                reap(sessions_.back(), status);
            }

            LoopEvent event;
            event.kind = LoopEvent::Kind::added;
            event.id = command.id;
            push_event(event);
            break;
        }
        case LoopCommand::Kind::remove: {
            const auto it = std::ranges::find_if(
                sessions_, [&](const Registered &session) { return session.id == command.id; });
            if (it != sessions_.end()) {
                sessions_.erase(it);
                if (rotation_ >= sessions_.size()) {
                    rotation_ = 0;
                }
            }
            LoopEvent event;
            event.kind = LoopEvent::Kind::removed;
            event.id = command.id;
            push_event(event);
            break;
        }
        case LoopCommand::Kind::write_pending:
            break; // The wake alone makes the loop re-evaluate POLLOUT.
        case LoopCommand::Kind::resume: {
            Registered *session = find(command.id);
            if (session != nullptr) {
                session->suspended = false;
            }
            break;
        }
        case LoopCommand::Kind::shutdown: {
            LoopEvent event;
            event.kind = LoopEvent::Kind::shutdown_done;
            if (!sessions_.empty()) {
                event.error = SessionError::not_running; // premature shutdown
            } else {
                stop_requested_.store(true, std::memory_order_release);
            }
            push_event(event);
            break;
        }
        }
    }
}

void LinuxIoLoop::build_pollfds()
{
    pollfds_.clear();
    refs_.clear();
    pollfds_.push_back({wake_fd_, POLLIN, 0});
    refs_.push_back({RefKind::wake, nullptr});
    pollfds_.push_back({sigchld_read_, POLLIN, 0});
    refs_.push_back({RefKind::sigchld, nullptr});

    const std::size_t count = sessions_.size();
    for (std::size_t index = 0; index < count; ++index) {
        Registered &session = sessions_[(rotation_ + index) % count];
        if (!session.master_retired && !session.suspended) {
            short events = POLLIN;
            if (!session.outbound->empty()) {
                events |= POLLOUT;
            }
            pollfds_.push_back({session.master_fd, events, 0});
            refs_.push_back({RefKind::master, &session});
        }
        if (!session.reaped && session.exit_fd >= 0) {
            pollfds_.push_back({session.exit_fd, POLLIN, 0});
            refs_.push_back({RefKind::exit, &session});
        }
    }
}

void LinuxIoLoop::handle_wake() const
{
    std::uint64_t value = 0;
    while (::read(wake_fd_, &value, sizeof(value)) > 0) {
    }
}

void LinuxIoLoop::handle_sigchld()
{
    std::array<char, 128> buffer{};
    while (::read(sigchld_read_, buffer.data(), buffer.size()) > 0) {
    }
    for (Registered &session : sessions_) {
        if (!session.reaped) {
            service_exit(session);
        }
    }
}

void LinuxIoLoop::service_ready()
{
    for (std::size_t index = 2; index < pollfds_.size(); ++index) {
        const short revents = pollfds_[index].revents;
        if (revents == 0) {
            continue;
        }
        Registered &session = *refs_[index].session;
        if (refs_[index].kind == RefKind::master) {
            if ((revents & POLLNVAL) != 0) {
                log(LogLevel::Warning, "session master reported POLLNVAL");
                retire_master(session);
                queue_eof(session);
                continue;
            }
            if ((revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                service_read(session, (revents & POLLHUP) != 0, (revents & POLLERR) != 0);
            }
            if (!session.master_retired && (revents & POLLOUT) != 0) {
                service_write(session);
            }
        } else if (refs_[index].kind == RefKind::exit) {
            service_exit(session);
        }
    }
}

void LinuxIoLoop::service_read(Registered &session, bool hangup, bool poll_error)
{
    if (session.master_retired) {
        return;
    }
    const bool draining = session.reaped;
    const auto start = std::chrono::steady_clock::now();
    std::size_t moved = 0;
    bool blocked = false;
    while (true) {
        if (session.inbound->data_full()) {
            session.suspended = true;
            break;
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= kBatchBudget) {
            break;
        }
        if (draining && session.drain_active + elapsed >= kDrainBudget) {
            session.drain_capped = true;
            break;
        }

        const std::size_t allowance = kReadChunk - moved;
        const std::span<std::byte> target(read_buffer_.data(),
                                          std::min(read_buffer_.size(), allowance));
        const IoResult result = session.session->read(target);
        if (result.status == IoStatus::ok && result.count > 0) {
            const std::span<const std::byte> batch(read_buffer_.data(), result.count);
            if (!session.inbound->try_push_data(batch)) {
                session.suspended = true;
                break;
            }
            wakeup_.notify();
            moved += result.count;
            if (moved >= kReadChunk) {
                break;
            }
            continue;
        }
        if (result.status == IoStatus::would_block) {
            blocked = true;
            break;
        }
        if (result.status == IoStatus::eof) {
            retire_master(session);
            queue_eof(session);
            break;
        }
        log(LogLevel::Warning, "session master read failed");
        retire_master(session);
        queue_eof(session);
        break;
    }
    if (draining) {
        session.drain_active += std::chrono::steady_clock::now() - start;
    }
    // A permanently asserted hangup/error must not keep the master in the poll
    // set producing empty reads. Once no buffered bytes remain (would_block),
    // retire the entry. POLLHUP means EOF; POLLERR alone is not proof of EOF,
    // so it is quarantined without publishing one (DIP 1.4).
    if (blocked && !session.master_retired && !session.suspended) {
        if (hangup) {
            retire_master(session);
            queue_eof(session);
        } else if (poll_error) {
            log(LogLevel::Warning, "session master poll error with no buffered data");
            retire_master(session);
        }
    }
    // Mark the post-exit drain complete only when it reached a terminal state
    // (would_block, EOF/error, or the active cap), not when a byte/time budget
    // merely yielded with output possibly still buffered.
    if (draining && !session.suspended &&
        (blocked || session.master_retired || session.drain_capped)) {
        session.drain_complete = true;
    }
}

void LinuxIoLoop::service_write(Registered &session)
{
    if (session.master_retired) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    std::size_t moved = 0;
    while (true) {
        const std::vector<std::byte> *chunk = session.outbound->front();
        if (chunk == nullptr) {
            break;
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= kBatchBudget) {
            break;
        }
        const std::size_t allowance = kWriteChunk - moved;
        const std::size_t available = chunk->size() - session.write_offset;
        const std::span<const std::byte> remaining(chunk->data() + session.write_offset,
                                                   std::min(available, allowance));
        const IoResult result = session.session->write(remaining);
        if (result.status == IoStatus::ok && result.count > 0) {
            session.write_offset += result.count;
            moved += result.count;
            if (session.write_offset == chunk->size()) {
                session.outbound->pop_front();
                session.write_offset = 0;
            }
            if (moved >= kWriteChunk) {
                break;
            }
            continue;
        }
        if (result.status == IoStatus::would_block) {
            break;
        }
        log(LogLevel::Warning, "session master write failed");
        retire_master(session);
        queue_eof(session);
        break;
    }
}

void LinuxIoLoop::service_exit(Registered &session)
{
    if (session.reaped) {
        return;
    }
    int status = 0;
    const auto pid = static_cast<pid_t>(session.process_id);
    const pid_t result = waitpid_retry(pid, &status);
    if (result == pid) {
        reap(session, status);
    } else if (result < 0 && errno != EINTR && errno != ECHILD) {
        log(LogLevel::Warning, "session waitpid failed");
    }
}

void LinuxIoLoop::reap(Registered &session, int status)
{
    session.reaped = true;
    session.exit_status = decode_wait_status(status);
    session.exit_fd = -1; // retire the exit-notification poll entry
}

void LinuxIoLoop::retire_master(Registered &session)
{
    session.master_retired = true;
}

void LinuxIoLoop::queue_eof(Registered &session)
{
    if (session.eof_queued) {
        return;
    }
    InboundMessage message;
    message.kind = InboundMessage::Kind::eof;
    if (!session.inbound->try_push_lifecycle(std::move(message))) {
        log(LogLevel::Error, "session lifecycle queue overflow (eof)");
    }
    session.eof_queued = true;
    wakeup_.notify();
}

void LinuxIoLoop::finish_exits()
{
    for (Registered &session : sessions_) {
        if (session.reaped && !session.exit_notified && !session.suspended &&
            !session.master_retired) {
            service_read(session, false, false);
        }
        finish_exit_if_drained(session);
    }
}

void LinuxIoLoop::finish_exit_if_drained(Registered &session)
{
    if (!session.reaped || session.exit_notified || session.suspended) {
        return;
    }
    if (!session.master_retired && !session.drain_complete) {
        return; // drain yielded with output possibly still buffered
    }
    if (session.drain_capped) {
        log(LogLevel::Warning, "session post-exit drain cap reached; possible truncation");
    }
    queue_eof(session);

    InboundMessage message;
    message.kind = InboundMessage::Kind::exited;
    message.exit_status = session.exit_status;
    if (!session.inbound->try_push_lifecycle(std::move(message))) {
        log(LogLevel::Error, "session lifecycle queue overflow (exited)");
    }
    session.exit_notified = true;
    session.master_retired = true;
    wakeup_.notify();
}

} // namespace

std::unique_ptr<IoLoop> make_linux_io_loop(WakeCallback wake, void *wake_context)
{
    return std::make_unique<LinuxIoLoop>(wake, wake_context);
}

} // namespace consola
