// filepath: modules/hook/dxgi_capture_frame.cpp
// CaptureFrameDXGI — per-present slot population for the DXGI hook path.
//
// Split from dxgi_interceptor.cpp to keep each file under 300 LOC.

#include "dxgi_shared_state.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdint>

#include "../common/halton.h"
#include "../common/ipc_protocol.h"
#include "../common/logging.h"
#include "../common/ring_buffer.h"
#include "../common/shared_fence.h"
#include "dxgi_depth_capture.h"

namespace omnirender::hook {

using namespace dxgi_state;

void CaptureFrameDXGI(IDXGISwapChain* swap) {
    if (!g_ring) EnsureRingMapping();
    if (!g_ring || !EnsureSharedTextures(swap)) return;

    omnirender::FrameSlot* slot = nullptr;
    for (uint32_t i = 0; i < omnirender::kRingCapacity; ++i) {
        if (omnirender::GetState(g_ring->slots[i]) == omnirender::SlotState::Free) {
            omnirender::SetState(g_ring->slots[i], omnirender::SlotState::Captured);
            slot = &g_ring->slots[i];
            break;
        }
    }
    if (!slot) return;

    ID3D11Texture2D* backbuffer = nullptr;
    if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                               reinterpret_cast<void**>(&backbuffer)))) {
        omnirender::SetState(*slot, omnirender::SlotState::Free);
        return;
    }

    // GPU-correct copy: AcquireSync -> CopyResource -> ReleaseSync.
    IDXGIKeyedMutex* km = omnirender::GetKeyedMutex(g_shared_color_tex);
    if (km) {
        // 100 ms patience: the daemon holds the mutex for its whole pipeline
        // read window (linearize + temporal + DLSS/FSR), which can exceed a
        // single vsync on heavy frames. Skipping here only when the daemon is
        // genuinely stuck avoids flapping capture on slower GPUs.
        if (FAILED(km->AcquireSync(omnirender::kKeyedMutexProducer, 100))) {
            km->Release();
            omnirender::SetState(*slot, omnirender::SlotState::Free);
            backbuffer->Release();
            return;  // daemon is slow; skip this frame
        }
        g_d3d11_context->CopyResource(g_shared_color_tex, backbuffer);
        km->ReleaseSync(omnirender::kKeyedMutexDaemon);
        km->Release();
    } else {
        g_d3d11_context->CopyResource(g_shared_color_tex, backbuffer);
        OMNI_LOG_WARN("DXGI: keyed mutex unavailable; GPU sync not guaranteed");
    }
    backbuffer->Release();

    DXGI_SWAP_CHAIN_DESC desc{};
    swap->GetDesc(&desc);
    const uint32_t W = desc.BufferDesc.Width;
    const uint32_t H = desc.BufferDesc.Height;

    slot->payload.magic_header        = omnirender::kIpcMagic;
    slot->payload.frame_index         = ++g_ring->producer_seq;
    slot->payload.surface_width       = W;
    slot->payload.surface_height      = H;
    slot->payload.target_width        = W;
    slot->payload.target_height       = H;
    slot->payload.color_format        = static_cast<uint32_t>(desc.BufferDesc.Format);
    slot->payload.depth_format        = 41;  // DXGI_FORMAT_R32_FLOAT (0x29 is TYPELESS_PAIRING, not R32F)
    slot->payload.shared_color_handle = reinterpret_cast<uint64_t>(g_shared_color_handle);
    slot->payload.shared_depth_handle = reinterpret_cast<uint64_t>(g_shared_depth_handle);
    slot->payload.fov_vertical_rad    = 1.0471975512f;  // 60 deg default
    omnirender::Halton23 jitter       = omnirender::Halton23At(slot->payload.frame_index);
    slot->payload.jitter_x            = jitter.x;
    slot->payload.jitter_y            = jitter.y;
    slot->payload.motion_format       = 34;  // DXGI_FORMAT_R16G16_FLOAT (0x22 is not RG16F)
    slot->payload.struct_version      = omnirender::kIpcVersion_V050;
    // No CPU pixel channel on the DXGI path (GPU shared handle only).
    slot->payload.pixel_block_name[0] = '\0';
    slot->payload.pixel_data_size     = 0;
    slot->payload.pixel_row_pitch     = 0;

    // Clear matrices before filling so partial writes leave known zeros.
    for (int i = 0; i < 16; ++i) {
        slot->payload.view_proj_current[i]  = 0.0f;
        slot->payload.view_proj_previous[i] = 0.0f;
    }

    // Depth: copy tracked DSV if available.
    const bool depth_ok = CopyTrackedDepth(g_d3d11_context, g_shared_depth_tex, W, H);

    // Camera: fill from heuristic constant-buffer scanner.
    bool camera_ok = false;
    {
        float vp[16], cn = 0.1f, cf = 1000.0f;
        if (GetTrackedCamera(vp, &cn, &cf)) {
            for (int i = 0; i < 16; ++i) {
                slot->payload.view_proj_current[i]  = vp[i];
                slot->payload.view_proj_previous[i] = vp[i];  // prev unknown
            }
            slot->payload.camera_near = cn;
            slot->payload.camera_far  = cf;
            camera_ok = true;
        }
    }

    uint32_t flags = 0;
    if (!depth_ok)  flags |= static_cast<uint32_t>(omnirender::IpcFlag::DepthRaw);
    if (!camera_ok) flags |= static_cast<uint32_t>(omnirender::IpcFlag::CameraZero);
    slot->payload.flags = flags;

    slot->fence.store(slot->payload.frame_index, std::memory_order_release);
    omnirender::SetState(*slot, omnirender::SlotState::Ready);
}

}  // namespace omnirender::hook
