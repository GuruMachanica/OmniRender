// filepath: modules/daemon/capture_adapter.cpp
// Translate IPC FrameSlot payloads into core::FrameContext objects.

#include "capture_adapter.h"

#include <cstring>
#include <cmath>
#include <dxgi.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../common/ipc_protocol.h"
#include "../common/logging.h"
#include "../../core/capability/GraphicsApi.h"
#include "../../core/frame/FrameTiming.h"
#include "../../core/resources/TextureDesc.h"
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
        case DXGI_FORMAT_R8G8B8A8_UNORM:      return TextureFormat::R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return TextureFormat::R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM:      return TextureFormat::B8G8R8A8_UNORM;
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
    UnmapPixelBlock();
    pixel_upload_tex_.reset();
    pixel_tex_width_ = pixel_tex_height_ = 0;
}

// ---------------------------------------------------------------------------
// CPU pixel fallback channel (OpenGL capture without WGL_NV_DX_interop2)
// ---------------------------------------------------------------------------
const uint8_t* CaptureAdapter::MapPixelBlock(const OmniRenderIPCFrameData& p) {
    if (p.struct_version < omnirender::kIpcVersion_V050) return nullptr;
    if (p.pixel_block_name[0] == '\0' || p.pixel_data_size == 0) return nullptr;

    // The block name is stable per process; remap only if not open yet.
    if (pixel_mapping_ && pixel_view_) {
        return pixel_view_;
    }

    pixel_mapping_ = ::OpenFileMappingA(FILE_MAP_READ, FALSE, p.pixel_block_name);
    if (!pixel_mapping_) {
        OMNI_LOG_WARN("CaptureAdapter: OpenFileMappingA(%s) failed: %lu",
                      p.pixel_block_name, ::GetLastError());
        return nullptr;
    }
    pixel_view_ = static_cast<uint8_t*>(::MapViewOfFile(
        pixel_mapping_, FILE_MAP_READ, 0, 0, p.pixel_data_size));
    if (!pixel_view_) {
        OMNI_LOG_WARN("CaptureAdapter: MapViewOfFile(pixel block) failed: %lu", ::GetLastError());
        ::CloseHandle(pixel_mapping_);
        pixel_mapping_ = nullptr;
        return nullptr;
    }
    OMNI_LOG_INFO("CaptureAdapter: GL pixel block mapped (%s, %u bytes)",
                  p.pixel_block_name, p.pixel_data_size);
    return pixel_view_;
}

void CaptureAdapter::UnmapPixelBlock() noexcept {
    if (pixel_view_) {
        ::UnmapViewOfFile(pixel_view_);
        pixel_view_ = nullptr;
    }
    if (pixel_mapping_) {
        ::CloseHandle(pixel_mapping_);
        pixel_mapping_ = nullptr;
    }
}

// Upload CPU pixels into an owned BGRA8 texture (recreated on resize).
static std::shared_ptr<graphics::IGraphicsTexture>
EnsurePixelUploadTexture(graphics::IGraphicsDevice& device,
                         std::shared_ptr<graphics::IGraphicsTexture>& tex,
                         uint32_t& tex_w, uint32_t& tex_h,
                         uint32_t width, uint32_t height) {
    if (!tex || tex_w != width || tex_h != height) {
        // The GL CPU pixel block holds RGBA byte order (glReadPixels GL_RGBA),
        // so the upload texture must be R8G8B8A8 — a BGRA8 view would swap
        // red and blue. The GL hook publishes color_format=28 accordingly.
        core::TextureDesc desc{ width, height, 1, core::TextureFormat::R8G8B8A8_UNORM,
            core::TextureUsage::ShaderResource | core::TextureUsage::RenderTarget |
            core::TextureUsage::TransferDst | core::TextureUsage::TransferSrc,
            "GLCpuPixelColor" };
        tex = device.CreateTexture(desc);
        tex_w = tex ? width : 0;
        tex_h = tex ? height : 0;
    }
    return tex;
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

    // --- Color texture ------------------------------------------------------
    // Two sources: a GPU shared handle (D3D9/DXGI/GL-interop) or a CPU pixel
    // block (OpenGL fallback without interop). PixelDataCpu wins when set.
    // Pixel fields only exist in IPC v2+; older payloads must not be trusted.
    const bool pixel_cpu = (p.struct_version >= omnirender::kIpcVersion_V050) &&
                           (p.flags & static_cast<uint32_t>(IpcFlag::PixelDataCpu)) != 0;
    std::shared_ptr<graphics::IGraphicsTexture> color_tex;
    if (pixel_cpu && fc.input_resolution.width > 0 && fc.input_resolution.height > 0) {
        if (const uint8_t* pixels = MapPixelBlock(p)) {
            color_tex = EnsurePixelUploadTexture(device_, pixel_upload_tex_,
                                                 pixel_tex_width_, pixel_tex_height_,
                                                 fc.input_resolution.width,
                                                 fc.input_resolution.height);
            if (color_tex) {
                auto ctx = device_.GetImmediateContext();
                if (ctx) {
                    ctx->UploadTextureData(color_tex.get(), pixels,
                                           p.pixel_row_pitch);
                } else {
                    color_tex = nullptr;
                }
            }
        }
        if (color_tex) {
            fc.color = core::GpuTexture(color_tex);
            fc.validity.color_valid = true;
        } else {
            OMNI_LOG_WARN("CaptureAdapter: CPU pixel upload failed (frame %llu)", p.frame_index);
        }
    } else {
        color_tex = GetOrOpen(p.shared_color_handle, color_cache_);
    }
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
            pixel_upload_tex_.reset();
            pixel_tex_width_ = pixel_tex_height_ = 0;
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
