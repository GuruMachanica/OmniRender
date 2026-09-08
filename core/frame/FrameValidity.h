// filepath: core/frame/FrameValidity.h
#pragma once

namespace omnirender::core {

struct FrameValidity {
    bool color_valid     = false;
    bool depth_valid     = false;
    bool motion_valid    = false;
    bool reactive_valid  = false;
    bool disocc_valid    = false;
    bool history_valid   = false;
    bool camera_valid    = false;
    bool jitter_valid    = false;

    void Reset() noexcept {
        color_valid = depth_valid = motion_valid = reactive_valid = false;
        disocc_valid = history_valid = camera_valid = jitter_valid = false;
    }
};

}  // namespace omnirender::core
