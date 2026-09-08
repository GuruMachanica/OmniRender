// filepath: modules/daemon/pipeline.h
#pragma once

#include <cstdint>

#include "../common/ring_buffer.h"

namespace omnirender::daemon {

// Lifecycle.
int  InitializePipeline();
void ShutdownPipeline();

// Per-feature toggles (used by the presentation loop and by tests).
void SetReconstructionEnabled(bool enable);
void SetUpscaleEnabled(bool enable);
void SetTonemapEnabled(bool enable);

// True when the processing module has initialized successfully.
bool PipelineReady();

// Run the bounded v0.3.0-alpha pipeline on a single frame slot. The
// caller is responsible for releasing the imported color/depth
// handles. Returns 0 on success, negative on internal failure.
int  RunPipelineFrame(FrameSlot& slot);

// Run a no-processing passthrough present for the given frame slot.
// Fallback when no compute shaders are available.
int  RunPassthroughFrame(FrameSlot& slot);

// VRAM-safe target resolution clamp. Exposed for tests and external
// callers that need to know the ceiling applied to a target.
void ClampTargetResolution(UINT source_width, UINT source_height,
                           UINT& target_width, UINT& target_height);

}  // namespace omnirender::daemon
