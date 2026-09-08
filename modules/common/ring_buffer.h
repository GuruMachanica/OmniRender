// filepath: modules/common/ring_buffer.h
// SPSC ring buffer with explicit slot state machine.
//
// Four-slot, single-producer / single-consumer. The hook is the
// producer (CapturePath); the daemon is the consumer (RenderPath).
// Each slot has a `state` field so the producer can never silently
// overwrite a slot the consumer hasn't yet finished reading.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <atomic>
#include <cstdint>

#include "ipc_protocol.h"

namespace omnirender {

// Lifecycle of a single slot.
// Producer path: Free → Captured → Ready
// Consumer path: Ready → Processing → Free
// Producers may only reclaim slots in the Free state. 'Processing' is owned
// by the consumer between ConsumeFrame() and ReleaseFrame(), so a producer
// can never overwrite a slot that is still being read.
enum class SlotState : uint32_t {
    Free       = 0,  // producer may write
    Captured   = 1,  // producer is mid-write; consumer must wait
    Ready      = 2,  // producer finished; consumer may read
    Processing = 3,  // consumer is actively using; producer must NOT reclaim
};

inline constexpr uint32_t kRingCapacity = 4;

// One slot in the ring. The consumer reads it after the producer flips
// its state to Ready. The producer waits until state is Free before
// reusing it.
#pragma pack(push, 8)
struct alignas(8) FrameSlot {
    std::atomic<uint32_t>           state;        // SlotState
    uint32_t                        _pad0;        // 8-byte alignment pad for 64-bit atomic fence
    std::atomic<uint64_t>           fence;        // GPU-side completion fence value (8-byte aligned)
    uint64_t                        frame_index;  // monotonically increasing
    OmniRenderIPCFrameData          payload;      // safe to read only when state == Ready
};
#pragma pack(pop)

// Layout written to the named file mapping. Producer writes the
// header once; from then on it only manipulates per-slot state.
#pragma pack(push, 8)
struct alignas(8) RingControlBlock {
    std::atomic<uint32_t>           magic;            // OmniRenderIPCFrameData::kIpcMagic
    std::atomic<uint32_t>           capacity;         // == kRingCapacity
    std::atomic<uint64_t>           producer_seq;     // monotonically increasing
    std::atomic<uint64_t>           consumer_seq;     // monotonically increasing
    std::atomic<uint64_t>           adapter_luid;     // Producer GPU adapter LUID
    FrameSlot                       slots[kRingCapacity];
};
#pragma pack(pop)

static_assert(sizeof(RingControlBlock) <= 64 * 1024,
              "RingControlBlock must fit in a single page");

// Helpers for in-process state transitions. These wrap std::atomic
// operations with the right memory orders.
inline void Transition(FrameSlot& slot, SlotState from, SlotState to) noexcept {
    uint32_t expected = static_cast<uint32_t>(from);
    slot.state.compare_exchange_strong(
        expected,
        static_cast<uint32_t>(to),
        std::memory_order_acq_rel,
        std::memory_order_relaxed);
}

inline void SetState(FrameSlot& slot, SlotState to) noexcept {
    slot.state.store(static_cast<uint32_t>(to), std::memory_order_release);
}

inline SlotState GetState(const FrameSlot& slot) noexcept {
    return static_cast<SlotState>(slot.state.load(std::memory_order_acquire));
}

}  // namespace omnirender
