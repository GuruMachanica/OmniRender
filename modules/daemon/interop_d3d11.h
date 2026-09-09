// filepath: modules/daemon/interop_d3d11.h
#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <cstdint>

namespace omnirender::daemon {

bool InitializeInterop(void* pAdapter = nullptr);

ID3D11Device*        Device();
ID3D11DeviceContext* Context();
uint64_t             CurrentAdapterLuid();

bool ImportColorHandle(HANDLE h, ID3D11Texture2D** out);
bool ImportDepthHandle(HANDLE h, ID3D11Texture2D** out);

// Keyed-mutex GPU sync helpers.
// Call AcquireKeyedMutex() on the texture opened via ImportColorHandle() BEFORE
// reading it. Call ReleaseKeyedMutex() AFTER you are done.
// Returns true on success; false on timeout or missing keyed mutex support.
bool AcquireKeyedMutex(ID3D11Texture2D* tex, UINT32 timeout_ms = 100) noexcept;
void ReleaseKeyedMutex(ID3D11Texture2D* tex) noexcept;

}  // namespace omnirender::daemon

