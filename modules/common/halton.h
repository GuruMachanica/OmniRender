// filepath: modules/common/halton.h
// Halton 2,3 low-discrepancy sequence.
//
// Used for sub-pixel jitter on the projection matrix so the temporal
// reconstruction pass has a different sample position every frame.
// Converges in 4 frames for a 4x4 pixel neighborhood; the daemon
// undoes the jitter at composite time.
//
// Returns (x, y) in [-0.5, +0.5]. Multiply by texel_size to get the
// actual projection-matrix offset.

#pragma once

#include <cstdint>
#include <cmath>

namespace omnirender {

inline float HaltonSequence(uint32_t index, uint32_t base) noexcept {
    // Radical inverse function.
    float result = 0.0f;
    float f = 1.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        result += f * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

struct Halton23 {
    float x;
    float y;
};

inline Halton23 Halton23At(uint64_t frame_index) noexcept {
    // Re-index to 1-based for the radical inverse.
    uint32_t i = static_cast<uint32_t>((frame_index % 16u) + 1u);
    Halton23 h;
    h.x = HaltonSequence(i, 2) - 0.5f;
    h.y = HaltonSequence(i, 3) - 0.5f;
    return h;
}

}  // namespace omnirender
