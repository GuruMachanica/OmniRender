// filepath: core/frame/FrameContext.h
#pragma once

#include "../resources/GpuTexture.h"
#include "../camera/CameraState.h"
#include "../temporal/JitterState.h"
#include "../temporal/HistoryState.h"
#include "Resolution.h"
#include "FrameTiming.h"
#include "FrameValidity.h"
#include "../capability/GraphicsApi.h"

namespace omnirender::core {

struct FrameContext {
    GpuTexture     color;
    GpuTexture     depth;
    GpuTexture     motion;
    GpuTexture     reactive;
    GpuTexture     disocclusion;
    GpuTexture     history;
    GpuTexture     output;

    CameraState    camera;
    JitterState    jitter;
    FrameTiming    timing;

    Resolution     input_resolution;
    Resolution     output_resolution;

    GraphicsApi        graphics_api        = GraphicsApi::Unknown;
    InvalidationReason invalidation_reason = InvalidationReason::Generic;
    FrameValidity      validity;

    [[nodiscard]] bool IsValid() const noexcept {
        return color.IsValid() && !input_resolution.IsEmpty();
    }
};

}  // namespace omnirender::core
