// filepath: modules/daemon/upscalers/xess_pipeline.cpp
// Intel XeSS pipeline wrapper delegating to XessAdapter.

#include "xess_pipeline.h"
#include "xess_adapter.h"

namespace omnirender::daemon::upscaler {

bool InitializeXeSS(ID3D11Device* device) {
    return GetGlobalXessAdapter().InitializeWithDevice(device, 3840, 2160);
}

void ShutdownXeSS() {
    GetGlobalXessAdapter().Shutdown();
}

bool XeSSAvailable() noexcept {
    return GetGlobalXessAdapter().IsRuntimeAvailable();
}

void DispatchXeSS(ID3D11DeviceContext* ctx,
                  ID3D11Texture2D* color,
                  ID3D11Texture2D* depth,
                  ID3D11Texture2D* motion,
                  ID3D11Texture2D* output,
                  UINT target_width,
                  UINT target_height) {
    (void)ctx;
    (void)output;
    (void)target_width;
    (void)target_height;
    FrameContext frame_ctx{};
    frame_ctx.color.resource = color;
    frame_ctx.depth.resource = depth;
    frame_ctx.motion.resource = motion;
    frame_ctx.validity.color_valid = (color != nullptr);
    frame_ctx.validity.depth_valid = (depth != nullptr);
    frame_ctx.validity.motion_valid = (motion != nullptr);

    GetGlobalXessAdapter().Execute(frame_ctx);
}

}  // namespace omnirender::daemon::upscaler
