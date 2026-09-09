// filepath: modules/daemon/capture_adapter.h
// Bridges the IPC FrameSlot produced by the hook into a core::FrameContext
// consumable by runtime::Pipeline.
//
// Responsibilities:
//   - Import shared D3D11 texture handles from the slot payload.
//   - Cache handle -> IGraphicsTexture to avoid per-frame OpenSharedResource
//     calls when the same handle is reused (typical at steady state).
//   - Wrap each texture in a GpuTexture via IGraphicsDevice::OpenSharedTexture.
//   - Populate CameraState from view_proj matrices, respecting IpcFlag::CameraZero.
//   - Validate texture dimensions against declared resolution (FrameContext::IsValid).
//   - Populate FrameValidity respecting IpcFlag::DepthRaw / CameraZero.
//   - Translate DXGI_FORMAT to core::TextureFormat.
#pragma once

#include <cstdint>
#include <memory>
#include "../../core/frame/FrameContext.h"
#include "../../core/frame/Resolution.h"
#include "../../core/resources/TextureFormat.h"

namespace omnirender {
struct OmniRenderIPCFrameData;
}

namespace omnirender::graphics {
class IGraphicsDevice;
class IGraphicsTexture;
}

namespace omnirender::daemon {

class CaptureAdapter {
public:
    explicit CaptureAdapter(graphics::IGraphicsDevice& device) noexcept
        : device_(device) {}

    // Translate one IPC payload into a core::FrameContext.
    // The resulting GpuTextures are reference-counted; texture objects are
    // reused across frames when the shared handle has not changed (#14).
    [[nodiscard]] core::FrameContext Adapt(
        const OmniRenderIPCFrameData& payload);

    // Map DXGI_FORMAT integer to core::TextureFormat.
    static core::TextureFormat ToDxgiFormat(uint32_t dxgi_format) noexcept;

    // Invalidate handle caches (call on resize / device loss).
    void InvalidateCache() noexcept;

private:
    // Per-handle texture cache: avoid OpenSharedResource every frame (#14).
    struct CachedTexture {
        uint64_t                                   handle = 0;
        std::shared_ptr<graphics::IGraphicsTexture> tex;
    };

    // Retrieve a cached texture or open it if the handle changed.
    [[nodiscard]] std::shared_ptr<graphics::IGraphicsTexture>
    GetOrOpen(uint64_t handle, CachedTexture& cache);

    graphics::IGraphicsDevice& device_;
    CachedTexture color_cache_;
    CachedTexture depth_cache_;
};

}  // namespace omnirender::daemon
