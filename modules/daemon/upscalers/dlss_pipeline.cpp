// filepath: modules/daemon/upscalers/dlss_pipeline.cpp
// NVIDIA DLSS pipeline wrapper delegating to DlssAdapter.

#include "dlss_pipeline.h"
#include "dlss_adapter.h"

namespace omnirender::daemon::upscaler {

bool InitializeDLSS(ID3D11Device* device) {
    return GetGlobalDlssAdapter().InitializeWithDevice(device, 3840, 2160);
}

void ShutdownDLSS() {
    GetGlobalDlssAdapter().Shutdown();
}

bool DLSSAvailable() noexcept {
    return GetGlobalDlssAdapter().IsRuntimeAvailable();
}

void DispatchDLSS(ID3D11DeviceContext* ctx,
                  ID3D11Texture2D* color,
                  ID3D11Texture2D* depth,
                  ID3D11Texture2D* motion,
                  ID3D11Texture2D* output,
                  float jitter_x,
                  float jitter_y) {
    (void)ctx;
    (void)output;
    FrameContext frame_ctx{};
    frame_ctx.color.resource = color;
    frame_ctx.depth.resource = depth;
    frame_ctx.motion.resource = motion;
    frame_ctx.jitter.offset_x = jitter_x;
    frame_ctx.jitter.offset_y = jitter_y;
    frame_ctx.validity.color_valid = (color != nullptr);
    frame_ctx.validity.depth_valid = (depth != nullptr);
    frame_ctx.validity.motion_valid = (motion != nullptr);

    GetGlobalDlssAdapter().Execute(frame_ctx);
}

}  // namespace omnirender::daemon::upscaler
