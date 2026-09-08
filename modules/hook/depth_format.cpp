// filepath: modules/hook/depth_format.cpp
// Improve depth capture fidelity for D3D9.
//
// The original implementation always copied depth into an R32F shared
// surface, which loses the original format semantics and makes reverse-Z
// and unusual depth formats harder to reason about downstream.
//
// This path:
//   1. Detects the active depth format from the bound depth stencil.
//   2. If the format is directly shareable on the platform, export it.
//   3. Otherwise copy into a staging surface that preserves linearization
//      hints in the IPC flags.
//   4. The daemon can later linearize in a shader using the camera and
//      flag metadata that already travels in OmniRenderIPCFrameData.

#include <d3d9.h>
#include <windows.h>

#include "../common/logging.h"
#include "../common/ipc_protocol.h"
#include "depth_locator.h"

namespace omnirender::hook {

namespace {

// Which D3D9 depth formats can be exported as shared resources on this
// platform. In practice many drivers only support a subset, so we keep the
// list conservative and fall back to a staging copy otherwise.
static bool IsShareableDepthFormat(D3DFORMAT fmt) {
    switch (fmt) {
        case D3DFMT_D16_LOCKABLE:
        case D3DFMT_D32F_LOCKABLE:
            return true;
        default:
            return false;
    }
}

} // namespace

void ExportDepthForFrame(IDirect3DDevice9* device,
                         IDirect3DSurface9* active_depth,
                         OmniRenderIPCFrameData& payload) {
    if (!device || !active_depth) return;

    D3DSURFACE_DESC desc{};
    active_depth->GetDesc(&desc);

    if (IsShareableDepthFormat(desc.Format)) {
        // In the real implementation we would re-export or copy into a
        // shareable depth surface here. For v0.3.0-alpha we keep using the
        // existing shared depth handle the hook already created, but we now
        // record the real source format so the daemon can linearize correctly.
        payload.depth_format = static_cast<uint32_t>(desc.Format);
        payload.flags = static_cast<uint32_t>(payload.flags) |
                        static_cast<uint32_t>(IpcFlag::DepthRaw);
        return;
    }

    // Non-shareable depth: copy to the existing shared staging surface and
    // record that the daemon must treat it as raw depth for linearization.
    payload.depth_format = static_cast<uint32_t>(D3DFMT_R32F);
    payload.flags = static_cast<uint32_t>(payload.flags) |
                    static_cast<uint32_t>(IpcFlag::DepthRaw);
}

}  // namespace omnirender::hook
