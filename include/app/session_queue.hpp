// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <app/message.hpp>
#include <app/spsc_ring.hpp>

namespace consola {

// Loop -> UI events for one session (DIP 1.7). Data batches and lifecycle
// events share a sequence counter so the UI observes accepted output before
// the session's eof/exited events. Data is bounded to kDataSlots batches (each
// at most one 64 KiB read batch); lifecycle events use separate reserved slots
// and are never dropped by backpressure.
class SessionInboundQueue {
public:
    static constexpr std::size_t kDataSlots = 16;
    static constexpr std::size_t kLifecycleSlots = 8;

    // Producer (I/O loop). Copies up to one read batch. Returns false when the
    // data slots are full; the caller suspends the master poll entry.
    [[nodiscard]] bool try_push_data(std::span<const std::byte> bytes);

    // Producer (I/O loop). Lifecycle events are never dropped; returns false
    // only if the reserved lifecycle slots are exhausted (a defect).
    [[nodiscard]] bool try_push_lifecycle(InboundMessage message);

    // Consumer (UI). Borrow the oldest message without moving its payload. The
    // view, including a data span, stays valid until pop() is called; feed the
    // bytes before popping. This keeps the ring slot's buffer alive so the
    // producer can reuse its capacity instead of allocating per batch.
    struct View {
        InboundMessage::Kind kind = InboundMessage::Kind::data;
        std::span<const std::byte> bytes;
        int exit_status = 0;
        PasteStatus paste_status = PasteStatus::completed;
    };
    [[nodiscard]] bool try_peek(View &out) const;
    void pop();

    // Consumer (UI). Moves/copies the oldest message, in sequence order, into
    // out. Prefer try_peek()/pop() on the hot path; this is kept for callers
    // that want an owning message.
    [[nodiscard]] bool try_pop(InboundMessage &out);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool data_full() const noexcept;
    [[nodiscard]] bool has_data() const noexcept;

private:
    struct DataSlot {
        std::uint64_t sequence = 0;
        std::vector<std::byte> bytes;
    };
    struct LifecycleSlot {
        std::uint64_t sequence = 0;
        InboundMessage message;
    };

    SpscRing<DataSlot, kDataSlots> data_;
    SpscRing<LifecycleSlot, kLifecycleSlots> lifecycle_;
    std::uint64_t next_sequence_ = 0; // producer only

    [[nodiscard]] bool front_is_data() const noexcept;
};

// Admission class for the outbound budget (DIP 1.7). `user` bytes (keys, paste
// payload) cannot consume the reserve kept for required replies and
// cancellation framing; `reserved` bytes (automatic replies, framing) may.
enum class OutboundAdmission : std::uint8_t { user, reserved };

// UI -> loop outgoing bytes for one session (DIP 1.7). Bounded by slot count
// and total pending bytes. The UI is the only producer; the I/O loop is the
// only consumer and the only thread that writes to the master FD.
//
// Credits return only as bytes are written or explicitly discarded, never
// merely by popping a chunk: the loop calls release() for each byte it writes
// and pop_front() when the front chunk is fully written. A `reserved` class of
// `reserve_bytes()` is kept for required replies and cancellation framing so a
// full user queue cannot make a small reply fail immediately.
class SessionOutboundQueue {
public:
    static constexpr std::size_t kSlots = 16;
    static constexpr std::size_t kDefaultReserve = std::size_t{16} * 1024;

    explicit SessionOutboundQueue(std::size_t byte_limit,
                                  std::size_t reserve = kDefaultReserve) noexcept;

    // Producer (UI). Returns false if the slot or byte bound would be
    // exceeded, in which case nothing is queued.
    [[nodiscard]] bool try_push(std::span<const std::byte> bytes,
                                OutboundAdmission admission = OutboundAdmission::user);

    // Consumer (loop). Returns the front chunk, or nullptr if empty.
    [[nodiscard]] const std::vector<std::byte> *front() const noexcept;

    // Consumer (loop). Return credit for bytes written or discarded. Clamped to
    // the front chunk's unreleased bytes.
    void release(std::size_t count) noexcept;

    // Consumer (loop). Remove the front chunk, releasing any bytes not already
    // released.
    void pop_front() noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t pending_bytes() const noexcept;
    [[nodiscard]] std::size_t byte_limit() const noexcept
    {
        return byte_limit_;
    }
    [[nodiscard]] std::size_t reserve_bytes() const noexcept
    {
        return reserve_bytes_;
    }

private:
    struct Slot {
        std::vector<std::byte> bytes;
        std::size_t released = 0;
    };

    SpscRing<Slot, kSlots> ring_;
    std::size_t byte_limit_;
    std::size_t reserve_bytes_;
    std::atomic<std::size_t> pending_{0};
};

} // namespace consola
