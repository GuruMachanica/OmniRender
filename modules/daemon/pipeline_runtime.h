// filepath: modules/daemon/pipeline_runtime.h
// New-path runtime::Pipeline entry points for daemon frame dispatch.
#pragma once

#include "../common/ring_buffer.h"

namespace omnirender::graphics { class IGraphicsTexture; }

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

}  // namespace omnirender::daemon
