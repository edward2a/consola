// SPDX-License-Identifier: MIT

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <utility>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <app/session.hpp>
#include <platform/startup.hpp>

namespace {

using namespace std::chrono_literals;

std::atomic<int> g_wake_count{0};

void count_wake(void *context)
{
    static_cast<void>(context);
    g_wake_count.fetch_add(1, std::memory_order_relaxed);
}

bool wait_result(consola::StartupWorker &worker, consola::StartupResult &out, int timeout_ms)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.try_pop(out)) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return worker.try_pop(out);
}

bool wait_exit(std::int64_t pid, int timeout_ms)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        int status = 0;
        pid_t result = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
        if (result == static_cast<pid_t>(pid)) {
            return true;
        }
        if (result < 0 && errno != EINTR) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

bool read_until(consola::TerminalSession &session, const std::string &needle,
                std::string &collected, int timeout_ms)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::array<std::byte, 4096> buffer{};
    while (std::chrono::steady_clock::now() < deadline) {
        if (collected.find(needle) != std::string::npos) {
            return true;
        }
        consola::IoResult result = session.read(buffer);
        if (result.status == consola::IoStatus::ok && result.count > 0) {
            collected.append(reinterpret_cast<const char *>(buffer.data()), result.count);
            continue;
        }
        if (result.status == consola::IoStatus::eof) {
            return collected.find(needle) != std::string::npos;
        }
        if (result.status == consola::IoStatus::error) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return collected.find(needle) != std::string::npos;
}

bool read_until_eof(consola::TerminalSession &session, int timeout_ms)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::array<std::byte, 4096> buffer{};
    while (std::chrono::steady_clock::now() < deadline) {
        consola::IoResult result = session.read(buffer);
        if (result.status == consola::IoStatus::eof) {
            return true;
        }
        if (result.status == consola::IoStatus::error) {
            return false;
        }
        if (result.status == consola::IoStatus::would_block) {
            std::this_thread::sleep_for(1ms);
        }
    }
    return false;
}

bool write_all(consola::TerminalSession &session, const std::string &data)
{
    std::span<const std::byte> pending(reinterpret_cast<const std::byte *>(data.data()),
                                       data.size());
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!pending.empty()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        consola::IoResult result = session.write(pending);
        if (result.status == consola::IoStatus::ok && result.count > 0) {
            pending = pending.subspan(result.count);
            continue;
        }
        if (result.status == consola::IoStatus::would_block) {
            std::this_thread::sleep_for(1ms);
            continue;
        }
        return false;
    }
    return true;
}

int count_open_fds()
{
    DIR *directory = ::opendir("/proc/self/fd");
    if (directory == nullptr) {
        return -1;
    }
    int count = 0;
    while (::readdir(directory) != nullptr) {
        ++count;
    }
    ::closedir(directory);
    return count;
}

bool test_echo_and_eof()
{
    consola::StartupWorker worker(&count_wake, nullptr);
    consola::LaunchRequest request;
    request.id.value = 1;
    request.options.executable = "/bin/echo";
    request.options.arguments = {"hello-world"};
    if (!worker.submit(std::move(request))) {
        return false;
    }
    consola::StartupResult result;
    if (!wait_result(worker, result, 5000)) {
        return false;
    }
    if (result.error != consola::SessionError::none || !result.session) {
        return false;
    }
    if (result.process_id <= 0 || result.master_fd < 0) {
        return false;
    }
    if (result.session->resize(0, 0) != consola::SessionError::resize_failed) {
        return false;
    }
    if (result.session->resize(100, 40) != consola::SessionError::none) {
        return false;
    }
    std::string collected;
    if (!read_until(*result.session, "hello-world", collected, 5000)) {
        return false;
    }
    if (!read_until_eof(*result.session, 5000)) {
        return false;
    }
    if (!wait_exit(result.process_id, 5000)) {
        return false;
    }
    result.session.reset();
    worker.stop();
    return true;
}

bool test_shell_io()
{
    consola::StartupWorker worker(nullptr, nullptr);
    consola::LaunchRequest request;
    request.id.value = 2;
    request.options.executable = "/bin/cat";
    if (!worker.submit(std::move(request))) {
        return false;
    }
    consola::StartupResult result;
    if (!wait_result(worker, result, 5000)) {
        return false;
    }
    if (result.error != consola::SessionError::none || !result.session) {
        return false;
    }
    if (!write_all(*result.session, "ping-pong\n")) {
        return false;
    }
    std::string collected;
    bool echoed = read_until(*result.session, "ping-pong", collected, 5000);
    result.session->terminate();
    bool exited = wait_exit(result.process_id, 5000);
    result.session.reset();
    worker.stop();
    return echoed && exited;
}

bool test_exec_failed()
{
    consola::StartupWorker worker(nullptr, nullptr);

    consola::LaunchRequest absolute;
    absolute.id.value = 3;
    absolute.options.executable = "/nonexistent/consola-not-a-program";
    if (!worker.submit(std::move(absolute))) {
        return false;
    }
    consola::StartupResult first;
    if (!wait_result(worker, first, 5000)) {
        return false;
    }
    if (first.error != consola::SessionError::exec_failed || first.session) {
        return false;
    }

    consola::LaunchRequest search;
    search.id.value = 4;
    search.options.executable = "consola-not-on-path-xyz";
    if (!worker.submit(std::move(search))) {
        return false;
    }
    consola::StartupResult second;
    if (!wait_result(worker, second, 5000)) {
        return false;
    }
    worker.stop();
    return second.error == consola::SessionError::exec_failed && !second.session;
}

bool test_invalid_dimensions()
{
    consola::StartupWorker worker(nullptr, nullptr);
    consola::LaunchRequest request;
    request.id.value = 5;
    request.options.executable = "/bin/echo";
    request.options.columns = 0;
    if (!worker.submit(std::move(request))) {
        return false;
    }
    consola::StartupResult result;
    if (!wait_result(worker, result, 5000)) {
        return false;
    }
    worker.stop();
    return result.error == consola::SessionError::pty_failed && !result.session;
}

bool test_terminate_long_running()
{
    consola::StartupWorker worker(nullptr, nullptr);
    consola::LaunchRequest request;
    request.id.value = 6;
    request.options.executable = "/bin/sleep";
    request.options.arguments = {"30"};
    if (!worker.submit(std::move(request))) {
        return false;
    }
    consola::StartupResult result;
    if (!wait_result(worker, result, 5000)) {
        return false;
    }
    if (result.error != consola::SessionError::none || !result.session) {
        return false;
    }
    if (!result.session->running()) {
        return false;
    }
    result.session->terminate();
    if (result.session->running()) {
        return false;
    }
    bool exited = wait_exit(result.process_id, 5000);
    result.session.reset();
    worker.stop();
    return exited;
}

bool test_stop_resolves_all()
{
    consola::StartupWorker worker(nullptr, nullptr);
    constexpr int kRequests = 16;
    for (int index = 0; index < kRequests; ++index) {
        consola::LaunchRequest request;
        request.id.value = static_cast<std::uint64_t>(index) + 100;
        request.options.executable = "/bin/echo";
        request.options.arguments = {"x"};
        if (!worker.submit(std::move(request))) {
            return false;
        }
    }
    worker.stop();

    int resolved = 0;
    consola::StartupResult result;
    while (worker.try_pop(result)) {
        ++resolved;
        if (result.session) {
            wait_exit(result.process_id, 5000);
        }
    }
    return resolved == kRequests;
}

bool test_no_descriptor_leak()
{
    consola::StartupWorker worker(nullptr, nullptr);
    int before = count_open_fds();
    for (int index = 0; index < 10; ++index) {
        consola::LaunchRequest request;
        request.id.value = static_cast<std::uint64_t>(index) + 200;
        request.options.executable = "/bin/echo";
        request.options.arguments = {"leak"};
        if (!worker.submit(std::move(request))) {
            return false;
        }
        consola::StartupResult result;
        if (!wait_result(worker, result, 5000)) {
            return false;
        }
        if (result.error != consola::SessionError::none || !result.session) {
            return false;
        }
        read_until_eof(*result.session, 5000);
        wait_exit(result.process_id, 5000);
        result.session.reset();
    }
    worker.stop();
    int after = count_open_fds();
    if (before < 0 || after < 0) {
        return false;
    }
    return after <= before + 2;
}

bool test_cancel_before_dequeue()
{
    consola::StartupWorker worker(nullptr, nullptr);
    worker.cancel(consola::TerminalId{42});
    consola::LaunchRequest request;
    request.id.value = 42;
    request.options.executable = "/bin/echo";
    if (!worker.submit(std::move(request))) {
        return false;
    }
    consola::StartupResult result;
    if (!wait_result(worker, result, 5000)) {
        return false;
    }
    worker.stop();
    return result.error == consola::SessionError::cancelled && !result.session;
}

bool test_cancel_unknown()
{
    consola::StartupWorker worker(nullptr, nullptr);
    worker.cancel(consola::TerminalId{999});
    consola::LaunchRequest request;
    request.id.value = 7;
    request.options.executable = "/bin/echo";
    request.options.arguments = {"ok"};
    if (!worker.submit(std::move(request))) {
        return false;
    }
    consola::StartupResult result;
    if (!wait_result(worker, result, 5000)) {
        return false;
    }
    bool ok = result.error == consola::SessionError::none && result.session != nullptr;
    if (result.session) {
        read_until_eof(*result.session, 5000);
        wait_exit(result.process_id, 5000);
        result.session.reset();
    }
    worker.stop();
    return ok;
}

bool test_stop_with_full_results()
{
    consola::StartupWorker worker(nullptr, nullptr);
    constexpr int kRequests = 80;
    int submitted = 0;
    for (int index = 0; index < kRequests; ++index) {
        consola::LaunchRequest request;
        request.id.value = static_cast<std::uint64_t>(index) + 300;
        request.options.executable = "/bin/echo";
        request.options.arguments = {"fill"};
        if (worker.submit(std::move(request))) {
            ++submitted;
        }
    }
    std::this_thread::sleep_for(200ms);
    worker.stop();

    int resolved = 0;
    consola::StartupResult result;
    while (worker.try_pop(result)) {
        ++resolved;
        if (result.session) {
            wait_exit(result.process_id, 5000);
        }
    }
    return submitted > 0 && resolved == submitted;
}

bool test_relative_path_resolution()
{
    const std::string base = "consola-path-test";
    const std::string work = base + "/work";
    const std::string bin = work + "/bin";
    const std::string script = bin + "/greet.sh";

    ::mkdir(base.c_str(), 0700);
    ::mkdir(work.c_str(), 0700);
    ::mkdir(bin.c_str(), 0700);

    int fd = ::open(script.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd < 0) {
        return false;
    }
    const char *contents = "#!/bin/sh\necho path-ok\n";
    std::size_t length = std::strlen(contents);
    ssize_t written = ::write(fd, contents, length);
    ::close(fd);
    if (!std::cmp_equal(written, length)) {
        return false;
    }
    ::chmod(script.c_str(), 0700);

    consola::StartupWorker worker(nullptr, nullptr);
    consola::LaunchRequest request;
    request.id.value = 500;
    request.options.executable = "greet.sh";
    request.options.working_directory = work;
    request.options.environment = {"PATH=bin"};
    if (!worker.submit(std::move(request))) {
        return false;
    }
    consola::StartupResult result;
    if (!wait_result(worker, result, 5000)) {
        return false;
    }
    bool ok = result.error == consola::SessionError::none && result.session != nullptr;
    std::string collected;
    if (ok) {
        ok = read_until(*result.session, "path-ok", collected, 5000);
    }
    if (result.session) {
        result.session->terminate();
        wait_exit(result.process_id, 5000);
        result.session.reset();
    }
    worker.stop();

    ::unlink(script.c_str());
    ::rmdir(bin.c_str());
    ::rmdir(work.c_str());
    ::rmdir(base.c_str());
    return ok;
}

bool test_term_environment()
{
    consola::StartupWorker worker(nullptr, nullptr);
    consola::LaunchRequest request;
    request.id.value = 501;
    request.options.executable = "/bin/sh";
    request.options.arguments = {"-c", "echo \"$TERM|$COLORTERM\""};
    request.options.environment = {"PATH=/bin:/usr/bin"};
    request.options.term = "consola-test-term";
    request.options.colorterm = "consola-test-color";
    if (!worker.submit(std::move(request))) {
        return false;
    }
    consola::StartupResult result;
    if (!wait_result(worker, result, 5000)) {
        return false;
    }
    bool ok = result.error == consola::SessionError::none && result.session != nullptr;
    std::string collected;
    if (ok) {
        ok = read_until(*result.session, "consola-test-term|consola-test-color", collected, 5000);
    }
    if (result.session) {
        result.session->terminate();
        wait_exit(result.process_id, 5000);
        result.session.reset();
    }
    worker.stop();
    return ok;
}

} // namespace

int main()
{
    struct Case {
        const char *name;
        bool (*run)();
    };
    std::array<Case, 12> cases{{
        {.name = "echo_and_eof", .run = &test_echo_and_eof},
        {.name = "shell_io", .run = &test_shell_io},
        {.name = "exec_failed", .run = &test_exec_failed},
        {.name = "invalid_dimensions", .run = &test_invalid_dimensions},
        {.name = "terminate_long_running", .run = &test_terminate_long_running},
        {.name = "stop_resolves_all", .run = &test_stop_resolves_all},
        {.name = "no_descriptor_leak", .run = &test_no_descriptor_leak},
        {.name = "cancel_before_dequeue", .run = &test_cancel_before_dequeue},
        {.name = "cancel_unknown", .run = &test_cancel_unknown},
        {.name = "stop_with_full_results", .run = &test_stop_with_full_results},
        {.name = "relative_path_resolution", .run = &test_relative_path_resolution},
        {.name = "term_environment", .run = &test_term_environment},
    }};

    int failures = 0;
    for (const Case &test_case : cases) {
        if (!test_case.run()) {
            std::fprintf(stderr, "FAIL: %s\n", test_case.name);
            ++failures;
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
