// filepath: modules/hook/dxgi_shared_state.h
// Shared globals between dxgi_interceptor.cpp and dxgi_capture_frame.cpp.
// MUST be included in exactly one TU with DXGI_DEFINE_GLOBALS defined,
// and in all other TUs without it.
#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include "../common/ring_buffer.h"

namespace omnirender::hook::dxgi_state {

#ifdef DXGI_DEFINE_GLOBALS
#  define DXGI_EXTERN
#else
#  define DXGI_EXTERN extern
#endif

DXGI_EXTERN omnirender::RingControlBlock* g_ring;
DXGI_EXTERN ID3D11Device*                g_d3d11_device;
DXGI_EXTERN ID3D11DeviceContext*         g_d3d11_context;
DXGI_EXTERN ID3D11Texture2D*             g_shared_color_tex;
DXGI_EXTERN ID3D11Texture2D*             g_shared_depth_tex;
DXGI_EXTERN HANDLE                       g_shared_color_handle;
DXGI_EXTERN HANDLE                       g_shared_depth_handle;
DXGI_EXTERN uint32_t                     g_tex_width;
DXGI_EXTERN uint32_t                     g_tex_height;
DXGI_EXTERN DXGI_FORMAT                  g_tex_format;

#undef DXGI_EXTERN

void EnsureRingMapping() noexcept;
bool EnsureSharedTextures(IDXGISwapChain* swap) noexcept;

}  // namespace omnirender::hook::dxgi_state
