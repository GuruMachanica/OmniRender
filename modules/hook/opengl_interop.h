// filepath: modules/hook/opengl_interop.h
// Zero-copy OpenGL <-> Direct3D 11 GPU texture interop via WGL_NV_DX_interop2.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>

namespace omnirender::hook {

// Queries WGL_NV_DX_interop2 and initializes D3D11 shared resource bridge.
bool InitializeGLInterop(HDC hdc, int width, int height);

// Releases all D3D11 and WGL interop handles.
void ShutdownGLInterop();

// Captures current OpenGL backbuffer directly to D3D11 shared texture on GPU.
// Returns DXGI shared handle on success, nullptr if interop is unsupported.
HANDLE CaptureGLFrameZeroCopy(HDC hdc, int width, int height);

// Checks if hardware zero-copy interop is currently active.
bool IsGLInteropActive();

}  // namespace omnirender::hook
