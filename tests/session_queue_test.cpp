// SPDX-License-Identifier: MIT

#include <app/session_queue.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::string to_string(const std::vector<std::byte> &bytes)
{
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

bool push_data(consola::SessionInboundQueue &queue, const char *text)
{
    const auto *bytes = reinterpret_cast<const std::byte *>(text);
    return queue.try_push_data(
        std::span<const std::byte>(bytes, std::char_traits<char>::length(text)));
}

bool test_inbound_order()
{
    consola::SessionInboundQueue queue;
    if (!push_data(queue, "first")) {
        return false;
    }
    consola::InboundMessage eof;
    eof.kind = consola::InboundMessage::Kind::eof;
    if (!queue.try_push_lifecycle(std::move(eof))) {
        return false;
    }
    if (!push_data(queue, "second")) {
        return false;
    }

    consola::InboundMessage message;
    if (!queue.try_pop(message) || message.kind != consola::InboundMessage::Kind::data ||
        to_string(message.bytes) != "first") {
        return false;
    }
    if (!queue.try_pop(message) || message.kind != consola::InboundMessage::Kind::eof) {
        return false;
    }
    if (!queue.try_pop(message) || message.kind != consola::InboundMessage::Kind::data ||
        to_string(message.bytes) != "second") {
        return false;
    }
    return !queue.try_pop(message) && queue.empty();
}

bool test_inbound_bounds()
{
    consola::SessionInboundQueue queue;
    for (std::size_t i = 0; i < consola::SessionInboundQueue::kDataSlots; ++i) {
        if (!push_data(queue, "x")) {
            return false;
        }
    }
    if (!queue.data_full()) {
        return false;
    }
    if (push_data(queue, "overflow")) {
        return false;
    }
    consola::InboundMessage eof;
    eof.kind = consola::InboundMessage::Kind::eof;
    if (!queue.try_push_lifecycle(std::move(eof))) {
        return false;
    }

    std::size_t data_count = 0;
    std::size_t lifecycle_count = 0;
    consola::InboundMessage message;
    while (queue.try_pop(message)) {
        if (message.kind == consola::InboundMessage::Kind::data) {
            ++data_count;
        } else {
            ++lifecycle_count;
        }
    }
    return data_count == consola::SessionInboundQueue::kDataSlots && lifecycle_count == 1;
}

bool test_outbound_bounds()
{
    consola::SessionOutboundQueue queue(10);
    const std::array<std::byte, 8> buffer{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                          std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    if (!queue.try_push(std::span<const std::byte>(buffer.data(), 4))) {
        return false;
    }
    if (queue.pending_bytes() != 4) {
        return false;
    }
    if (queue.try_push(std::span<const std::byte>(buffer.data(), 7))) {
        return false;
    }
    if (!queue.try_push(std::span<const std::byte>(buffer.data(), 6))) {
        return false;
    }
    if (queue.pending_bytes() != 10) {
        return false;
    }
    if (queue.front() == nullptr || queue.front()->size() != 4) {
        return false;
    }
    queue.pop_front();
    if (queue.pending_bytes() != 6) {
        return false;
    }
    queue.pop_front();
    return queue.empty() && queue.pending_bytes() == 0;
}

bool test_outbound_slots()
{
    consola::SessionOutboundQueue queue(1024);
    const auto a = std::byte{7};
    for (std::size_t i = 0; i < consola::SessionOutboundQueue::kSlots; ++i) {
        if (!queue.try_push(std::span<const std::byte>(&a, 1))) {
            return false;
        }
    }
    return !queue.try_push(std::span<const std::byte>(&a, 1));
}

bool test_inbound_concurrent_order()
{
    consola::SessionInboundQueue queue;
    constexpr int kCount = 4000;
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            if ((i & 1) == 0) {
                const auto value = static_cast<std::uint64_t>(i);
                const auto *bytes = reinterpret_cast<const std::byte *>(&value);
                while (!queue.try_push_data(std::span<const std::byte>(bytes, sizeof(value)))) {
                    std::this_thread::yield();
                }
            } else {
                consola::InboundMessage message;
                message.kind = consola::InboundMessage::Kind::paste_finished;
                message.exit_status = i;
                while (!queue.try_push_lifecycle(message)) {
                    std::this_thread::yield();
                }
            }
        }
    });

    int expected = 0;
    consola::InboundMessage message;
    while (expected < kCount) {
        if (!queue.try_pop(message)) {
            std::this_thread::yield();
            continue;
        }
        int index = -1;
        if (message.kind == consola::InboundMessage::Kind::data) {
            if (message.bytes.size() != sizeof(std::uint64_t)) {
                failed.store(true);
                break;
            }
            std::uint64_t value = 0;
            std::memcpy(&value, message.bytes.data(), sizeof(value));
            index = static_cast<int>(value);
        } else {
            index = message.exit_status;
        }
        if (index != expected) {
            failed.store(true);
            break;
        }
        ++expected;
    }
    producer.join();
    return !failed.load() && expected == kCount;
}

bool test_spsc_stress()
{
    consola::SpscRing<int, 64> ring;
    constexpr int kCount = 100000;
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            int *slot = nullptr;
            while ((slot = ring.prepare_push()) == nullptr) {
                std::this_thread::yield();
            }
            *slot = i;
            ring.commit_push();
        }
    });

    int expected = 0;
    while (expected < kCount) {
        const int *value = ring.front();
        if (value == nullptr) {
            std::this_thread::yield();
            continue;
        }
        if (*value != expected) {
            failed.store(true);
            break;
        }
        ring.pop();
        ++expected;
    }
    producer.join();
    return !failed.load() && expected == kCount;
}

} // namespace

int main()
{
    if (!test_inbound_order()) {
        std::printf("test_inbound_order failed\n");
        return 1;
    }
    if (!test_inbound_bounds()) {
        std::printf("test_inbound_bounds failed\n");
        return 1;
    }
    if (!test_outbound_bounds()) {
        std::printf("test_outbound_bounds failed\n");
        return 1;
    }
    if (!test_outbound_slots()) {
        std::printf("test_outbound_slots failed\n");
        return 1;
    }
    if (!test_inbound_concurrent_order()) {
        std::printf("test_inbound_concurrent_order failed\n");
        return 1;
    }
    if (!test_spsc_stress()) {
        std::printf("test_spsc_stress failed\n");
        return 1;
    }
    std::printf("session_queue_test ok\n");
    return 0;
}
