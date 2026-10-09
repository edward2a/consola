// SPDX-License-Identifier: MIT

#include "pty_session.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <app/log.hpp>

namespace consola {
namespace {

enum class SetupStage : std::uint8_t {
    setsid = 1,
    setctty = 2,
    dup = 3,
    chdir = 4,
    exec = 5,
};

struct SetupError {
    std::int32_t stage;
    std::int32_t error;
};

struct PreparedLaunch {
    std::string executable;
    std::string working_directory;
    std::vector<std::string> argument_storage;
    std::vector<char *> argv;
    std::vector<std::string> environment_storage;
    std::vector<char *> envp;
};

bool set_cloexec(int fd) noexcept
{
    int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool set_nonblocking(int fd) noexcept
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int ensure_above_stderr(int fd) noexcept
{
    if (fd < 0 || fd > STDERR_FILENO) {
        return fd;
    }
    int higher = ::fcntl(fd, F_DUPFD, STDERR_FILENO + 1);
    if (higher < 0) {
        ::close(fd);
        return -1;
    }
    ::close(fd);
    return higher;
}

const std::string *find_environment(const std::vector<std::string> &environment,
                                    std::string_view key)
{
    for (const std::string &entry : environment) {
        if (entry.size() > key.size() && entry.starts_with(key) && entry[key.size()] == '=') {
            return &entry;
        }
    }
    return nullptr;
}

std::string search_path(const std::string &name, const std::string &path,
                        const std::string &working_directory)
{
    std::size_t begin = 0;
    for (;;) {
        std::size_t end = path.find(':', begin);
        std::string directory =
            path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (directory.empty()) {
            directory = ".";
        }
        std::string base;
        if (directory.front() == '/' || working_directory.empty()) {
            base = directory;
        } else {
            base = working_directory;
            base += '/';
            base += directory;
        }
        std::string candidate = base;
        candidate += '/';
        candidate += name;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
        if (end == std::string::npos) {
            return {};
        }
        begin = end + 1;
    }
}

SessionError prepare_launch(const SessionOptions &options, PreparedLaunch &out)
{
    out.working_directory = options.working_directory;

    if (options.environment.empty()) {
        for (char **entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
            out.environment_storage.emplace_back(*entry);
        }
    } else {
        out.environment_storage = options.environment;
    }

    std::vector<std::string> filtered;
    filtered.reserve(out.environment_storage.size() + 2);
    for (std::string &entry : out.environment_storage) {
        if (entry.starts_with("TERM=") || entry.starts_with("COLORTERM=")) {
            continue;
        }
        filtered.push_back(std::move(entry));
    }
    out.environment_storage = std::move(filtered);
    if (!options.term.empty()) {
        out.environment_storage.emplace_back("TERM=");
        out.environment_storage.back() += options.term;
    }
    if (!options.colorterm.empty()) {
        out.environment_storage.emplace_back("COLORTERM=");
        out.environment_storage.back() += options.colorterm;
    }
    out.envp.reserve(out.environment_storage.size() + 1);
    for (std::string &entry : out.environment_storage) {
        out.envp.push_back(entry.data());
    }
    out.envp.push_back(nullptr);

    if (options.executable.empty()) {
        log(LogLevel::Error, "launch requested with an empty executable");
        return SessionError::exec_failed;
    }
    if (options.executable.find('/') != std::string::npos) {
        out.executable = options.executable;
    } else {
        std::string path = "/usr/local/bin:/usr/bin:/bin";
        if (const std::string *found = find_environment(out.environment_storage, "PATH")) {
            path = found->substr(std::strlen("PATH="));
        }
        out.executable = search_path(options.executable, path, out.working_directory);
        if (out.executable.empty()) {
            log(LogLevel::Error, "executable not found on PATH: " + options.executable);
            return SessionError::exec_failed;
        }
    }

    std::string argument_zero = options.executable;
    if (options.login_shell) {
        std::size_t slash = argument_zero.find_last_of('/');
        std::string base =
            slash == std::string::npos ? argument_zero : argument_zero.substr(slash + 1);
        argument_zero = "-" + base;
    }
    out.argument_storage.push_back(std::move(argument_zero));
    for (const std::string &argument : options.arguments) {
        out.argument_storage.push_back(argument);
    }
    out.argv.reserve(out.argument_storage.size() + 1);
    for (std::string &argument : out.argument_storage) {
        out.argv.push_back(argument.data());
    }
    out.argv.push_back(nullptr);

    return SessionError::none;
}

void report_setup_error(int fd, SetupStage stage, int error) noexcept
{
    SetupError record{.stage = static_cast<std::int32_t>(stage),
                      .error = static_cast<std::int32_t>(error)};
    const char *data = reinterpret_cast<const char *>(&record);
    std::size_t remaining = sizeof(record);
    while (remaining > 0) {
        ssize_t written = ::write(fd, data, remaining);
        if (written > 0) {
            data += written;
            remaining -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

[[noreturn]] void run_child(const PreparedLaunch &launch, int slave_fd, int error_fd) noexcept
{
    if (slave_fd < 0 || error_fd < 0) {
        ::_exit(127);
    }
    if (::setsid() < 0) {
        report_setup_error(error_fd, SetupStage::setsid, errno);
        ::_exit(127);
    }
    if (::ioctl(slave_fd, TIOCSCTTY, 0) != 0) {
        report_setup_error(error_fd, SetupStage::setctty, errno);
        ::_exit(127);
    }
    if (::dup2(slave_fd, STDIN_FILENO) < 0 || ::dup2(slave_fd, STDOUT_FILENO) < 0 ||
        ::dup2(slave_fd, STDERR_FILENO) < 0) {
        report_setup_error(error_fd, SetupStage::dup, errno);
        ::_exit(127);
    }
    if (slave_fd > STDERR_FILENO) {
        ::close(slave_fd);
    }

    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    ::sigaction(SIGPIPE, &action, nullptr);
    ::sigaction(SIGCHLD, &action, nullptr);

    sigset_t empty_mask;
    ::sigemptyset(&empty_mask);
    ::sigprocmask(SIG_SETMASK, &empty_mask, nullptr);

    if (!launch.working_directory.empty() && ::chdir(launch.working_directory.c_str()) != 0) {
        report_setup_error(error_fd, SetupStage::chdir, errno);
        ::_exit(127);
    }

    ::execve(launch.executable.c_str(), launch.argv.data(), launch.envp.data());
    report_setup_error(error_fd, SetupStage::exec, errno);
    ::_exit(127);
}

SessionError wait_exec_handshake(int read_fd, int cancel_fd)
{
    std::array<struct pollfd, 2> descriptors{};
    descriptors[0].fd = read_fd;
    descriptors[0].events = POLLIN;
    descriptors[0].revents = 0;
    descriptors[1].fd = cancel_fd;
    descriptors[1].events = POLLIN;
    descriptors[1].revents = 0;
    const nfds_t count = cancel_fd >= 0 ? 2 : 1;

    for (;;) {
        int ready = ::poll(descriptors.data(), count, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            log(LogLevel::Error, "poll failed while waiting for child exec");
            return SessionError::exec_failed;
        }
        if ((descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            SetupError record{};
            std::size_t received = 0;
            while (received < sizeof(record)) {
                ssize_t bytes = ::read(read_fd, reinterpret_cast<char *>(&record) + received,
                                       sizeof(record) - received);
                if (bytes > 0) {
                    received += static_cast<std::size_t>(bytes);
                    continue;
                }
                if (bytes < 0 && errno == EINTR) {
                    continue;
                }
                break;
            }
            if (received == sizeof(record)) {
                log(LogLevel::Error, "child setup failed before exec (stage " +
                                         std::to_string(record.stage) + ", errno " +
                                         std::to_string(record.error) + ")");
                return SessionError::exec_failed;
            }
            if (received == 0) {
                // Exec success closes the CLOEXEC writer. An abnormal pre-exec
                // child death also closes it; the loop's waitpid() is
                // authoritative for that case (DIP 1.3 #11).
                return SessionError::none;
            }
            log(LogLevel::Error, "incomplete child setup error record");
            return SessionError::exec_failed;
        }
        if (count == 2 && (descriptors[1].revents & POLLIN) != 0) {
            return SessionError::cancelled;
        }
    }
}

void reap_child(std::int64_t pid) noexcept
{
    if (pid <= 0) {
        return;
    }
    ::kill(static_cast<pid_t>(pid), SIGKILL);
    int status = 0;
    while (::waitpid(static_cast<pid_t>(pid), &status, 0) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
}

int open_exit_notification(std::int64_t pid) noexcept
{
#ifdef SYS_pidfd_open
    long fd = ::syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0);
    if (fd < 0) {
        return -1;
    }
    return static_cast<int>(fd);
#else
    static_cast<void>(pid);
    return -1;
#endif
}

} // namespace

LinuxPtySession::LinuxPtySession(int cancel_fd) noexcept : cancel_fd_(cancel_fd)
{}

LinuxPtySession::~LinuxPtySession()
{
    close_descriptors();
}

SessionError LinuxPtySession::start(const SessionOptions &options)
{
    if (started_) {
        return SessionError::not_running;
    }
    if (options.columns <= 0 || options.rows <= 0) {
        log(LogLevel::Error, "invalid initial terminal dimensions");
        return SessionError::pty_failed;
    }

    PreparedLaunch launch;
    SessionError prepared = prepare_launch(options, launch);
    if (prepared != SessionError::none) {
        return prepared;
    }

    struct winsize size{};
    size.ws_col = static_cast<unsigned short>(options.columns);
    size.ws_row = static_cast<unsigned short>(options.rows);

    int master = -1;
    int slave = -1;
    if (::openpty(&master, &slave, nullptr, nullptr, &size) != 0) {
        log(LogLevel::Error, "openpty failed");
        return SessionError::pty_failed;
    }
    master = ensure_above_stderr(master);
    slave = ensure_above_stderr(slave);
    if (master < 0 || slave < 0 || !set_cloexec(master) || !set_cloexec(slave) ||
        !set_nonblocking(master)) {
        log(LogLevel::Error, "failed to prepare PTY descriptors");
        if (master >= 0) {
            ::close(master);
        }
        if (slave >= 0) {
            ::close(slave);
        }
        return SessionError::pty_failed;
    }

    std::array<int, 2> error_pipe{-1, -1};
    if (::pipe2(error_pipe.data(), O_CLOEXEC) != 0) {
        log(LogLevel::Error, "pipe2 failed");
        ::close(master);
        ::close(slave);
        return SessionError::pty_failed;
    }
    error_pipe[0] = ensure_above_stderr(error_pipe[0]);
    error_pipe[1] = ensure_above_stderr(error_pipe[1]);
    if (error_pipe[0] < 0 || error_pipe[1] < 0 || !set_cloexec(error_pipe[0]) ||
        !set_cloexec(error_pipe[1])) {
        log(LogLevel::Error, "failed to prepare error pipe");
        ::close(master);
        ::close(slave);
        if (error_pipe[0] >= 0) {
            ::close(error_pipe[0]);
        }
        if (error_pipe[1] >= 0) {
            ::close(error_pipe[1]);
        }
        return SessionError::pty_failed;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        log(LogLevel::Error, "fork failed");
        ::close(master);
        ::close(slave);
        ::close(error_pipe[0]);
        ::close(error_pipe[1]);
        return SessionError::fork_failed;
    }
    if (pid == 0) {
        ::close(master);
        ::close(error_pipe[0]);
        run_child(launch, slave, error_pipe[1]);
    }

    child_pid_ = static_cast<std::int64_t>(pid);
    ::close(slave);
    ::close(error_pipe[1]);
    master_fd_ = master;

    SessionError handshake = wait_exec_handshake(error_pipe[0], cancel_fd_);
    ::close(error_pipe[0]);

    if (handshake != SessionError::none) {
        reap_child(child_pid_);
        child_pid_ = -1;
        ::close(master_fd_);
        master_fd_ = -1;
        return handshake;
    }

    exit_fd_ = open_exit_notification(pid);
    if (exit_fd_ >= 0 && !set_cloexec(exit_fd_)) {
        ::close(exit_fd_);
        exit_fd_ = -1;
    }
    if (exit_fd_ < 0) {
        log(LogLevel::Debug, "pidfd_open unavailable; using SIGCHLD fallback");
    }
    started_ = true;
    return SessionError::none;
}

IoResult LinuxPtySession::read(std::span<std::byte> buffer)
{
    if (master_fd_ < 0) {
        return {.count = 0, .status = IoStatus::error};
    }
    if (buffer.empty()) {
        return {.count = 0, .status = IoStatus::ok};
    }
    ssize_t bytes = ::read(master_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
        return {.count = static_cast<std::size_t>(bytes), .status = IoStatus::ok};
    }
    if (bytes == 0) {
        return {.count = 0, .status = IoStatus::eof};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return {.count = 0, .status = IoStatus::would_block};
    }
    if (errno == EIO) {
        return {.count = 0, .status = IoStatus::eof};
    }
    log(LogLevel::Error, "PTY master read failed");
    return {.count = 0, .status = IoStatus::error};
}

IoResult LinuxPtySession::write(std::span<const std::byte> buffer)
{
    if (master_fd_ < 0) {
        return {.count = 0, .status = IoStatus::error};
    }
    if (buffer.empty()) {
        return {.count = 0, .status = IoStatus::ok};
    }
    ssize_t bytes = ::write(master_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
        return {.count = static_cast<std::size_t>(bytes), .status = IoStatus::ok};
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return {.count = 0, .status = IoStatus::would_block};
    }
    log(LogLevel::Error, "PTY master write failed");
    return {.count = 0, .status = IoStatus::error};
}

SessionError LinuxPtySession::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0) {
        return SessionError::resize_failed;
    }
    if (master_fd_ < 0) {
        return SessionError::not_running;
    }
    struct winsize size{};
    size.ws_col = static_cast<unsigned short>(cols);
    size.ws_row = static_cast<unsigned short>(rows);
    if (::ioctl(master_fd_, TIOCSWINSZ, &size) != 0) {
        log(LogLevel::Warning, "TIOCSWINSZ failed");
        return SessionError::resize_failed;
    }
    return SessionError::none;
}

void LinuxPtySession::terminate()
{
    if (stopping_) {
        return;
    }
    stopping_ = true;
    if (child_pid_ > 0) {
        if (::kill(-static_cast<pid_t>(child_pid_), SIGHUP) != 0 && errno != ESRCH) {
            log(LogLevel::Warning, "failed to send SIGHUP to child process group");
        }
    }
}

bool LinuxPtySession::running() const
{
    return started_ && !stopping_;
}

int LinuxPtySession::master_fd() const noexcept
{
    return master_fd_;
}

int LinuxPtySession::exit_notification_fd() const noexcept
{
    return exit_fd_;
}

std::int64_t LinuxPtySession::process_id() const noexcept
{
    return child_pid_;
}

void LinuxPtySession::close_descriptors() noexcept
{
    if (master_fd_ >= 0) {
        ::close(master_fd_);
        master_fd_ = -1;
    }
    if (exit_fd_ >= 0) {
        ::close(exit_fd_);
        exit_fd_ = -1;
    }
}

} // namespace consola
