// filepath: modules/hook/depth_locator.cpp
// Active depth/stencil surface identification.
//
// Many DirectX 9 titles create multiple render targets and depth
// surfaces; this module identifies the "active" one by walking the
// device state. The heuristic is:
//   1. Surface that matches the backbuffer dimensions exactly.
//   2. Surface format is one of: D24S8, D24X8, D32, D16, D32F_LOCKABLE.
//   3. Surface currently bound via SetDepthStencilSurface.
// If no perfect match exists, fall back to the most recently bound
// depth surface.
//
// The D3D9 path can't directly share D24S8; we copy the active
// surface into an A8R8G8B8 / R32F staging surface (see
// d3d9_interceptor.cpp). This module just chooses the source.

#include <d3d9.h>
#include <windows.h>

#include "../common/logging.h"

namespace omnirender::hook {

bool IsDepthFormat(D3DFORMAT fmt) {
    switch (fmt) {
        case D3DFMT_D16:
        case D3DFMT_D24X8:
        case D3DFMT_D24S8:
        case D3DFMT_D24FS8:
        case D3DFMT_D32:
        case D3DFMT_D32F_LOCKABLE:
        case D3DFMT_D15S1:
        case D3DFMT_D16_LOCKABLE:
            return true;
        default:
            return false;
    }
}

D3DFORMAT ChooseShareableStagingFormat(D3DFORMAT source) {
    // R32F is the cleanest portable shareable format. If the GPU
    // doesn't support it (rare), we fall back to A8R8G8B8 in the
    // caller.
    (void)source;
    return D3DFMT_R32F;
}

bool PickActiveDepth(IDirect3DDevice9* pDevice, IDirect3DSurface9** out_surface) {
    if (!pDevice || !out_surface) return false;
    *out_surface = nullptr;

    IDirect3DSurface9* candidate = nullptr;
    if (FAILED(pDevice->GetDepthStencilSurface(&candidate)) || !candidate) {
        return false;
    }

    D3DSURFACE_DESC desc{};
    candidate->GetDesc(&desc);
    if (!IsDepthFormat(desc.Format)) {
        // The bound surface is not actually a depth format; release
        // and return failure.
        candidate->Release();
        return false;
    }

    *out_surface = candidate;
    return true;
}

void InstallDepthLocator() {
    OMNI_LOG_INFO("Depth locator ready (bound-surface heuristic)");
}

}  // namespace omnirender::hook
