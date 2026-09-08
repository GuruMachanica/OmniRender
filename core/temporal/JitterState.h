// filepath: core/temporal/JitterState.h
#pragma once

#include <cstdint>

namespace omnirender::core {

struct JitterState {
    float    jitter_x   = 0.0f;
    float    jitter_y   = 0.0f;
    uint32_t phase      = 0;
    uint32_t phase_max  = 16;

    static float Halton(uint32_t index, uint32_t base) noexcept {
        float result = 0.0f;
        float f = 1.0f / static_cast<float>(base);
        uint32_t i = index;
        while (i > 0) {
            result += static_cast<float>(i % base) * f;
            i /= base;
            f /= static_cast<float>(base);
        }
        return result;
    }

    void Advance() noexcept {
        phase = (phase + 1) % phase_max;
        jitter_x = Halton(phase + 1, 2) - 0.5f;
        jitter_y = Halton(phase + 1, 3) - 0.5f;
    }
};

}  // namespace omnirender::core
