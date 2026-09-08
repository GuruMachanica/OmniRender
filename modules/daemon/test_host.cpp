// filepath: modules/daemon/test_host.cpp
// OmniRenderTestHost — IPC contract verification test renderer.

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "../common/halton.h"
#include "../common/ipc_protocol.h"
#include "../common/logging.h"
#include "../common/ring_buffer.h"
#include "test_host_renderer.h"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace omnirender::test {

namespace {

constexpr UINT kDefaultWidth  = 1280;
constexpr UINT kDefaultHeight = 720;

struct TestHostState {
    TestHostRenderer             renderer;
    omnirender::RingControlBlock* ring        = nullptr;
    std::atomic<bool>            running     { true };
    uint64_t                     frame_index = 0;
};

TestHostState g;

bool OpenIpcRing() {
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
    if (!mapping) return false;

    g.ring = static_cast<omnirender::RingControlBlock*>(
        ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                        sizeof(omnirender::RingControlBlock)));
    ::CloseHandle(mapping);
    if (!g.ring) return false;

    uint32_t expected = 0;
    if (g.ring->magic.compare_exchange_strong(
            expected, omnirender::kIpcMagic,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        g.ring->capacity.store(omnirender::kRingCapacity, std::memory_order_release);
        for (auto& slot : g.ring->slots) {
            slot.state.store(static_cast<uint32_t>(omnirender::SlotState::Free),
                             std::memory_order_release);
            slot.fence.store(0, std::memory_order_release);
        }
    }
    if (g.renderer.adapter_luid != 0) {
        g.ring->adapter_luid.store(g.renderer.adapter_luid, std::memory_order_release);
    }
    OMNI_LOG_INFO("test_host: ring attached (capacity=%u, adapter=0x%llx)",
                  static_cast<unsigned>(omnirender::kRingCapacity),
                  g.renderer.adapter_luid);
    return true;
}

void PublishFrame() {
    if (!g.ring || !g.renderer.shared_color_h) return;
    if (g.renderer.adapter_luid != 0) {
        g.ring->adapter_luid.store(g.renderer.adapter_luid, std::memory_order_release);
    }

    omnirender::FrameSlot* slot = nullptr;
    for (uint32_t i = 0; i < omnirender::kRingCapacity; ++i) {
        auto state = omnirender::GetState(g.ring->slots[i]);
        if (state == omnirender::SlotState::Free) {
            omnirender::SetState(g.ring->slots[i], omnirender::SlotState::Captured);
            slot = &g.ring->slots[i];
            break;
        }
    }
    if (!slot) return;

    slot->payload.magic_header         = omnirender::kIpcMagic;
    slot->payload.struct_version       = omnirender::kIpcVersion_V040;
    slot->payload.frame_index          = g.frame_index;
    slot->payload.surface_width        = g.renderer.width;
    slot->payload.surface_height       = g.renderer.height;
    slot->payload.target_width         = g.renderer.width;
    slot->payload.target_height        = g.renderer.height;
    slot->payload.color_format         = static_cast<uint32_t>(DXGI_FORMAT_B8G8R8A8_UNORM);
    slot->payload.depth_format         = static_cast<uint32_t>(DXGI_FORMAT_R32_FLOAT);
    slot->payload.shared_color_handle  = reinterpret_cast<uint64_t>(g.renderer.shared_color_h);
    slot->payload.shared_depth_handle  = reinterpret_cast<uint64_t>(g.renderer.shared_depth_h);
    slot->payload.shared_motion_handle = 0;
    slot->payload.camera_near          = 0.1f;
    slot->payload.camera_far           = 100.0f;
    slot->payload.fov_vertical_rad     = 1.0471975512f;
    auto h = omnirender::Halton23At(g.frame_index);
    slot->payload.jitter_x             = h.x;
    slot->payload.jitter_y             = h.y;
    slot->payload.motion_format        = 0;

    std::memcpy(slot->payload.view_proj_current, g.renderer.current_vp,
                sizeof(slot->payload.view_proj_current));
    std::memcpy(slot->payload.view_proj_previous, g.renderer.previous_vp,
                sizeof(slot->payload.view_proj_previous));
    slot->payload.flags = static_cast<uint32_t>(omnirender::IpcFlag::ReversedZ);

    slot->fence.store(g.frame_index, std::memory_order_release);
    omnirender::SetState(*slot, omnirender::SlotState::Ready);

    HANDLE evt = ::OpenEventA(EVENT_MODIFY_STATE, FALSE,
                              omnirender::kFrameReadyEventName);
    if (evt) {
        ::SetEvent(evt);
        ::CloseHandle(evt);
    }
}

}  // namespace
}  // namespace omnirender::test

int main() {
    using namespace omnirender;
    using namespace omnirender::test;

    UINT width = kDefaultWidth;
    UINT height = kDefaultHeight;
    if (const char* w = std::getenv("OMNIRENDER_TEST_HOST_WIDTH")) {
        width = static_cast<UINT>(std::atoi(w));
    }
    if (const char* h = std::getenv("OMNIRENDER_TEST_HOST_HEIGHT")) {
        height = static_cast<UINT>(std::atoi(h));
    }

    int duration_sec = 60;
    if (const char* d = std::getenv("OMNIRENDER_TEST_HOST_DURATION_SECONDS")) {
        duration_sec = std::atoi(d);
        if (duration_sec <= 0) duration_sec = 60;
    }

    if (!InitializeTestRenderer(g.renderer, width, height)) {
        std::fprintf(stderr, "test_host: InitializeTestRenderer failed\n");
        return 1;
    }

    if (!OpenIpcRing()) {
        std::fprintf(stderr, "test_host: OpenIpcRing failed\n");
        CleanupTestRenderer(g.renderer);
        return 2;
    }

    std::printf("test_host: running %ux%u for %d seconds\n", width, height, duration_sec);

    auto start_time = std::chrono::steady_clock::now();
    auto end_time   = start_time + std::chrono::seconds(duration_sec);
    auto last_frame = start_time;

    std::vector<double> frame_times_ms;
    frame_times_ms.reserve(duration_sec * 120);

    MSG msg{};
    while (g.running.load(std::memory_order_acquire)) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g.running.store(false, std::memory_order_release);
                break;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (!g.running.load(std::memory_order_acquire)) break;

        auto now = std::chrono::steady_clock::now();
        if (now >= end_time) break;

        double elapsed = std::chrono::duration<double>(now - start_time).count();
        RenderTestCube(g.renderer, elapsed, g.frame_index);
        PublishFrame();

        g.renderer.swapchain->Present(0, 0);

        double dt_ms = std::chrono::duration<double, std::milli>(now - last_frame).count();
        frame_times_ms.push_back(dt_ms);
        last_frame = now;

        ++g.frame_index;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    double total_sec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_time).count();
    double avg_fps = (total_sec > 0.0) ? (static_cast<double>(g.frame_index) / total_sec) : 0.0;
    std::printf("test_host: finished. rendered %llu frames in %.2f s (avg %.1f fps)\n",
                g.frame_index, total_sec, avg_fps);

    CleanupTestRenderer(g.renderer);
    return 0;
}
