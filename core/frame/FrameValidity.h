// filepath: core/frame/FrameValidity.h
#pragma once

namespace omnirender::core {

struct FrameValidity {
    // NOTE: fields are positionally initialized with aggregate braces in
    // several call sites (runtime/Pipeline.cpp, tests). Append new fields
    // at the END only, never in the middle.
    bool color_valid     = false;
    bool depth_valid     = false;
    bool motion_valid    = false;
    bool reactive_valid  = false;
    bool disocc_valid    = false;
    bool history_valid   = false;
    bool camera_valid    = false;
    bool jitter_valid    = false;
    bool depth_linear_valid = false;  // DepthProvider output (linearized [0,1])

    void Reset() noexcept {
        color_valid = depth_valid = motion_valid = reactive_valid = false;
        disocc_valid = history_valid = camera_valid = jitter_valid = false;
        depth_linear_valid = false;
    }
};

}  // namespace omnirender::core
