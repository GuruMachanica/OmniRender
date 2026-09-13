// filepath: modules/daemon/pipeline_runtime.h
// New-path runtime::Pipeline entry points for daemon frame dispatch.
#pragma once

#include "../common/ring_buffer.h"

namespace omnirender::graphics { class IGraphicsTexture; }

// D3D11 SRV forward declaration (composes with <d3d11.h> in any order;
// same pattern as capture_adapter.h).
struct ID3D11ShaderResourceView;

namespace omnirender::daemon {

// Initialize the runtime::Pipeline and its D3D11 graphics device wrapper.
// Must be called after InitializeInterop().
bool InitializeRuntimePipeline();
void ShutdownRuntimePipeline();

// Execute one frame. Returns 0 on success, <0 on failure.
int  NewPipelineFrame(omnirender::FrameSlot& slot);

// True once device is wrapped — NewPipelineFrame() handles first-frame lazy-init.
bool RuntimeDeviceReady();

// True once Pipeline::Initialize() has succeeded at least once.
bool RuntimePipelineReady();

// Returns the reconstructed output texture from the last successful frame,
// or nullptr if none. Do NOT hold this pointer across frames (#15).
graphics::IGraphicsTexture* GetLastOutputTexture() noexcept;

// Dimensions of the last processed output (0,0 before the first frame).
// Presentation uses this to size the swapchain to the *upscaled* resolution
// instead of the game's input resolution.
void GetLastOutputResolution(uint32_t* width, uint32_t* height) noexcept;

// Name of the reconstruction backend actually attached to the runtime
// pipeline ("NVIDIA DLSS", "Intel XeSS", "FSR 1.0 (EASU+RCAS)", or
// "Passthrough"). The HUD shows this next to the input→output resolution.
const char* GetActiveBackendName() noexcept;

// F12 frame-debugger channel (0 = final output, 1..5 = pipeline views).
// Returns a null SRV when the requested view has no data this frame; the
// caller falls back to the pipeline output.
ID3D11ShaderResourceView* GetRuntimeDebugSrv(int channel) noexcept;

// Current debug channel (read back for the HUD label).
int GetRuntimeDebugMode() noexcept;

// Advance to the next debug channel; returns the new channel.
int CycleRuntimeDebugMode() noexcept;

}  // namespace omnirender::daemon
