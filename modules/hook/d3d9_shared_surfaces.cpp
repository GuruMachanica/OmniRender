// filepath: modules/hook/d3d9_shared_surfaces.cpp
// D3D9 shared surfaces and IPC frame publishing.

#include "d3d9_shared_surfaces.h"

#include <windows.h>
#include <d3d9.h>
#include <cstdint>

#include "../common/halton.h"
#include "../common/logging.h"
#include "../common/ring_buffer.h"

namespace omnirender::hook {

namespace {

IDirect3DSurface9* g_shared_color        = nullptr;
HANDLE             g_shared_color_handle = nullptr;
IDirect3DSurface9* g_shared_depth        = nullptr;
HANDLE             g_shared_depth_handle = nullptr;

omnirender::RingControlBlock* g_ring     = nullptr;

}  // namespace

void EnsureSharedSurfaces(IDirect3DDevice9* device) {
    if (!device || g_shared_color) return;

    UINT width = 0, height = 0;
    IDirect3DSurface9* back = nullptr;
    if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && back) {
        D3DSURFACE_DESC desc{};
        back->GetDesc(&desc);
        width  = desc.Width;
        height = desc.Height;
        back->Release();
    }
    if (width == 0 || height == 0) {
        width  = ::GetSystemMetrics(SM_CXSCREEN);
        height = ::GetSystemMetrics(SM_CYSCREEN);
    }

    HRESULT hr = device->CreateRenderTarget(
        width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
        &g_shared_color, &g_shared_color_handle);
    if (FAILED(hr)) {
        // Fallback without shared handle
        hr = device->CreateRenderTarget(
            width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
            &g_shared_color, nullptr);
    }
    if (FAILED(hr)) {
        hr = device->CreateOffscreenPlainSurface(
            width, height, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
            &g_shared_color, nullptr);
    }
    if (FAILED(hr) || !g_shared_color) {
        OMNI_LOG_ERROR("CreateRenderTarget(color) failed: 0x%08lX", hr);
        return;
    }

    hr = device->CreateRenderTarget(
        width, height, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0, FALSE,
        &g_shared_depth, &g_shared_depth_handle);
    if (FAILED(hr)) {
        device->CreateRenderTarget(
            width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
            &g_shared_depth, nullptr);
    }
    OMNI_LOG_INFO("D3D9 shared surfaces ready: %ux%u (handle=%p)", width, height, g_shared_color_handle);
}

void DestroySharedSurfaces() {
    if (g_shared_color) { g_shared_color->Release(); g_shared_color = nullptr; }
    g_shared_color_handle = nullptr;
    if (g_shared_depth) { g_shared_depth->Release(); g_shared_depth = nullptr; }
    g_shared_depth_handle = nullptr;
}

bool IsRingMappingActive() {
    return g_ring != nullptr;
}

void EnsureRingMapping() {
    if (g_ring) return;
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
        OMNI_LOG_ERROR("CreateFileMappingA/OpenFileMappingA failed: %lu", ::GetLastError());
        return;
    }
    g_ring = static_cast<omnirender::RingControlBlock*>(
        ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                        sizeof(omnirender::RingControlBlock)));
    ::CloseHandle(mapping);
    if (!g_ring) {
        OMNI_LOG_ERROR("MapViewOfFile failed: %lu", ::GetLastError());
        return;
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
    OMNI_LOG_INFO("IPC ring attached (capacity=%u)",
                  static_cast<unsigned>(omnirender::kRingCapacity));
}

void CaptureD3D9Frame(IDirect3DDevice9* device) {
    if (!g_ring || !g_shared_color) return;

    uint64_t p_seq = g_ring->producer_seq.load(std::memory_order_relaxed);
    uint32_t slot_idx = static_cast<uint32_t>(p_seq % omnirender::kRingCapacity);
    auto state = omnirender::GetState(g_ring->slots[slot_idx]);
    if (state != omnirender::SlotState::Free) {
        return;  // backpressure: slot not yet released by consumer
    }
    omnirender::SetState(g_ring->slots[slot_idx], omnirender::SlotState::Captured);
    omnirender::FrameSlot* slot = &g_ring->slots[slot_idx];

    IDirect3DSurface9* back = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) {
        omnirender::SetState(*slot, omnirender::SlotState::Free);
        return;
    }
    HRESULT hr = device->StretchRect(back, nullptr, g_shared_color, nullptr, D3DTEXF_NONE);
    back->Release();
    if (FAILED(hr)) {
        omnirender::SetState(*slot, omnirender::SlotState::Free);
        return;
    }

    IDirect3DSurface9* depth = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&depth)) && depth) {
        device->StretchRect(depth, nullptr, g_shared_depth, nullptr, D3DTEXF_NONE);
        depth->Release();
    }

    D3DSURFACE_DESC desc{};
    g_shared_color->GetDesc(&desc);
    slot->payload.magic_header         = omnirender::kIpcMagic;
    slot->payload.struct_version       = omnirender::kIpcVersion_V040;
    slot->payload.frame_index          = p_seq + 1;
    slot->payload.surface_width        = desc.Width;
    slot->payload.surface_height       = desc.Height;
    slot->payload.target_width         = desc.Width;
    slot->payload.target_height        = desc.Height;
    slot->payload.color_format         = 0x00000015;  // DXGI_FORMAT_B8G8R8A8_UNORM
    slot->payload.depth_format         = 0x00000029;  // DXGI_FORMAT_R32_FLOAT
    slot->payload.motion_format        = 0x00000022;  // DXGI_FORMAT_R16G16_FLOAT
    slot->payload.shared_color_handle  = reinterpret_cast<uint64_t>(g_shared_color_handle);
    slot->payload.shared_depth_handle  = reinterpret_cast<uint64_t>(g_shared_depth_handle);
    slot->payload.shared_motion_handle = 0;           // motion produced by daemon
    slot->payload.camera_near          = 0.1f;
    slot->payload.camera_far           = 1000.0f;
    slot->payload.fov_vertical_rad     = 1.0471975512f;  // 60 deg

    omnirender::Halton23 jitter = omnirender::Halton23At(slot->payload.frame_index);
    slot->payload.jitter_x             = jitter.x;
    slot->payload.jitter_y             = jitter.y;
    slot->payload.flags                = 0;

    static float s_prev_vp[16] = {};
    D3DMATRIX view{}, proj{}, vp{};
    if (SUCCEEDED(device->GetTransform(D3DTS_VIEW, &view)) &&
        SUCCEEDED(device->GetTransform(D3DTS_PROJECTION, &proj))) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += view.m[i][k] * proj.m[k][j];
                }
                vp.m[i][j] = sum;
            }
        }
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                slot->payload.view_proj_current[c * 4 + r] = vp.m[r][c];
            }
        }
        bool first = true;
        for (int i = 0; i < 16; ++i) {
            if (s_prev_vp[i] != 0.0f) { first = false; break; }
        }
        if (first) {
            for (int i = 0; i < 16; ++i) {
                slot->payload.view_proj_previous[i] = slot->payload.view_proj_current[i];
                s_prev_vp[i] = slot->payload.view_proj_current[i];
            }
        } else {
            for (int i = 0; i < 16; ++i) {
                slot->payload.view_proj_previous[i] = s_prev_vp[i];
            }
        }
        for (int i = 0; i < 16; ++i) {
            s_prev_vp[i] = slot->payload.view_proj_current[i];
        }
    }

    slot->fence.store(slot->payload.frame_index, std::memory_order_release);
    g_ring->producer_seq.store(p_seq + 1, std::memory_order_release);
    omnirender::SetState(*slot, omnirender::SlotState::Ready);
}

}  // namespace omnirender::hook
