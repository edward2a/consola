// SPDX-License-Identifier: MIT

// Repeatable benchmarks for Stage 7 (DIP 7.1, 7.2). Every workload is
// deterministic: the same bytes are generated for every run and the PTY
// workload re-executes this binary in --emit mode instead of depending on
// external tools. Results are printed as key=value lines so the shell wrapper
// can record them without parsing prose.

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <time.h> // NOLINT(modernize-deprecated-headers): POSIX clock_gettime
#include <unistd.h>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include <app/emulator.hpp>
#include <app/session.hpp>
#include <app/terminal_manager.hpp>
#include <app/terminal_style.hpp>
#include <ui/terminal_widget.hpp>

namespace {

using consola::SessionOptions;
using consola::TerminalEmulator;
using consola::TerminalId;
using consola::TerminalManager;
using consola::TerminalOptions;
using consola::TerminalState;
using consola::TerminalWidget;

constexpr std::size_t kChunkBytes = std::size_t{64} * 1024;
constexpr std::size_t kMegabyte = std::size_t{1024} * 1024;

std::uint64_t g_random = 0x9e3779b97f4a7c15ULL;

std::uint64_t next_random()
{
    // xorshift64*: deterministic, no allocation, good enough for byte patterns.
    g_random ^= g_random >> 12;
    g_random ^= g_random << 25;
    g_random ^= g_random >> 27;
    return g_random * 0x2545f4914f6cdd1dULL;
}

// A representative terminal stream: coloured words, cursor movement, scrolling
// text and the occasional erase. The pattern is stable across runs.
std::string make_chunk(std::size_t bytes)
{
    static constexpr std::array<std::string_view, 26> words = {
        "alpha ",  "bravo ",   "charlie ", "delta ",  "echo ",   "foxtrot ", "golf ",
        "hotel ",  "india ",   "juliet ",  "kilo ",   "lima ",   "mike ",    "november ",
        "oscar ",  "papa ",    "quebec ",  "romeo ",  "sierra ", "tango ",   "uniform ",
        "victor ", "whiskey ", "xray ",    "yankee ", "zulu ",
    };
    std::string out;
    out.reserve(bytes + 128);
    unsigned line = 0;
    while (out.size() < bytes) {
        if ((line % 37U) == 0U) {
            out += "\x1b[H"; // home
        } else if ((line % 53U) == 0U) {
            out += "\x1b[2J"; // clear
        }
        out += "\x1b[";
        out += std::to_string(30 + static_cast<int>(next_random() % 8));
        out += 'm';
        for (int word = 0; word < 12; ++word) {
            out += words[next_random() % std::size(words)];
        }
        out += "\x1b[0m\r\n";
        ++line;
    }
    out.resize(bytes);
    return out;
}

std::vector<std::byte> as_bytes(const std::string &text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::optional<long> read_vm_rss_kib()
{
    std::ifstream status("/proc/self/status");
    if (!status) {
        return std::nullopt;
    }
    std::string line;
    while (std::getline(status, line)) {
        if (!line.starts_with("VmRSS:")) {
            continue;
        }
        long value = 0;
        const char *begin = line.data() + 6;
        const char *end = line.data() + line.size();
        while (begin != end && (*begin == ' ' || *begin == '\t')) {
            ++begin;
        }
        if (std::from_chars(begin, end, value).ec == std::errc{}) {
            return value;
        }
    }
    return std::nullopt;
}

std::size_t count_entries(const char *path)
{
    std::error_code error;
    std::size_t count = 0;
    for (std::filesystem::directory_iterator it(path, error), end; !error && it != end;
         it.increment(error)) {
        ++count;
    }
    return count;
}

double process_cpu_seconds()
{
    timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) {
        return 0.0;
    }
    return static_cast<double>(value.tv_sec) + (static_cast<double>(value.tv_nsec) / 1e9);
}

double now_seconds()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

std::size_t parse_size(std::string_view text, std::size_t fallback)
{
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} ? value : fallback;
}

std::string self_path()
{
    std::error_code error;
    const auto target = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) {
        return target.string();
    }
    return {};
}

bool wait_for(const std::function<bool()> &predicate, TerminalManager &manager, double timeout)
{
    const double deadline = now_seconds() + timeout;
    while (now_seconds() < deadline) {
        manager.pump();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    manager.pump();
    return predicate();
}

// --emit child mode: write the same deterministic stream to stdout.
int emit_stream(std::size_t megabytes)
{
    const std::string chunk = make_chunk(kChunkBytes);
    std::size_t remaining = megabytes * kMegabyte;
    while (remaining > 0) {
        const std::size_t amount = std::min(remaining, chunk.size());
        std::size_t offset = 0;
        while (offset < amount) {
            const ssize_t written = ::write(STDOUT_FILENO, chunk.data() + offset, amount - offset);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return 1;
            }
            offset += static_cast<std::size_t>(written);
        }
        remaining -= amount;
    }
    return 0;
}

void run_parser(std::size_t megabytes)
{
    TerminalEmulator emulator;
    emulator.resize(160, 48);
    emulator.set_max_scrollback(1000);
    const std::vector<std::byte> chunk = as_bytes(make_chunk(kChunkBytes));

    const double start = now_seconds();
    std::size_t remaining = megabytes * kMegabyte;
    while (remaining > 0) {
        const std::size_t amount = std::min(remaining, chunk.size());
        emulator.feed(std::span<const std::byte>(chunk.data(), amount));
        remaining -= amount;
    }
    const double elapsed = now_seconds() - start;
    const auto mb = static_cast<double>(megabytes);
    std::printf("parser_mb=%zu\n", megabytes);
    std::printf("parser_seconds=%.6f\n", elapsed);
    std::printf("parser_mb_s=%.2f\n", mb / elapsed);
}

void run_pty(std::size_t megabytes)
{
    const std::string self = self_path();
    if (self.empty()) {
        std::fprintf(stderr, "pty: cannot resolve /proc/self/exe\n");
        std::exit(EXIT_FAILURE);
    }

    const std::optional<long> rss_before = read_vm_rss_kib();
    TerminalManager manager(nullptr, nullptr, std::chrono::milliseconds{2000});
    manager.start();
    TerminalOptions options;
    options.session.executable = self;
    options.session.arguments = {"--emit", std::to_string(megabytes)};
    options.session.columns = 160;
    options.session.rows = 48;
    options.session.term = "xterm-256color";
    const auto id = manager.create_root_terminal(options);
    if (!id.has_value()) {
        std::fprintf(stderr, "pty: could not create terminal\n");
        std::exit(EXIT_FAILURE);
    }

    const bool running = wait_for(
        [&] {
            return manager.find(*id) != nullptr &&
                   manager.find(*id)->state() == TerminalState::running;
        },
        manager, 5.0);
    if (!running) {
        std::fprintf(stderr, "pty: terminal did not start\n");
        manager.shutdown();
        std::exit(EXIT_FAILURE);
    }

    const double start = now_seconds();
    const bool done = wait_for(
        [&] {
            auto *instance = manager.find(*id);
            return instance == nullptr ||
                   (instance->state() == TerminalState::exited && instance->inbound().empty());
        },
        manager, 120.0);
    const double elapsed = now_seconds() - start;
    manager.shutdown();
    const std::optional<long> rss_after = read_vm_rss_kib();
    if (!done) {
        std::fprintf(stderr, "pty: timed out\n");
        std::exit(EXIT_FAILURE);
    }
    const auto mb = static_cast<double>(megabytes);
    std::printf("pty_mb=%zu\n", megabytes);
    std::printf("pty_seconds=%.6f\n", elapsed);
    std::printf("pty_mb_s=%.2f\n", mb / elapsed);
    if (rss_before.has_value() && rss_after.has_value()) {
        std::printf("pty_rss_before_kib=%ld\n", rss_before.value());
        std::printf("pty_rss_after_kib=%ld\n", rss_after.value());
        std::printf("pty_rss_delta_kib=%ld\n", rss_after.value() - rss_before.value());
    }
}

void run_startup()
{
    constexpr int kRuns = 5;
    std::array<double, kRuns> samples{};
    for (int run = 0; run < kRuns; ++run) {
        const double start = now_seconds();
        TerminalManager manager(nullptr, nullptr, std::chrono::milliseconds{2000});
        manager.start();
        TerminalOptions options;
        options.session.executable = "/bin/sleep";
        options.session.arguments = {"1"};
        const auto id = manager.create_root_terminal(options);
        if (!id.has_value()) {
            std::fprintf(stderr, "startup: could not create terminal\n");
            std::exit(EXIT_FAILURE);
        }
        const bool running = wait_for(
            [&] {
                auto *instance = manager.find(*id);
                return instance != nullptr && instance->state() == TerminalState::running;
            },
            manager, 5.0);
        samples[static_cast<std::size_t>(run)] = now_seconds() - start;
        manager.shutdown();
        if (!running) {
            std::fprintf(stderr, "startup: terminal did not start\n");
            std::exit(EXIT_FAILURE);
        }
    }
    std::ranges::sort(samples);
    // Five managers in one process: this is warm-process startup latency
    // (allocator and page cache already exercised), not cold process startup.
    std::printf("startup_runs=%d\n", kRuns);
    std::printf("startup_warm_min_ms=%.3f\n", samples.front() * 1000.0);
    std::printf("startup_warm_median_ms=%.3f\n",
                samples[static_cast<std::size_t>(kRuns / 2)] * 1000.0);
    std::printf("startup_warm_max_ms=%.3f\n", samples.back() * 1000.0);
}

void run_memory(std::size_t terminals)
{
    TerminalManager manager(nullptr, nullptr, std::chrono::milliseconds{2000});
    manager.start();
    const std::optional<long> baseline = read_vm_rss_kib();
    std::vector<TerminalId> ids;
    ids.reserve(terminals);
    for (std::size_t index = 0; index < terminals; ++index) {
        if (index > 0) {
            manager.create_tab();
        }
        TerminalOptions options;
        options.session.executable = "/bin/sleep";
        options.session.arguments = {"30"};
        options.session.columns = 80;
        options.session.rows = 24;
        const auto id = manager.create_root_terminal(options);
        if (!id.has_value()) {
            std::fprintf(stderr, "memory: could not create terminal %zu\n", index);
            manager.shutdown();
            std::exit(EXIT_FAILURE);
        }
        ids.push_back(*id);
    }
    const bool running = wait_for(
        [&] {
            return std::ranges::all_of(ids, [&](TerminalId id) {
                const auto *instance = manager.find(id);
                return instance != nullptr && instance->state() == TerminalState::running;
            });
        },
        manager, 15.0);
    if (!running) {
        std::fprintf(stderr, "memory: terminals did not start\n");
        manager.shutdown();
        std::exit(EXIT_FAILURE);
    }
    const std::optional<long> after = read_vm_rss_kib();
    manager.shutdown();

    std::printf("memory_terminals=%zu\n", terminals);
    if (baseline.has_value() && after.has_value()) {
        const long delta = after.value() - baseline.value();
        std::printf("memory_base_kib=%ld\n", baseline.value());
        std::printf("memory_rss_kib=%ld\n", after.value());
        std::printf("memory_delta_kib=%ld\n", delta);
        std::printf("memory_per_terminal_kib=%.1f\n",
                    static_cast<double>(delta) / static_cast<double>(terminals));
    } else {
        std::printf("memory_rss_kib=unavailable\n");
    }
}

void run_churn(std::size_t cycles, std::size_t parallel)
{
    TerminalManager manager(nullptr, nullptr, std::chrono::milliseconds{200});
    manager.start();
    const std::size_t fds_before = count_entries("/proc/self/fd");
    const std::size_t threads_before = count_entries("/proc/self/task");
    const std::optional<long> rss_before = read_vm_rss_kib();
    long peak_kib = rss_before.value_or(0);
    const double start = now_seconds();

    for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
        std::vector<TerminalId> ids;
        ids.reserve(parallel);
        for (std::size_t index = 0; index < parallel; ++index) {
            if (index > 0) {
                manager.create_tab();
            }
            TerminalOptions options;
            options.session.executable = "/bin/sleep";
            options.session.arguments = {"30"};
            const auto id = manager.create_root_terminal(options);
            if (!id.has_value()) {
                std::fprintf(stderr, "churn: could not create terminal\n");
                manager.shutdown();
                std::exit(EXIT_FAILURE);
            }
            ids.push_back(*id);
        }
        const bool running = wait_for(
            [&] {
                return std::ranges::all_of(ids, [&](TerminalId id) {
                    const auto *instance = manager.find(id);
                    return instance != nullptr && instance->state() == TerminalState::running;
                });
            },
            manager, 15.0);
        if (!running) {
            std::fprintf(stderr, "churn: terminals did not start\n");
            manager.shutdown();
            std::exit(EXIT_FAILURE);
        }
        // Sample while the terminals are alive; reading only after removal
        // would miss transient per-terminal memory.
        if (const auto rss = read_vm_rss_kib(); rss.has_value()) {
            peak_kib = std::max(peak_kib, rss.value());
        }
        for (const TerminalId id : ids) {
            manager.close_terminal(id);
        }
        while (manager.tabs().count() > 1) {
            manager.close_tab(manager.tabs().active_id());
            wait_for([&] { return manager.tabs().count() == 1; }, manager, 5.0);
        }
        if (manager.tabs().count() != 1) {
            std::fprintf(stderr, "churn: tab cleanup failed\n");
            manager.shutdown();
            std::exit(EXIT_FAILURE);
        }
        const bool removed = wait_for([&] { return manager.terminal_count() == 0; }, manager, 10.0);
        if (!removed) {
            std::fprintf(stderr, "churn: terminals were not removed\n");
            manager.shutdown();
            std::exit(EXIT_FAILURE);
        }
        if (const auto rss = read_vm_rss_kib(); rss.has_value()) {
            peak_kib = std::max(peak_kib, rss.value());
        }
    }

    const double elapsed = now_seconds() - start;
    const std::size_t fds_after = count_entries("/proc/self/fd");
    const std::size_t threads_after = count_entries("/proc/self/task");
    manager.shutdown();
    const std::size_t fds_final = count_entries("/proc/self/fd");
    const std::size_t threads_final = count_entries("/proc/self/task");

    std::printf("churn_cycles=%zu\n", cycles);
    std::printf("churn_parallel=%zu\n", parallel);
    std::printf("churn_seconds=%.3f\n", elapsed);
    std::printf("churn_fd_before=%zu\n", fds_before);
    std::printf("churn_fd_after=%zu\n", fds_after);
    std::printf("churn_fd_final=%zu\n", fds_final);
    std::printf("churn_thread_before=%zu\n", threads_before);
    std::printf("churn_thread_after=%zu\n", threads_after);
    std::printf("churn_thread_final=%zu\n", threads_final);
    if (rss_before.has_value()) {
        std::printf("churn_rss_before_kib=%ld\n", rss_before.value());
        std::printf("churn_rss_peak_kib=%ld\n", peak_kib);
    }
    if (fds_after > fds_before || fds_final > fds_before) {
        std::fprintf(stderr, "churn: descriptor leak detected\n");
        std::exit(EXIT_FAILURE);
    }
    if (threads_after > threads_before || threads_final > 1) {
        std::fprintf(stderr, "churn: thread leak detected\n");
        std::exit(EXIT_FAILURE);
    }
}

void run_scrollback(std::size_t lines)
{
    // Build the input before the baseline and keep it alive so the delta only
    // covers the emulator's scrollback, not the harness's own input buffer.
    std::string text;
    text.reserve((lines + 16) * 82);
    for (std::size_t line = 0; line < lines; ++line) {
        text += "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        text += "0123456789abcdefghij\r\n";
    }
    const std::optional<long> baseline = read_vm_rss_kib();
    TerminalEmulator emulator;
    emulator.resize(160, 48);
    emulator.set_max_scrollback(static_cast<int>(lines));
    const double start = now_seconds();
    emulator.feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte *>(text.data()), text.size()));
    const double elapsed = now_seconds() - start;
    const std::optional<long> after = read_vm_rss_kib();

    std::printf("scrollback_lines=%zu\n", lines);
    std::printf("scrollback_feed_seconds=%.6f\n", elapsed);
    const int stored = emulator.scrollback_line_count();
    std::printf("scrollback_stored=%d\n", stored);
    if (std::cmp_greater(stored, lines)) {
        std::fprintf(stderr, "scrollback: stored %d lines exceeds the %zu-line bound\n", stored,
                     lines);
        std::exit(EXIT_FAILURE);
    }
    if (baseline.has_value() && after.has_value()) {
        const long delta = after.value() - baseline.value();
        std::printf("scrollback_delta_kib=%ld\n", delta);
        std::printf("scrollback_bytes_per_line=%.1f",
                    static_cast<double>(delta) * 1024.0 / static_cast<double>(lines));
        std::printf("\n");
    }
}

void run_resize(std::size_t count)
{
    TerminalEmulator emulator;
    emulator.resize(80, 24);
    const std::string text = make_chunk(std::size_t{16} * 1024);
    emulator.feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte *>(text.data()), text.size()));
    const double start = now_seconds();
    for (std::size_t index = 0; index < count; ++index) {
        if ((index % 2U) == 0U) {
            emulator.resize(160, 48);
        } else {
            emulator.resize(80, 24);
        }
    }
    const double elapsed = now_seconds() - start;
    std::printf("resize_count=%zu\n", count);
    std::printf("resize_seconds=%.6f\n", elapsed);
    std::printf("resize_us_per_op=%.3f\n", elapsed * 1e6 / static_cast<double>(count));
}

void run_redraw(std::size_t count)
{
    Fl_Window window(800, 480);
    TerminalEmulator emulator;
    TerminalWidget widget(0, 0, 800, 480, emulator);
    window.add(&widget);
    window.end();
    window.show();
    Fl::flush();
    Fl::wait(0.05);
    Fl::flush();
    window.make_current();
    if (window.shown() == 0) {
        std::fprintf(stderr, "redraw: window did not show\n");
        std::exit(EXIT_FAILURE);
    }

    const std::string fill = make_chunk(static_cast<std::size_t>(emulator.rows()) * 82);
    emulator.feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte *>(fill.data()), fill.size()));
    widget.damage(FL_DAMAGE_ALL);
    widget.draw();
    widget.damage(0);
    Fl::check();

    const double full_start = now_seconds();
    for (std::size_t index = 0; index < count; ++index) {
        widget.damage(FL_DAMAGE_ALL);
        widget.draw();
        widget.damage(0);
    }
    const double full_elapsed = now_seconds() - full_start;

    const std::string line = "incremental redraw line\r\n";
    const double incremental_start = now_seconds();
    for (std::size_t index = 0; index < count; ++index) {
        emulator.feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>(line.data()),
                                                 line.size()));
        widget.damage(0);
        widget.draw();
    }
    const double incremental_elapsed = now_seconds() - incremental_start;

    Fl::check();
    std::printf("redraw_count=%zu\n", count);
    std::printf("redraw_full_ms=%.3f\n", full_elapsed * 1000.0 / static_cast<double>(count));
    std::printf("redraw_incremental_ms=%.3f\n",
                incremental_elapsed * 1000.0 / static_cast<double>(count));
    std::printf("redraw_full_fps=%.1f\n", static_cast<double>(count) / full_elapsed);
    std::printf("redraw_incremental_fps=%.1f\n", static_cast<double>(count) / incremental_elapsed);
}

void run_idle(double seconds, std::size_t terminals)
{
    TerminalManager manager(nullptr, nullptr, std::chrono::milliseconds{2000});
    manager.start();
    std::vector<TerminalId> ids;
    for (std::size_t index = 0; index < terminals; ++index) {
        if (index > 0) {
            manager.create_tab();
        }
        TerminalOptions options;
        options.session.executable = "/bin/sleep";
        options.session.arguments = {"30"};
        const auto id = manager.create_root_terminal(options);
        if (!id.has_value()) {
            std::fprintf(stderr, "idle: could not create terminal\n");
            manager.shutdown();
            std::exit(EXIT_FAILURE);
        }
        ids.push_back(*id);
    }
    const bool running = wait_for(
        [&] {
            return std::ranges::all_of(ids, [&](TerminalId id) {
                const auto *instance = manager.find(id);
                return instance != nullptr && instance->state() == TerminalState::running;
            });
        },
        manager, 15.0);
    if (!running) {
        std::fprintf(stderr, "idle: terminals did not start\n");
        manager.shutdown();
        std::exit(EXIT_FAILURE);
    }

    const double cpu_start = process_cpu_seconds();
    const double wall_start = now_seconds();
    while (now_seconds() - wall_start < seconds) {
        manager.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    const double cpu = process_cpu_seconds() - cpu_start;
    const double wall = now_seconds() - wall_start;
    manager.shutdown();
    std::printf("idle_seconds=%.3f\n", wall);
    std::printf("idle_terminals=%zu\n", terminals);
    std::printf("idle_cpu_percent=%.4f\n", cpu / wall * 100.0);
}

void usage()
{
    std::fprintf(stderr, "usage: consola_bench <workload> [args]\n"
                         "  parser [MB]        feed the deterministic stream to one emulator\n"
                         "  pty [MB]           end-to-end PTY read + parse of the same stream\n"
                         "  startup            first terminal running, 5 runs\n"
                         "  memory [N]         RSS with N idle terminals\n"
                         "  scrollback [LINES] RSS for a full scrollback\n"
                         "  churn [CYCLES] [N] create/close terminals and report leaks\n"
                         "  resize [COUNT]     emulator resize cost\n"
                         "  redraw [COUNT]     full and incremental widget draws (needs X)\n"
                         "  idle [SECONDS] [N] CPU while pumping N idle terminals\n"
                         "  all                run the default suite\n");
}

} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc >= 2 && std::string_view(argv[1]) == "--emit") {
            const std::size_t megabytes = argc >= 3 ? parse_size(argv[2], 1) : 1;
            return emit_stream(megabytes);
        }
        if (argc < 2) {
            usage();
            return EXIT_FAILURE;
        }

        const std::string_view workload = argv[1];
        if (workload == "parser") {
            run_parser(argc >= 3 ? parse_size(argv[2], 256) : 256);
        } else if (workload == "pty") {
            run_pty(argc >= 3 ? parse_size(argv[2], 64) : 64);
        } else if (workload == "startup") {
            run_startup();
        } else if (workload == "memory") {
            run_memory(argc >= 3 ? parse_size(argv[2], 1) : 1);
        } else if (workload == "scrollback") {
            run_scrollback(argc >= 3 ? parse_size(argv[2], 10000) : 10000);
        } else if (workload == "churn") {
            run_churn(argc >= 3 ? parse_size(argv[2], 100) : 100,
                      argc >= 4 ? parse_size(argv[3], 1) : 1);
        } else if (workload == "resize") {
            run_resize(argc >= 3 ? parse_size(argv[2], 1000) : 1000);
        } else if (workload == "redraw") {
            run_redraw(argc >= 3 ? parse_size(argv[2], 200) : 200);
        } else if (workload == "idle") {
            const double seconds = argc >= 3 ? std::atof(argv[2]) : 2.0;
            const std::size_t terminals = argc >= 4 ? parse_size(argv[3], 1) : 1;
            run_idle(seconds > 0.0 ? seconds : 2.0, terminals);
        } else if (workload == "all") {
            run_parser(256);
            run_pty(64);
            run_startup();
            run_memory(1);
            run_memory(10);
            run_memory(50);
            run_scrollback(10000);
            run_resize(1000);
            run_redraw(200);
            run_idle(2.0, 1);
            run_idle(2.0, 10);
        } else {
            usage();
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "benchmark failed: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
