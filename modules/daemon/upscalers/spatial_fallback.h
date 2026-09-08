// filepath: modules/daemon/upscalers/spatial_fallback.h
#pragma once

#include <d3d11.h>
#include <windows.h>

namespace omnirender::daemon::upscaler {

bool InitializeSpatialFallback(ID3D11Device* device);
void ShutdownSpatialFallback();

void DispatchSpatialFallback(ID3D11DeviceContext* ctx,
                             ID3D11ShaderResourceView* source_srv,
                             ID3D11UnorderedAccessView* target_uav,
                             UINT source_width, UINT source_height,
                             UINT target_width, UINT target_height);

bool SpatialFallbackAvailable() noexcept;

}  // namespace omnirender::daemon::upscaler
