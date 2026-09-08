// filepath: modules/hook/opengl_interceptor.cpp
// OpenGL frame capture via wglSwapBuffers hook with zero-copy WGL_NV_DX_interop2.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "opengl_interop.h"
#include "wgl_thunk.h"
#include "../common/halton.h"
#include "../common/ipc_protocol.h"
#include "../common/logging.h"
#include "../common/ring_buffer.h"

namespace omnirender::hook {

namespace {

HMODULE                g_real_opengl32  = nullptr;
std::atomic<bool>       g_real_resolved { false };
std::mutex              g_real_mu;
PFNWGLSWAPBUFFERS       g_real_wglSwapBuffers       = nullptr;
PFNWGLSWAPLAYERBUFFERS  g_real_wglSwapLayerBuffers  = nullptr;

omnirender::RingControlBlock* g_ring = nullptr;
std::atomic<uint64_t>         g_gl_frame_index { 0 };
std::atomic<int>              g_last_width  { 0 };
std::atomic<int>              g_last_height { 0 };
std::vector<uint8_t>          g_pixels;

void PublishGLFrame(int width, int height, HANDLE shared_color_handle, const void* pixels_bgra) {
    if (g_ring == nullptr) {
        HANDLE mapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, omnirender::kIPCBlockName);
        if (!mapping) {
            mapping = ::CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                           0, sizeof(omnirender::RingControlBlock),
                                           omnirender::kIPCBlockName);
        }
        if (!mapping) return;
        g_ring = static_cast<omnirender::RingControlBlock*>(
            ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(omnirender::RingControlBlock)));
        ::CloseHandle(mapping);
        if (!g_ring) return;

        uint32_t expected = 0;
        if (g_ring->magic.compare_exchange_strong(expected, omnirender::kIpcMagic, std::memory_order_acq_rel)) {
            g_ring->capacity.store(omnirender::kRingCapacity, std::memory_order_release);
            for (auto& slot : g_ring->slots) {
                slot.state.store(static_cast<uint32_t>(omnirender::SlotState::Free), std::memory_order_release);
                slot.fence.store(0, std::memory_order_release);
            }
        }
    }

    uint64_t p_seq = g_ring->producer_seq.load(std::memory_order_relaxed);
    uint32_t slot_idx = static_cast<uint32_t>(p_seq % omnirender::kRingCapacity);
    auto state = omnirender::GetState(g_ring->slots[slot_idx]);
    if (state != omnirender::SlotState::Free) {
        return; // backpressure: ring full, drop frame
    }
    omnirender::SetState(g_ring->slots[slot_idx], omnirender::SlotState::Captured);
    omnirender::FrameSlot* slot = &g_ring->slots[slot_idx];

    slot->payload.magic_header         = omnirender::kIpcMagic;
    slot->payload.struct_version       = omnirender::kIpcVersion_V040;
    slot->payload.frame_index          = p_seq + 1;
    slot->payload.surface_width        = static_cast<uint32_t>(width);
    slot->payload.surface_height       = static_cast<uint32_t>(height);
    slot->payload.target_width         = static_cast<uint32_t>(width);
    slot->payload.target_height        = static_cast<uint32_t>(height);
    slot->payload.color_format         = 0x00000015;  // B8G8R8A8_UNORM
    slot->payload.depth_format         = 0;
    slot->payload.motion_format        = 0;
    slot->payload.shared_color_handle  = reinterpret_cast<uint64_t>(shared_color_handle);
    slot->payload.shared_depth_handle  = 0;
    slot->payload.shared_motion_handle = 0;
    slot->payload.camera_near          = 0.1f;
    slot->payload.camera_far           = 1000.0f;
    slot->payload.fov_vertical_rad     = 1.0471975512f;

    omnirender::Halton23 jitter        = omnirender::Halton23At(slot->payload.frame_index);
    slot->payload.jitter_x             = jitter.x;
    slot->payload.jitter_y             = jitter.y;
    for (int i = 0; i < 16; ++i) {
        slot->payload.view_proj_current[i]  = 0.0f;
        slot->payload.view_proj_previous[i] = 0.0f;
    }
    slot->payload.flags = (shared_color_handle != nullptr) ? 0x01 : 0x00;  // 0x01 = Hardware Zero-Copy Shared Handle

    (void)pixels_bgra;
    slot->fence.store(slot->payload.frame_index, std::memory_order_release);
    g_ring->producer_seq.store(p_seq + 1, std::memory_order_release);
    omnirender::SetState(*slot, omnirender::SlotState::Ready);
    (void)g_gl_frame_index.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

bool InstallWGLHook() {
    std::lock_guard<std::mutex> lock(g_real_mu);
    if (g_real_opengl32 == nullptr) {
        wchar_t sysdir[MAX_PATH]{};
        if (::GetSystemDirectoryW(sysdir, MAX_PATH) == 0) return false;
        std::wstring path = std::wstring(sysdir) + L"\\opengl32.dll";
        g_real_opengl32 = ::LoadLibraryW(path.c_str());
        if (!g_real_opengl32) {
            OMNI_LOG_ERROR("InstallWGLHook: cannot load real opengl32.dll");
            return false;
        }
        g_real_wglSwapBuffers = reinterpret_cast<PFNWGLSWAPBUFFERS>(
            ::GetProcAddress(g_real_opengl32, "wglSwapBuffers"));
        g_real_wglSwapLayerBuffers = reinterpret_cast<PFNWGLSWAPLAYERBUFFERS>(
            ::GetProcAddress(g_real_opengl32, "wglSwapLayerBuffers"));
        if (!g_real_wglSwapBuffers) {
            OMNI_LOG_ERROR("InstallWGLHook: wglSwapBuffers not found in real opengl32.dll");
            return false;
        }
        g_real_resolved.store(true, std::memory_order_release);
        OMNI_LOG_INFO("InstallWGLHook: real opengl32 resolved at %ls", path.c_str());
    }
    return g_real_resolved.load(std::memory_order_acquire);
}

extern "C" BOOL WINAPI Hooked_wglSwapBuffers(HDC hdc) {
    if (hdc) {
        GLint vp[4] = {0, 0, 0, 0};
        ::glGetIntegerv(GL_VIEWPORT, vp);
        int w = vp[2];
        int h = vp[3];
        if (w > 0 && h > 0) {
            g_last_width.store(w, std::memory_order_release);
            g_last_height.store(h, std::memory_order_release);

            // Priority 1: Hardware zero-copy WGL_NV_DX_interop2 pathway
            HANDLE shared_handle = CaptureGLFrameZeroCopy(hdc, w, h);
            if (shared_handle) {
                PublishGLFrame(w, h, shared_handle, nullptr);
            } else {
                // Fallback: CPU glReadPixels staging readback
                size_t need = static_cast<size_t>(w) * h * 4;
                if (g_pixels.size() < need) g_pixels.resize(need);
                ::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, g_pixels.data());
                PublishGLFrame(w, h, nullptr, g_pixels.data());
            }
        }
    }
    if (g_real_wglSwapBuffers) {
        return g_real_wglSwapBuffers(hdc);
    }
    return FALSE;
}

extern "C" BOOL WINAPI Hooked_wglSwapLayerBuffers(HDC hdc, UINT fuPlanes) {
    BOOL rc = Hooked_wglSwapBuffers(hdc);
    if (g_real_wglSwapLayerBuffers) {
        return g_real_wglSwapLayerBuffers(hdc, fuPlanes);
    }
    return rc;
}

}  // namespace omnirender::hook
