// SPDX-License-Identifier: MIT

#include <app/session_queue.hpp>

#include <algorithm>
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

bool SessionInboundQueue::try_push_lifecycle(InboundMessage &&message)
{
    LifecycleSlot *slot = lifecycle_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    slot->sequence = next_sequence_++;
    // Lifecycle payloads carry no bytes; copy the small fields so the ring slot
    // never performs a vector move assignment (GCC 14 warns on the inlined one).
    slot->message.kind = message.kind;
    slot->message.exit_status = message.exit_status;
    slot->message.paste_status = message.paste_status;
    slot->message.bytes.clear();
    lifecycle_.commit_push();
    return true;
}

bool SessionInboundQueue::front_is_data() const noexcept
{
    const DataSlot *data = data_.front();
    const LifecycleSlot *lifecycle = lifecycle_.front();
    if (data == nullptr) {
        return false;
    }
    if (lifecycle == nullptr) {
        return true;
    }

    // Data and lifecycle events live in separate rings. If we selected the ring
    // that published later, re-read the other ring: an item published just
    // before the selected one (lower sequence) may not have been visible on the
    // first load. The acquire on the selected ring's head makes every earlier
    // producer commit visible to this second load, so the merge cannot return a
    // later event before an earlier one (DIP 1.7 ordering).
    if (data->sequence < lifecycle->sequence) {
        const LifecycleSlot *earlier = lifecycle_.front();
        return earlier == nullptr || earlier->sequence >= data->sequence;
    }
    const DataSlot *earlier = data_.front();
    return earlier != nullptr && earlier->sequence < lifecycle->sequence;
}

bool SessionInboundQueue::try_peek(View &out) const
{
    const DataSlot *data = data_.front();
    const LifecycleSlot *lifecycle = lifecycle_.front();
    if (data == nullptr && lifecycle == nullptr) {
        return false;
    }
    if (front_is_data()) {
        out.kind = InboundMessage::Kind::data;
        out.bytes = data->bytes;
        out.exit_status = 0;
        out.paste_status = PasteStatus::completed;
        return true;
    }
    out.kind = lifecycle->message.kind;
    out.bytes = {};
    out.exit_status = lifecycle->message.exit_status;
    out.paste_status = lifecycle->message.paste_status;
    return true;
}

void SessionInboundQueue::pop()
{
    if (front_is_data()) {
        data_.pop();
    } else {
        lifecycle_.pop();
    }
}

bool SessionInboundQueue::try_pop(InboundMessage &out)
{
    View view;
    if (!try_peek(view)) {
        return false;
    }
    out.kind = view.kind;
    out.exit_status = view.exit_status;
    out.paste_status = view.paste_status;
    if (view.kind == InboundMessage::Kind::data) {
        out.bytes.assign(view.bytes.begin(), view.bytes.end());
    } else {
        out.bytes.clear();
    }
    pop();
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

SessionOutboundQueue::SessionOutboundQueue(std::size_t byte_limit, std::size_t reserve) noexcept
    : byte_limit_(byte_limit), reserve_bytes_(std::min(reserve, byte_limit))
{}

bool SessionOutboundQueue::try_push(std::span<const std::byte> bytes, OutboundAdmission admission)
{
    if (bytes.empty()) {
        return true;
    }
    Slot *slot = ring_.prepare_push();
    if (slot == nullptr) {
        return false;
    }
    const std::size_t limit =
        admission == OutboundAdmission::reserved ? byte_limit_ : byte_limit_ - reserve_bytes_;
    const std::size_t pending = pending_.load(std::memory_order_relaxed);
    if (pending > limit || bytes.size() > limit - pending) {
        return false;
    }
    slot->bytes.assign(bytes.begin(), bytes.end());
    slot->released = 0;
    pending_.fetch_add(bytes.size(), std::memory_order_release);
    ring_.commit_push();
    return true;
}

const std::vector<std::byte> *SessionOutboundQueue::front() const noexcept
{
    const Slot *slot = ring_.front();
    return slot == nullptr ? nullptr : &slot->bytes;
}

void SessionOutboundQueue::release(std::size_t count) noexcept
{
    Slot *slot = ring_.front_mut();
    if (slot == nullptr) {
        return;
    }
    const std::size_t remaining = slot->bytes.size() - slot->released;
    const std::size_t amount = std::min(count, remaining);
    slot->released += amount;
    pending_.fetch_sub(amount, std::memory_order_release);
}

void SessionOutboundQueue::pop_front() noexcept
{
    Slot *slot = ring_.front_mut();
    if (slot == nullptr) {
        return;
    }
    pending_.fetch_sub(slot->bytes.size() - slot->released, std::memory_order_release);
    slot->released = 0;
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
