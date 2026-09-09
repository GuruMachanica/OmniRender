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
#include "../../graphics/abstraction/IGraphicsTexture.h"

namespace omnirender::daemon {

using core::FrameContext;
using core::TextureFormat;

// ---------------------------------------------------------------------------
// DXGI_FORMAT -> core::TextureFormat  (enum, not magic integers)
// ---------------------------------------------------------------------------
TextureFormat CaptureAdapter::ToDxgiFormat(uint32_t fmt_uint) noexcept {
    switch (static_cast<DXGI_FORMAT>(fmt_uint)) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:      return TextureFormat::RGBA8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return TextureFormat::R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM:      return TextureFormat::BGRA8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_UNORM:   return TextureFormat::R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:  return TextureFormat::R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32_FLOAT:           return TextureFormat::R32_FLOAT;
        case DXGI_FORMAT_R16G16_FLOAT:        return TextureFormat::R16G16_FLOAT;
        case DXGI_FORMAT_D32_FLOAT:           return TextureFormat::D32_FLOAT;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:   return TextureFormat::D24_UNORM_S8_UINT;
        case DXGI_FORMAT_D16_UNORM:           return TextureFormat::D16_UNORM;
        default:                              return TextureFormat::Unknown;
    }
}

// ---------------------------------------------------------------------------
// Handle cache  (#14)
// ---------------------------------------------------------------------------
std::shared_ptr<graphics::IGraphicsTexture>
CaptureAdapter::GetOrOpen(uint64_t handle, CachedTexture& cache) {
    if (handle == 0) return nullptr;
    if (cache.handle == handle && cache.tex) return cache.tex;  // cache hit

    // Open the shared resource and cache it.
    auto tex = device_.OpenSharedTexture(handle);
    cache.handle = tex ? handle : 0;
    cache.tex    = tex;
    return tex;
}

void CaptureAdapter::InvalidateCache() noexcept {
    color_cache_ = {};
    depth_cache_ = {};
}

// ---------------------------------------------------------------------------
// Camera validity  (#7): finite + non-zero + non-degenerate + near/far sane
// ---------------------------------------------------------------------------
static bool ValidateCameraMatrix(const float* m44,
                                 float camera_near, float camera_far) noexcept {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m44[i])) return false;

    float max_abs = 0.0f;
    for (int i = 0; i < 16; ++i)
        max_abs = max_abs < std::abs(m44[i]) ? std::abs(m44[i]) : max_abs;
    if (max_abs < 1e-6f) return false;

    // |det| of 3x3 rotation sub-block.
    const float det3 =
        m44[0] * (m44[5]*m44[10] - m44[6]*m44[9]) -
        m44[1] * (m44[4]*m44[10] - m44[6]*m44[8]) +
        m44[2] * (m44[4]*m44[9]  - m44[5]*m44[8]);
    if (std::abs(det3) < 1e-4f) return false;

    if (camera_near <= 0.0f || camera_far <= camera_near) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Adapt: build core::FrameContext from one IPC slot payload
// ---------------------------------------------------------------------------
FrameContext CaptureAdapter::Adapt(const OmniRenderIPCFrameData& p) {
    FrameContext fc;

    // --- Resolutions -------------------------------------------------------
    fc.input_resolution  = { p.surface_width, p.surface_height };
    fc.output_resolution = { p.target_width  ? p.target_width  : p.surface_width,
                              p.target_height ? p.target_height : p.surface_height };

    // --- Color texture (cached import, #14) --------------------------------
    auto color_tex = GetOrOpen(p.shared_color_handle, color_cache_);
    if (color_tex) {
        // Dimension validation (#8): warn if texture disagrees with IPC metadata.
        const uint32_t tw = color_tex->GetWidth();
        const uint32_t th = color_tex->GetHeight();
        if (tw != p.surface_width || th != p.surface_height) {
            OMNI_LOG_WARN("CaptureAdapter: color texture %ux%u != declared %ux%u (frame %llu)",
                          tw, th, p.surface_width, p.surface_height, p.frame_index);
            // Texture is mismatched — treat as invalid so downstream passes
            // don't process wrong-sized data.
            color_tex = nullptr;
            color_cache_ = {};  // force re-open next frame
        }
    }
    if (color_tex) {
        fc.color = core::GpuTexture(color_tex);
        fc.validity.color_valid = true;
    } else if (p.shared_color_handle) {
        OMNI_LOG_WARN("CaptureAdapter: OpenSharedTexture(color) failed (frame %llu)",
                      p.frame_index);
    }

    // --- Depth texture (cached import, skip if DepthRaw flag set) ----------
    const bool depth_raw   = (p.flags & static_cast<uint32_t>(IpcFlag::DepthRaw)) != 0;
    const bool camera_zero = (p.flags & static_cast<uint32_t>(IpcFlag::CameraZero)) != 0;

    if (!depth_raw && p.shared_depth_handle) {
        auto depth_tex = GetOrOpen(p.shared_depth_handle, depth_cache_);
        if (depth_tex) {
            fc.depth = core::GpuTexture(depth_tex);
            fc.validity.depth_valid = true;
        }
    }

    // --- Camera state (skip when CameraZero flag set) ----------------------
    if (!camera_zero &&
        ValidateCameraMatrix(p.view_proj_current, p.camera_near, p.camera_far)) {
        std::memcpy(fc.camera.view_proj,      p.view_proj_current,  16 * sizeof(float));
        std::memcpy(fc.camera.prev_view_proj, p.view_proj_previous, 16 * sizeof(float));
        fc.camera.near_z       = p.camera_near;
        fc.camera.far_z        = p.camera_far;
        fc.camera.fov_y_rad    = p.fov_vertical_rad;
        fc.camera.is_reverse_z =
            (p.flags & static_cast<uint32_t>(IpcFlag::ReversedZ)) != 0;
        fc.validity.camera_valid = true;
    } else if (!camera_zero) {
        OMNI_LOG_WARN("CaptureAdapter: camera matrix failed validation (frame %llu)",
                      p.frame_index);
    }

    // --- Jitter -------------------------------------------------------------
    fc.jitter.jitter_x = p.jitter_x;
    fc.jitter.jitter_y = p.jitter_y;
    fc.jitter.phase    = static_cast<uint32_t>(p.frame_index % 16u);

    // --- Timing -------------------------------------------------------------
    fc.timing.frame_index = p.frame_index;

    // --- GraphicsApi (daemon always processes via D3D11) --------------------
    fc.graphics_api = core::GraphicsApi::D3D11;

    return fc;
}

}  // namespace omnirender::daemon
