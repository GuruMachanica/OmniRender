// filepath: modules/daemon/pipeline_runtime.h
// New-path runtime::Pipeline entry points for daemon frame dispatch.
#pragma once

#include "../common/ring_buffer.h"

namespace omnirender::daemon {

// Initialize the runtime::Pipeline and its D3D11 graphics device wrapper.
// Must be called after InitializeInterop().
bool InitializeRuntimePipeline();
void ShutdownRuntimePipeline();

// Execute one frame through runtime::Pipeline.
// Returns 0 on success, <0 on failure.
int  NewPipelineFrame(omnirender::FrameSlot& slot);

// True once device is wrapped — NewPipelineFrame() handles first-frame lazy-init.
bool RuntimeDeviceReady();

// True when the new pipeline is initialized and ready to accept frames.
bool RuntimePipelineReady();

}  // namespace omnirender::daemon
