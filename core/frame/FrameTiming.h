// filepath: core/frame/FrameTiming.h
#pragma once

#include <cstdint>

namespace omnirender::core {

struct FrameTiming {
    uint64_t frame_index      = 0;
    float    delta_time_ms    = 0.0f;
    float    render_time_ms   = 0.0f;
    float    fps              = 0.0f;
    uint64_t timestamp_us     = 0;
    uint32_t queue_depth      = 0;

    [[nodiscard]] constexpr float InstantaneousFps() const noexcept {
        return delta_time_ms > 0.0f ? 1000.0f / delta_time_ms : 0.0f;
    }

    void Reset() noexcept {
        frame_index = 0;
        delta_time_ms = 0.0f;
        render_time_ms = 0.0f;
        fps = 0.0f;
        timestamp_us = 0;
        queue_depth = 0;
    }
};

}  // namespace omnirender::core
