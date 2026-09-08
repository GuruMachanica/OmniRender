// filepath: modules/daemon/processing.h
#pragma once

#include <d3d11.h>
#include <cstdint>

#include "../common/ipc_protocol.h"

namespace omnirender::daemon {

// Lifecycle.
int  InitializeProcessing();
void ShutdownProcessing();

// True when the bounded work buffer, history buffers, and any loaded
// compute shaders are ready to receive frames.
bool ProcessingCapabilities();

// Process a single frame. The caller retains ownership of color/depth
// and is expected to Release() them after the call returns. Returns
// 0 on success, negative on internal failure.
int  PrepareFrame(OmniRenderIPCFrameData const& payload,
                  ID3D11Texture2D* color,
                  ID3D11Texture2D* depth,
                  bool enable_reconstruction,
                  bool enable_upscale);

// Build per-pixel 2D motion vectors via depth+view-proj reprojection
// (the bounded v0.4.0-alpha path). Returns 0 on success, negative on
// failure (in which case the caller should fall back to optical flow).
int  BuildMotionVectors(OmniRenderIPCFrameData const& payload,
                        ID3D11Texture2D* current_depth,
                        ID3D11Texture2D* motion_uav);

// Build a reactive mask identifying pixels where history samples
// should be discarded (particles, UI, transparency). Returns 0 on
// success, negative on failure (in which case the daemon treats every
// pixel as "trust history").
int  BuildReactiveMask(OmniRenderIPCFrameData const& payload,
                       ID3D11Texture2D* current_color,
                       ID3D11Texture2D* previous_color,
                       ID3D11Texture2D* current_depth,
                       ID3D11Texture2D* reactive_uav);

// Build disocclusion mask comparing current depth to reprojected previous depth.
int  BuildDisocclusionMask(OmniRenderIPCFrameData const& payload,
                           ID3D11Texture2D* current_depth,
                           ID3D11Texture2D* previous_depth,
                           ID3D11Texture2D* motion_vectors,
                           ID3D11Texture2D* disocclusion_uav);

// Resolve temporal accumulation (reconstruct shader) using color, history,
// motion vectors, disocclusion mask, and reactive mask.
int  ResolveTemporal(ID3D11DeviceContext* ctx,
                     UINT width, UINT height,
                     bool enable_reconstruction);

// End-of-frame commitment: rotates history and preserves depth for the next frame.
void EndFrameProcessing(ID3D11DeviceContext* ctx, ID3D11Texture2D* current_depth);
void InvalidateTemporalHistory() noexcept;

// Dispatch the compute tone-map on the bounded work texture. No-op when
// the shader is not loaded. Called by the pipeline.
void DispatchTonemap(ID3D11DeviceContext* ctx, UINT width, UINT height);

// Dispatch Screen-Space Ray Tracing (SSR + RTAO) on the bounded work texture.
void DispatchRayTracing(ID3D11DeviceContext* ctx, UINT width, UINT height);

// Present the bounded work texture into the swap chain. The pipeline
// calls this with the work SRV; the passthrough path calls it with the
// imported color SRV.
void PresentProcessed(ID3D11DeviceContext* ctx,
                      ID3D11ShaderResourceView* source_srv,
                      UINT target_width,
                      UINT target_height,
                      bool enable_upscale);

// Accessors used by the presentation loop and the pipeline.
ID3D11ShaderResourceView* ProcessingWorkSrv() noexcept;
ID3D11ShaderResourceView* ProcessingHistorySrv() noexcept;

// In-engine Frame Debugger (F12 cycle):
// 0=Final, 1=RawColor, 2=Depth, 3=Motion, 4=Reactive, 5=Disocclusion, 6=History
ID3D11ShaderResourceView* GetDebugChannelSrv(int channel) noexcept;
int  CycleDebugMode() noexcept;
int  GetDebugMode() noexcept;

}  // namespace omnirender::daemon
