// SPDX-License-Identifier: MIT

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include <app/emulator.hpp>
#include <app/tab.hpp>
#include <app/terminal_instance.hpp>
#include <app/terminal_manager.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

template <typename T> const T &required(const std::optional<T> &value, int line)
{
    static const T fallback{};
    if (!value) {
        std::fprintf(stderr, "FAIL line %d: expected a value\n", line);
        ++g_failures;
        return fallback;
    }
    return *value;
}

#define REQUIRED(value) required((value), __LINE__)

using consola::Orientation;
using consola::TerminalInstance;
using consola::TerminalManager;
using consola::TerminalOptions;
using consola::TerminalState;

struct TextContext {
    std::string text;
    int last_row = 0;
    bool has_row = false;
};

void collect_cell(void *data, [[maybe_unused]] int column, int row, const consola::Cell &cell)
{
    auto *context = static_cast<TextContext *>(data);
    if (!context->has_row) {
        context->has_row = true;
        context->last_row = row;
    } else if (row != context->last_row) {
        context->text.push_back('\n');
        context->last_row = row;
    }
    for (const std::uint32_t codepoint : cell.text) {
        if (codepoint >= 32 && codepoint < 127) {
            context->text.push_back(static_cast<char>(codepoint));
        }
    }
}

std::string screen_text(consola::TerminalEmulator &emulator)
{
    TextContext context;
    emulator.draw(&collect_cell, &context);
    return context.text;
}

bool wait_for(const std::function<bool()> &predicate, int timeout_ms, TerminalManager &manager)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        manager.pump();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    manager.pump();
    return predicate();
}

TerminalOptions shell(const std::string &command)
{
    TerminalOptions options;
    options.session.executable = "/bin/sh";
    options.session.arguments = {"-c", command};
    return options;
}

void test_independent_terminals()
{
    TerminalManager manager;
    manager.start();
    const auto first = manager.create_root_terminal(shell("printf one"));
    CHECK(first.has_value());
    const auto second =
        manager.split_terminal(REQUIRED(first), Orientation::vertical, shell("printf two"));
    CHECK(second.has_value());
    CHECK(manager.tab().terminal_count() == 2);
    CHECK(manager.tab().layout().terminal_count() == 2);

    TerminalInstance *first_instance = manager.find(REQUIRED(first));
    TerminalInstance *second_instance = manager.find(REQUIRED(second));
    CHECK(first_instance != nullptr);
    CHECK(second_instance != nullptr);
    CHECK(first_instance != second_instance);

    const bool saw_one = wait_for(
        [&] {
            return first_instance->state() == TerminalState::exited &&
                   screen_text(first_instance->emulator()).find("one") != std::string::npos;
        },
        5000, manager);
    const bool saw_two = wait_for(
        [&] {
            return second_instance->state() == TerminalState::exited &&
                   screen_text(second_instance->emulator()).find("two") != std::string::npos;
        },
        5000, manager);
    CHECK(saw_one);
    CHECK(saw_two);
    CHECK(screen_text(first_instance->emulator()).find("two") == std::string::npos);
    CHECK(screen_text(second_instance->emulator()).find("one") == std::string::npos);
    CHECK(!manager.launch_failed());
}

void test_launch_failure()
{
    TerminalManager manager;
    manager.start();
    TerminalOptions options;
    options.session.executable = "/nonexistent/consola-no-such-binary";
    const auto id = manager.create_root_terminal(options);
    CHECK(id.has_value());

    TerminalInstance *instance = manager.find(REQUIRED(id));
    CHECK(instance != nullptr);
    const bool exited =
        wait_for([&] { return instance->state() == TerminalState::exited; }, 5000, manager);
    CHECK(exited);
    CHECK(manager.launch_failed());
}

void test_send_to_running()
{
    TerminalManager manager;
    manager.start();
    TerminalOptions options;
    options.session.executable = "/bin/cat";
    const auto id = manager.create_root_terminal(options);
    CHECK(id.has_value());

    TerminalInstance *instance = manager.find(REQUIRED(id));
    CHECK(instance != nullptr);
    const bool running =
        wait_for([&] { return instance->state() == TerminalState::running; }, 5000, manager);
    CHECK(running);

    const std::string payload = "ping\n";
    manager.send(REQUIRED(id),
                 std::span<const std::byte>(reinterpret_cast<const std::byte *>(payload.data()),
                                            payload.size()));
    const bool echoed = wait_for(
        [&] { return screen_text(instance->emulator()).find("ping") != std::string::npos; }, 5000,
        manager);
    CHECK(echoed);
}

void test_root_rejected_when_nonempty()
{
    TerminalManager manager;
    manager.start();
    const auto first = manager.create_root_terminal(shell("printf first"));
    CHECK(first.has_value());
    const auto second = manager.create_root_terminal(shell("printf second"));
    CHECK(!second.has_value());
    CHECK(manager.tab().terminal_count() == 1);
}

} // namespace

int main()
{
    test_independent_terminals();
    test_launch_failure();
    test_send_to_running();
    test_root_rejected_when_nonempty();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d terminal manager check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
