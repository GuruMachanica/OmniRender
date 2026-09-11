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

// CPU fallback capture path (used when WGL_NV_DX_interop2 is unavailable).
// glReadPixels into a persistent staging buffer, flips the bottom-up GL
// image top-down, and copies it into a named shared file mapping that the
// daemon can open cross-process. Returns true when pixels are ready.
// The mapping name/size/pitch are returned through the out params so the
// caller can publish them in the IPC payload.
bool CaptureGLFrameCpu(HDC hdc, int width, int height,
                       const char** out_block_name,
                       uint32_t* out_data_size,
                       uint32_t* out_row_pitch);

// Releases the CPU pixel staging mapping (call on resize / shutdown).
void ShutdownGLPixelBlock();

// Checks if hardware zero-copy interop is currently active.
bool IsGLInteropActive();

}  // namespace omnirender::hook
