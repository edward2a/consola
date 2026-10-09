// SPDX-License-Identifier: MIT

#include <app/session_queue.hpp>

#include <utility>

namespace consola {

bool SessionInboundQueue::try_push_data(std::span<const std::byte> bytes)
{
    DataSlot *slot = data_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    slot->sequence = next_sequence_++;
    slot->bytes.assign(bytes.begin(), bytes.end());
    data_.commit_push();
    return true;
}

bool SessionInboundQueue::try_push_lifecycle(InboundMessage message)
{
    LifecycleSlot *slot = lifecycle_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    slot->sequence = next_sequence_++;
    slot->message = std::move(message);
    lifecycle_.commit_push();
    return true;
}

bool SessionInboundQueue::try_pop(InboundMessage &out)
{
    const DataSlot *data = data_.front();
    const LifecycleSlot *lifecycle = lifecycle_.front();
    if (data == nullptr && lifecycle == nullptr) {
        return false;
    }
    const bool take_data =
        lifecycle == nullptr || (data != nullptr && data->sequence < lifecycle->sequence);
    if (take_data) {
        out = InboundMessage{};
        out.kind = InboundMessage::Kind::data;
        out.bytes = std::move(data_.front_mut()->bytes);
        data_.pop();
    } else {
        out = std::move(lifecycle_.front_mut()->message);
        lifecycle_.pop();
    }
    return true;
}

bool SessionInboundQueue::empty() const noexcept
{
    return data_.empty() && lifecycle_.empty();
}

bool SessionInboundQueue::data_full() const noexcept
{
    return data_.full();
}

bool SessionInboundQueue::has_data() const noexcept
{
    return !data_.empty();
}

SessionOutboundQueue::SessionOutboundQueue(std::size_t byte_limit) noexcept
    : byte_limit_(byte_limit)
{}

bool SessionOutboundQueue::try_push(std::span<const std::byte> bytes)
{
    if (bytes.empty()) {
        return true;
    }
    Slot *slot = ring_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    if (pending_.load(std::memory_order_relaxed) + bytes.size() > byte_limit_) {
        return false;
    }
    slot->bytes.assign(bytes.begin(), bytes.end());
    pending_.fetch_add(bytes.size(), std::memory_order_release);
    ring_.commit_push();
    return true;
}

const std::vector<std::byte> *SessionOutboundQueue::front() const noexcept
{
    const Slot *slot = ring_.front();
    return slot == nullptr ? nullptr : &slot->bytes;
}

void SessionOutboundQueue::pop_front() noexcept
{
    const Slot *slot = ring_.front();
    if (slot == nullptr) {
        return;
    }
    pending_.fetch_sub(slot->bytes.size(), std::memory_order_release);
    ring_.pop();
}

bool SessionOutboundQueue::empty() const noexcept
{
    return ring_.empty();
}

std::size_t SessionOutboundQueue::pending_bytes() const noexcept
{
    return pending_.load(std::memory_order_acquire);
}

} // namespace consola
