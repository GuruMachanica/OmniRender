// filepath: modules/hook/dxgi_depth_capture.h
// Depth-buffer and camera-matrix capture for the DXGI hook path.
//
// Hooks ID3D11DeviceContext::OMSetRenderTargets to track the currently bound
// depth-stencil view, and VSSetConstantBuffers to heuristically scan vertex
// shader constant buffers for a view-projection matrix.
//
// These are best-effort: depth is valid when a game binds a D32_FLOAT or
// D24S8 DSV on the immediate context. Camera is valid only when a constant
// buffer in VS slots 0-3 contains a 4x4 float matrix that passes the
// finite/non-degenerate/near-far checks in ValidateCameraMatrix().
//
// Both pieces of data are captured per-Present and published into the IPC slot.
#pragma once

#include <d3d11.h>
#include <cstdint>

namespace omnirender::hook {

// Install OMSetRenderTargets and VSSetConstantBuffers hooks on a D3D11 context.
// Must be called once after the context is obtained from the swap chain.
void InstallContextHooks(ID3D11DeviceContext* ctx) noexcept;

// Copy the last tracked depth-stencil resource to dst_tex (must be same dims).
// Returns true if a valid DSV was tracked and the copy succeeded.
bool CopyTrackedDepth(ID3D11DeviceContext* ctx, ID3D11Texture2D* dst_tex,
                      uint32_t expected_w, uint32_t expected_h) noexcept;

// Fill 16 floats with the last heuristically detected view-projection matrix.
// Returns true if a plausible matrix was found; false if no valid matrix seen.
bool GetTrackedCamera(float out_view_proj[16],
                      float* out_near, float* out_far) noexcept;

// Reset tracked state (call on resize / device loss).
void ResetTrackedState() noexcept;

}  // namespace omnirender::hook
