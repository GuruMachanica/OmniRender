// filepath: modules/daemon/upscalers/xess_pipeline.h
#pragma once

#include <d3d11.h>
#include <windows.h>

namespace omnirender::daemon::upscaler {

bool InitializeXeSS(ID3D11Device* device);
void ShutdownXeSS();
bool XeSSAvailable() noexcept;

void DispatchXeSS(ID3D11DeviceContext* ctx,
                  ID3D11Texture2D* color,
                  ID3D11Texture2D* depth,
                  ID3D11Texture2D* motion,
                  ID3D11Texture2D* output,
                  UINT target_width,
                  UINT target_height);

}  // namespace omnirender::daemon::upscaler
