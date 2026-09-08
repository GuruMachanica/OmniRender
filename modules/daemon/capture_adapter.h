// filepath: modules/daemon/capture_adapter.h
// Bridges the IPC FrameSlot produced by the hook into a core::FrameContext
// consumable by runtime::Pipeline.
//
// Responsibilities:
//   - Import shared D3D11 texture handles from the slot payload.
//   - Wrap each texture in a GpuTexture via IGraphicsDevice::OpenSharedTexture.
//   - Populate CameraState from view_proj matrices, respecting IpcFlag::CameraZero.
//   - Populate FrameValidity respecting IpcFlag::DepthRaw / CameraZero.
//   - Clamp output resolution to pipeline limits.
//   - Translate DXGI_FORMAT to core::TextureFormat.
#pragma once

#include <cstdint>
#include "../../core/frame/FrameContext.h"
#include "../../core/frame/Resolution.h"
#include "../../core/resources/TextureFormat.h"

// Forward declarations — avoid pulling D3D11 into every TU that includes this.
struct ID3D11Device;

namespace omnirender {
struct OmniRenderIPCFrameData;
}

namespace omnirender::graphics {
class IGraphicsDevice;
}

namespace omnirender::daemon {

// CaptureAdapter: stateless per-frame bridge.
// Thread-safety: Adapt() may be called from a single consumer thread only.
class CaptureAdapter {
public:
    // Initialize with the daemon's IGraphicsDevice (D3D11 backend).
    // Must outlive all Adapt() calls.
    explicit CaptureAdapter(graphics::IGraphicsDevice& device) noexcept
        : device_(device) {}

    // Translate one IPC payload into a core::FrameContext.
    // Returns a context whose IsValid() == false if critical imports fail.
    // The resulting GpuTextures hold shared_ptr references keeping the
    // underlying D3D11 resources alive until the context is destroyed.
    [[nodiscard]] core::FrameContext Adapt(
        const OmniRenderIPCFrameData& payload) const;

    // Translate a DXGI_FORMAT integer (from payload.color_format) to the
    // core enum.  Returns TextureFormat::Unknown for unrecognized values.
    static core::TextureFormat ToDxgiFormat(uint32_t dxgi_format) noexcept;

private:
    graphics::IGraphicsDevice& device_;
};

}  // namespace omnirender::daemon
