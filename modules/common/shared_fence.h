// filepath: modules/common/shared_fence.h
// Cross-process GPU synchronization via IDXGIKeyedMutex.
//
// D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX textures carry a built-in keyed
// mutex that is GPU-aware: AcquireSync blocks until the GPU has finished
// all work on that resource for the specified key. This is the only correct
// cross-process synchronization for D3D11 shared textures — CPU atomics
// cannot observe GPU completion.
//
// Protocol (per frame):
//
//  Producer (hook, in-process):
//    1. QueryInterface<IDXGIKeyedMutex>(shared_color_tex) -> km
//    2. km->AcquireSync(kProducerKey, INFINITE)    // wait for daemon to release
//    3. ctx->CopyResource(shared_color_tex, backbuffer)
//    4. km->ReleaseSync(kDaemonKey)                // GPU: signal daemon
//    5. slot->fence.store(frame_index)             // CPU: signal slot ready
//    6. SetState(slot, Ready)
//
//  Consumer (daemon):
//    1. ConsumeFrame() -> slot (CPU state check only)
//    2. FenceWait::WaitForFence()                  // wait for CPU Ready signal
//    3. QueryInterface<IDXGIKeyedMutex>(opened_tex) -> km
//    4. km->AcquireSync(kDaemonKey, timeout_ms)    // GPU: wait for copy done
//    5. ... read texture ...
//    6. km->ReleaseSync(kProducerKey)              // release texture to producer
//    7. ReleaseFrame(slot)                         // CPU: slot -> Free
//
// kProducerKey = 0  (producer owns on init and after daemon releases)
// kDaemonKey   = 1  (daemon owns after producer's CopyResource completes)
//
// If IDXGIKeyedMutex is unavailable (pre-Win8 or non-DXGI path), the caller
// falls back to the CPU-only fence which is documented as UNSAFE for GPU
// write ordering; it is kept only so non-DXGI paths (D3D9/OpenGL) compile.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>
#include <dxgi.h>

#include "ring_buffer.h"

namespace omnirender {

// Keyed mutex key values. Both producer and consumer must use the same enum.
constexpr UINT64 kKeyedMutexProducer = 0;  // producer acquires with this key
constexpr UINT64 kKeyedMutexDaemon   = 1;  // daemon   acquires with this key

// Helper: obtain the keyed mutex interface from a D3D11 shared texture.
// Returns nullptr if the texture was not created with
// D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX.
inline IDXGIKeyedMutex* GetKeyedMutex(IUnknown* resource) noexcept {
    if (!resource) return nullptr;
    IDXGIKeyedMutex* km = nullptr;
    resource->QueryInterface(__uuidof(IDXGIKeyedMutex),
                             reinterpret_cast<void**>(&km));
    return km;  // caller must Release()
}

// CPU-side slot readiness fence (NOT a GPU completion fence).
// Only use this to confirm the producer has finished writing the IPC
// payload fields (surface dimensions, handles, flags). GPU completion
// MUST be handled via IDXGIKeyedMutex on the opened texture.
class FenceWait {
public:
    // Polls at ~1ms granularity. Returns true when
    // slot.fence >= expected_fence (i.e., producer wrote all payload fields).
    // Does NOT guarantee GPU write completion — call IDXGIKeyedMutex::AcquireSync
    // on the opened texture AFTER this returns.
    bool WaitForFence(const FrameSlot& slot,
                      uint64_t expected_fence,
                      uint32_t timeout_ms) noexcept {
        const auto deadline = ::GetTickCount64() + timeout_ms;
        while (::GetTickCount64() < deadline) {
            if (slot.fence.load(std::memory_order_acquire) >= expected_fence)
                return true;
            ::Sleep(0);
        }
        return false;
    }
};

// Producer helper: call OnPresent after CopyResource + ReleaseSync.
class FenceSignal {
public:
    void OnPresent(FrameSlot& slot, uint64_t frame_index,
                   uint64_t fence_value) noexcept {
        slot.fence.store(fence_value, std::memory_order_release);
        slot.frame_index = frame_index;
        SetState(slot, SlotState::Ready);
    }
};

}  // namespace omnirender
