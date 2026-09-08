// filepath: core/temporal/TemporalPassBase.h
// Shared types, constant-buffer layout, and matrix helpers for all three
// temporal compute passes (MotionReproject, Disocclusion, ReactiveMask).
#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include "../frame/FrameContext.h"

namespace omnirender::core::temporal {

// GPU constant buffer layout -- must match all temporal HLSL shaders (cbuffer b0).
// std140/HLSL packing: each member is 16-byte aligned.
struct alignas(16) ReprojectionCB {
    float  prev_view_proj[16];   // previous frame view-projection (row-major)
    float  inv_view_proj[16];    // inverse of current view-projection
    float  input_width_f;        // input render target width (float)
    float  input_height_f;       // input render target height (float)
    float  rcp_width;            // 1.0 / input_width
    float  rcp_height;           // 1.0 / input_height
    float  near_z;
    float  far_z;
    uint32_t frame_index;
    float  _pad;
};
static_assert(sizeof(ReprojectionCB) % 16 == 0, "ReprojectionCB must be 16-byte padded");

// Fill ReprojectionCB from a FrameContext.
inline ReprojectionCB MakeReprojectionCB(const FrameContext& fc) noexcept {
    ReprojectionCB cb{};
    std::memcpy(cb.prev_view_proj, fc.camera.prev_view_proj, sizeof(cb.prev_view_proj));
    std::memcpy(cb.inv_view_proj,  fc.camera.inv_view_proj,  sizeof(cb.inv_view_proj));
    cb.input_width_f  = static_cast<float>(fc.input_resolution.width);
    cb.input_height_f = static_cast<float>(fc.input_resolution.height);
    cb.rcp_width      = (fc.input_resolution.width  > 0) ? 1.0f / cb.input_width_f  : 0.0f;
    cb.rcp_height     = (fc.input_resolution.height > 0) ? 1.0f / cb.input_height_f : 0.0f;
    cb.near_z         = fc.camera.near_z;
    cb.far_z          = fc.camera.far_z;
    cb.frame_index    = 0u;
    cb._pad           = 0.0f;
    return cb;
}

// Simple 4x4 row-major transform of a homogeneous point.
inline bool TransformPoint(const float m[16], float x, float y, float z, float w,
                           float& ox, float& oy, float& oz, float& ow) noexcept {
    ox = m[0]*x + m[4]*y + m[8]*z  + m[12]*w;
    oy = m[1]*x + m[5]*y + m[9]*z  + m[13]*w;
    oz = m[2]*x + m[6]*y + m[10]*z + m[14]*w;
    ow = m[3]*x + m[7]*y + m[11]*z + m[15]*w;
    return std::abs(ow) > 1e-7f;
}

// Compute thread group count (ceil division).
inline uint32_t GroupCount(uint32_t size, uint32_t tile) noexcept {
    return (size + tile - 1) / tile;
}

}  // namespace omnirender::core::temporal
