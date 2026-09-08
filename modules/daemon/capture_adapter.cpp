// filepath: modules/daemon/capture_adapter.cpp
// Translate IPC FrameSlot payloads into core::FrameContext objects.

#include "capture_adapter.h"

#include <cstring>
#include <cmath>
#include <dxgi.h>

#include "../common/ipc_protocol.h"
#include "../common/logging.h"
#include "../../core/capability/GraphicsApi.h"
#include "../../core/frame/FrameTiming.h"
#include "../../graphics/abstraction/IGraphicsDevice.h"

namespace omnirender::daemon {

using core::FrameContext;
using core::TextureFormat;

// ---------------------------------------------------------------------------
// DXGI_FORMAT -> core::TextureFormat
// ---------------------------------------------------------------------------
TextureFormat CaptureAdapter::ToDxgiFormat(uint32_t fmt) noexcept {
    // Only the formats actually produced by the hook interceptors.
    switch (fmt) {
        case 28:  return TextureFormat::RGBA8_UNORM;        // DXGI_FORMAT_R8G8B8A8_UNORM
        case 29:  return TextureFormat::R32_FLOAT;          // DXGI_FORMAT_R32_FLOAT
        case 34:  return TextureFormat::RGBA16_FLOAT;       // DXGI_FORMAT_R16G16B16A16_FLOAT
        case 10:  return TextureFormat::RGBA16_FLOAT;       // DXGI_FORMAT_R16G16B16A16_FLOAT (alias)
        case 87:  return TextureFormat::BGRA8_UNORM;        // DXGI_FORMAT_B8G8R8A8_UNORM
        default:  return TextureFormat::Unknown;
    }
}

// ---------------------------------------------------------------------------
// Adapt: build core::FrameContext from one IPC slot payload
// ---------------------------------------------------------------------------
FrameContext CaptureAdapter::Adapt(const OmniRenderIPCFrameData& p) const {
    FrameContext fc;

    // --- Resolutions -------------------------------------------------------
    fc.input_resolution  = { p.surface_width,  p.surface_height };
    fc.output_resolution = { p.target_width  ? p.target_width  : p.surface_width,
                              p.target_height ? p.target_height : p.surface_height };

    // --- Import color texture ----------------------------------------------
    if (p.shared_color_handle) {
        auto tex = device_.OpenSharedTexture(p.shared_color_handle);
        if (tex) {
            fc.color = core::GpuTexture(std::move(tex));
            fc.validity.color_valid = true;
        } else {
            OMNI_LOG_WARN("CaptureAdapter: OpenSharedTexture(color) failed for frame %llu",
                          p.frame_index);
        }
    }

    // --- Import depth texture (only if DepthRaw flag is NOT set) -----------
    const bool depth_raw = (p.flags & static_cast<uint32_t>(IpcFlag::DepthRaw)) != 0;
    if (!depth_raw && p.shared_depth_handle) {
        auto tex = device_.OpenSharedTexture(p.shared_depth_handle);
        if (tex) {
            fc.depth = core::GpuTexture(std::move(tex));
            fc.validity.depth_valid = true;
        }
    }

    // --- Camera state (only if CameraZero flag is NOT set) -----------------
    const bool camera_zero = (p.flags & static_cast<uint32_t>(IpcFlag::CameraZero)) != 0;
    if (!camera_zero) {
        std::memcpy(fc.camera.view_proj,      p.view_proj_current,  sizeof(float) * 16);
        std::memcpy(fc.camera.prev_view_proj, p.view_proj_previous, sizeof(float) * 16);
        fc.camera.near_z     = p.camera_near;
        fc.camera.far_z      = p.camera_far;
        fc.camera.fov_y_rad  = p.fov_vertical_rad;
        fc.camera.is_reverse_z = (p.flags &
            static_cast<uint32_t>(IpcFlag::ReversedZ)) != 0;
        // Validity: camera is valid if the matrix is non-identity.
        for (int i = 0; i < 16 && !fc.validity.camera_valid; ++i)
            if (std::abs(p.view_proj_current[i]) > 1e-6f)
                fc.validity.camera_valid = true;
    }

    // --- Jitter -------------------------------------------------------------
    fc.jitter.jitter_x    = p.jitter_x;
    fc.jitter.jitter_y    = p.jitter_y;
    fc.jitter.phase       = static_cast<uint32_t>(p.frame_index % 16u);

    // --- Timing -------------------------------------------------------------
    fc.timing.frame_index = p.frame_index;

    // --- GraphicsApi -------------------------------------------------------
    fc.graphics_api = core::GraphicsApi::D3D11;

    return fc;
}

}  // namespace omnirender::daemon
