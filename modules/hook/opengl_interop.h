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

// ---------------------------------------------------------------------------
// Depth capture (CPU channel, same transport as the color fallback).
// Reads the current depth buffer with glReadPixels(GL_DEPTH_COMPONENT) as
// 32-bit floats, flips rows top-down, and publishes through a second
// resolution-unique named mapping. Depth range is the standard GL window
// depth [0,1] (near=0, far=1) — the daemon's DepthProvider linearizes it.
// ---------------------------------------------------------------------------
bool CaptureGLDepthCpu(int width, int height,
                       const char** out_block_name,
                       uint32_t* out_data_size,
                       uint32_t* out_row_pitch);

// Releases the depth staging mapping (call on resize / shutdown).
void ShutdownGLDepthBlock();

// ---------------------------------------------------------------------------
// Camera matrix extraction. Reads GL_MODELVIEW_MATRIX and GL_PROJECTION_MATRIX
// from the current GL context and stores the product P*MV (GL column-major
// float[16] — byte-identical to the daemon FrameContext camera convention).
// The previous frame's product is kept in prev_view_proj so the daemon can
// reproject motion between frames without guessing.
// Returns false when the context has no usable matrices (published as zeros
// and the daemon's CameraZero handling applies).
// ---------------------------------------------------------------------------
bool QueryGLCameraMatrices(float out_view_proj[16], float out_prev_view_proj[16],
                           float* out_near, float* out_far);

}  // namespace omnirender::hook
