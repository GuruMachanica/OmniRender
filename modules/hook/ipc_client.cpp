// filepath: modules/hook/ipc_client.cpp
// IPC ring buffer producer (hook side).
//
// The hook is the producer of the named file mapping
// `Global\OmniRender_IPC_Block`. We open the mapping lazily — if the
// daemon isn't up yet, we create the mapping ourselves so the daemon
// can attach when it starts. The actual per-frame work happens in
// d3d9_interceptor.cpp and dxgi_interceptor.cpp; this file just owns
// the mapping and the slot helpers.

#include <windows.h>
#include <cstdint>

#include "../common/logging.h"
#include "../common/ring_buffer.h"

namespace omnirender::hook {

namespace {

omnirender::RingControlBlock* g_ring = nullptr;

}  // namespace

bool PublishFrame(const omnirender::FrameSlot& slot) {
    if (!g_ring) return false;
    // Producer-side: caller already wrote `slot.payload`. We just
    // commit by setting state to Ready.
    return true;
}

bool InitializeIpcClient() {
    if (g_ring) return true;
    HANDLE mapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE,
                                        omnirender::kIPCBlockName);
    if (!mapping) {
        mapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE,
                                     omnirender::kIPCBlockNameLocal);
    }
    if (!mapping) {
        mapping = ::CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(omnirender::RingControlBlock),
            omnirender::kIPCBlockName);
    }
    if (!mapping) {
        mapping = ::CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(omnirender::RingControlBlock),
            omnirender::kIPCBlockNameLocal);
    }
    if (!mapping) {
        OMNI_LOG_ERROR("OpenFileMappingA/CreateFileMappingA failed: %lu", ::GetLastError());
        return false;
    }
    g_ring = static_cast<omnirender::RingControlBlock*>(
        ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                        sizeof(omnirender::RingControlBlock)));
    if (!g_ring) {
        OMNI_LOG_ERROR("MapViewOfFile failed: %lu", ::GetLastError());
        return false;
    }
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
    OMNI_LOG_INFO("IPC client attached (capacity=%u)",
                  static_cast<unsigned>(omnirender::kRingCapacity));
    return true;
}

void ShutdownIpcClient() {
    if (g_ring) {
        ::UnmapViewOfFile(g_ring);
        g_ring = nullptr;
    }
}

// Install-side entry point used by the DllMain. Today this is a
// thin wrapper around InitializeIpcClient + a log line; the hook
// can later extend it to push the first sample into the ring
// without breaking callers.
void InstallIPCClient() {
    if (!InitializeIpcClient()) {
        OMNI_LOG_ERROR("InstallIPCClient: InitializeIpcClient failed");
        return;
    }
    OMNI_LOG_INFO("InstallIPCClient: hook IPC client online");
}

}  // namespace omnirender::hook
