// filepath: modules/daemon/interop_d3d11.h
#pragma once

#include <d3d11.h>
#include <windows.h>
#include <cstdint>

namespace omnirender::daemon {

bool InitializeInterop(void* pAdapter = nullptr);

ID3D11Device*        Device();
ID3D11DeviceContext* Context();
uint64_t             CurrentAdapterLuid();

bool ImportColorHandle(HANDLE h, ID3D11Texture2D** out);
bool ImportDepthHandle(HANDLE h, ID3D11Texture2D** out);

}  // namespace omnirender::daemon

