// filepath: modules/daemon/upscalers/dlss_pipeline.h
#pragma once

#include <d3d11.h>
#include <windows.h>

namespace omnirender::daemon::upscaler {

bool InitializeDLSS(ID3D11Device* device);
void ShutdownDLSS();
bool DLSSAvailable() noexcept;

void DispatchDLSS(ID3D11DeviceContext* ctx,
                  ID3D11Texture2D* color,
                  ID3D11Texture2D* depth,
                  ID3D11Texture2D* motion,
                  ID3D11Texture2D* output,
                  float jitter_x,
                  float jitter_y);

}  // namespace omnirender::daemon::upscaler
