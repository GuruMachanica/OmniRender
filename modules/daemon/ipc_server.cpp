// filepath: modules/daemon/ipc_server.cpp
// Daemon-side IPC ring consumer.
//
// Opens the named file mapping created by the hook, scans the slots
// for ones in `Ready` state, and returns the next frame. The
// presentation loop calls ConsumeFrame once per present.

#include <windows.h>
#include <cstdint>

#include "../common/logging.h"
#include "../common/ring_buffer.h"

namespace omnirender::daemon {

namespace {

HANDLE                       g_mapping = nullptr;
omnirender::RingControlBlock* g_ring    = nullptr;

}  // namespace

int InitializeIpcServer() {
    if (g_ring) return 0;
    g_mapping = ::CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(omnirender::RingControlBlock),
        omnirender::kIPCBlockName);
    if (!g_mapping) {
        g_mapping = ::CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(omnirender::RingControlBlock),
            omnirender::kIPCBlockNameLocal);
    }
    if (!g_mapping) {
        // Maybe the hook created the mapping first; try opening it.
        g_mapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE,
                                       omnirender::kIPCBlockName);
    }
    if (!g_mapping) {
        g_mapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE,
                                       omnirender::kIPCBlockNameLocal);
    }
    if (!g_mapping) {
        OMNI_LOG_ERROR("CreateFileMappingA/OpenFileMappingA failed: %lu", ::GetLastError());
        return -1;
    }
    g_ring = static_cast<omnirender::RingControlBlock*>(
        ::MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                        sizeof(omnirender::RingControlBlock)));
    if (!g_ring) {
        OMNI_LOG_ERROR("MapViewOfFile failed: %lu", ::GetLastError());
        ::CloseHandle(g_mapping);
        g_mapping = nullptr;
        return -2;
    }
    // The hook may have initialized first. If not, initialize now.
    uint32_t expected = 0;
    if (g_ring->magic.compare_exchange_strong(
            expected, omnirender::kIpcMagic,
            std::memory_order_acq_rel)) {
        g_ring->capacity.store(omnirender::kRingCapacity, std::memory_order_release);
        for (auto& slot : g_ring->slots) {
            slot.state.store(static_cast<uint32_t>(omnirender::SlotState::Free),
                             std::memory_order_release);
            slot.fence.store(0, std::memory_order_release);
        }
    }
    OMNI_LOG_INFO("IPC server ready (capacity=%u)",
                  static_cast<unsigned>(omnirender::kRingCapacity));
    return 0;
}

void ShutdownIpcServer() {
    if (g_ring) {
        ::UnmapViewOfFile(g_ring);
        g_ring = nullptr;
    }
    if (g_mapping) {
        ::CloseHandle(g_mapping);
        g_mapping = nullptr;
    }
}

bool ConsumeFrame(omnirender::FrameSlot*& out_slot) {
    if (!g_ring) return false;
    uint64_t c_seq = g_ring->consumer_seq.load(std::memory_order_acquire);
    uint32_t slot_idx = static_cast<uint32_t>(c_seq % omnirender::kRingCapacity);
    auto state = omnirender::GetState(g_ring->slots[slot_idx]);
    if (state == omnirender::SlotState::Ready) {
        // Transition to Processing BEFORE returning the slot pointer.
        // This prevents the producer from reclaiming the slot while we read it.
        omnirender::SetState(g_ring->slots[slot_idx], omnirender::SlotState::Processing);
        out_slot = &g_ring->slots[slot_idx];
        g_ring->consumer_seq.store(c_seq + 1, std::memory_order_release);
        return true;
    }
    return false;
}

void ReleaseFrame(omnirender::FrameSlot& slot) {
    // Processing -> Free: slot is now safe for the producer to reuse.
    omnirender::SetState(slot, omnirender::SlotState::Free);
}

uint64_t GetRingAdapterLuid() {
    return g_ring ? g_ring->adapter_luid.load(std::memory_order_acquire) : 0;
}

}  // namespace omnirender::daemon
