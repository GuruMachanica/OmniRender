// filepath: modules/common/shared_fence.h
// Cross-process GPU fence helper.
//
// A fence is a 64-bit monotonically increasing value that both the
// producer (game/hook) and the consumer (daemon) can wait on. The
// producer signals the fence every present; the consumer waits until
// the fence value reaches the one the producer advertised in the IPC
// payload, then knows the GPU has finished writing the shared texture.
//
// On D3D11.5+ the underlying primitive is `ID3D11Fence` (D3D12-style).
// On older D3D11 we fall back to a CPU-side present-count and a
// busy-wait via `IDXGISwapChain::GetLastPresentCount`. D3D9 falls back
// to `D3DPRESENTSTATS` or a CPU fence in shared memory.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>

#include "ring_buffer.h"

namespace omnirender {

// Producer-side helper. Updates the in-process frame counter and the
// shared fence value atomically. The actual GPU signal is performed by
// the caller (hook) after `IDXGISwapChain::Present` returns.
class FenceSignal {
public:
    void OnPresent(FrameSlot& slot, uint64_t frame_index, uint64_t fence_value) noexcept {
        slot.fence.store(fence_value, std::memory_order_release);
        slot.frame_index = frame_index;
        // The state transition to Ready happens *after* the GPU has
        // signalled. The hook is responsible for ordering.
        SetState(slot, SlotState::Ready);
    }
};

// Consumer-side helper. Blocks (with a timeout) until the slot's
// fence value reaches `expected_fence`. Returns true on success.
class FenceWait {
public:
    // Polls at ~1ms granularity. Production code would prefer a
    // platform-native waitable; this is a portable starting point.
    bool WaitForFence(const FrameSlot& slot,
                      uint64_t expected_fence,
                      uint32_t timeout_ms) noexcept {
        const auto deadline = ::GetTickCount64() + timeout_ms;
        while (::GetTickCount64() < deadline) {
            uint64_t current = slot.fence.load(std::memory_order_acquire);
            if (current >= expected_fence) {
                return true;
            }
            ::Sleep(0);  // yield instead of spin
        }
        return false;
    }
};

}  // namespace omnirender
