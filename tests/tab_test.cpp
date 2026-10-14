// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>

#include <app/tab.hpp>
#include <app/terminal_instance.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::Orientation;
using consola::SessionError;
using consola::SessionOptions;
using consola::Tab;
using consola::TerminalId;
using consola::TerminalInstance;
using consola::TerminalState;

TerminalId tid(std::uint64_t value)
{
    return TerminalId{value};
}

class FakeSession : public consola::TerminalSession {
public:
    SessionError start([[maybe_unused]] const SessionOptions &options) override
    {
        return SessionError::none;
    }
    consola::IoResult read([[maybe_unused]] std::span<std::byte> buffer) override
    {
        return {};
    }
    consola::IoResult write([[maybe_unused]] std::span<const std::byte> bytes) override
    {
        return {};
    }
    SessionError resize([[maybe_unused]] int cols, [[maybe_unused]] int rows) override
    {
        return SessionError::none;
    }
    void terminate() override
    {}
    [[nodiscard]] bool running() const override
    {
        return true;
    }
};

void test_instance_lifecycle()
{
    TerminalInstance instance(tid(1), SessionOptions{});
    CHECK(instance.id() == tid(1));
    CHECK(instance.state() == TerminalState::created);
    CHECK(!instance.input_enabled());
    CHECK(instance.session() == nullptr);
    CHECK(instance.master_fd() == -1);

    auto fake = std::make_unique<FakeSession>();
    FakeSession *raw = fake.get();
    instance.attach(std::move(fake), 5, 6, 42);
    CHECK(instance.state() == TerminalState::running);
    CHECK(instance.input_enabled());
    CHECK(instance.session() == raw);
    CHECK(instance.master_fd() == 5);
    CHECK(instance.exit_notification_fd() == 6);
    CHECK(instance.process_id() == 42);

    auto released = instance.release_session();
    CHECK(released.get() == raw);
    CHECK(instance.session() == nullptr);
    CHECK(instance.master_fd() == -1);
}

void test_tab_ownership()
{
    Tab tab;
    CHECK(tab.terminal_count() == 0);

    auto first = std::make_unique<TerminalInstance>(tid(1), SessionOptions{});
    TerminalInstance *root = tab.add_root(std::move(first));
    CHECK(root != nullptr);
    CHECK(tab.terminal_count() == 1);
    CHECK(tab.find(tid(1)) == root);
    CHECK(tab.layout().contains(tid(1)));
    CHECK(tab.layout().terminal_count() == 1);

    auto second = std::make_unique<TerminalInstance>(tid(2), SessionOptions{});
    const auto leaf = tab.split_terminal(tid(1), Orientation::vertical, std::move(second));
    CHECK(leaf.has_value());
    CHECK(tab.terminal_count() == 2);
    CHECK(tab.find(tid(2)) != nullptr);
    CHECK(tab.layout().contains(tid(2)));
    CHECK(tab.layout().terminal_count() == 2);
    CHECK(tab.terminals().size() == 2);
}

void test_tab_rejections()
{
    Tab tab;
    tab.add_root(std::make_unique<TerminalInstance>(tid(1), SessionOptions{}));

    auto duplicate = std::make_unique<TerminalInstance>(tid(1), SessionOptions{});
    CHECK(!tab.split_terminal(tid(1), Orientation::horizontal, std::move(duplicate)).has_value());
    CHECK(tab.terminal_count() == 1);

    auto unknown = std::make_unique<TerminalInstance>(tid(3), SessionOptions{});
    CHECK(!tab.split_terminal(tid(99), Orientation::horizontal, std::move(unknown)).has_value());
    CHECK(tab.find(tid(3)) == nullptr);
    CHECK(tab.terminal_count() == 1);
}

void test_tab_removal()
{
    Tab tab;
    tab.add_root(std::make_unique<TerminalInstance>(tid(1), SessionOptions{}));
    tab.split_terminal(tid(1), Orientation::vertical,
                       std::make_unique<TerminalInstance>(tid(2), SessionOptions{}));

    auto removed = tab.remove_terminal(tid(2));
    CHECK(removed != nullptr);
    CHECK(removed->id() == tid(2));
    CHECK(tab.find(tid(2)) == nullptr);
    CHECK(tab.terminal_count() == 1);
    CHECK(tab.layout().terminal_count() == 1);
    CHECK(tab.layout().terminal(tab.layout().root())->terminal == tid(1));

    CHECK(tab.remove_terminal(tid(99)) == nullptr);
}

void test_add_root_rejections()
{
    Tab tab;
    CHECK(tab.add_root(std::make_unique<TerminalInstance>(tid(1), SessionOptions{})) != nullptr);
    CHECK(tab.add_root(std::make_unique<TerminalInstance>(tid(2), SessionOptions{})) == nullptr);
    CHECK(tab.terminal_count() == 1);
    CHECK(tab.find(tid(2)) == nullptr);
}

} // namespace

int main()
{
    test_instance_lifecycle();
    test_tab_ownership();
    test_tab_rejections();
    test_tab_removal();
    test_add_root_rejections();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d tab check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
