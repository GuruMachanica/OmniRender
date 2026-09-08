// filepath: modules/daemon/test_host_math.h
#pragma once

#include <cmath>
#include <cstdint>

namespace omnirender::test {

struct Vertex {
    float x, y, z;
    float r, g, b;
};

inline const Vertex kCubeVertices[8] = {
    { -1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f }, // 0 red
    {  1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f }, // 1 green
    {  1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 1.0f }, // 2 blue
    { -1.0f,  1.0f, -1.0f, 1.0f, 1.0f, 0.0f }, // 3 yellow
    { -1.0f, -1.0f,  1.0f, 1.0f, 0.0f, 1.0f }, // 4 magenta
    {  1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 1.0f }, // 5 cyan
    {  1.0f,  1.0f,  1.0f, 1.0f, 1.0f, 1.0f }, // 6 white
    { -1.0f,  1.0f,  1.0f, 0.5f, 0.5f, 0.5f }, // 7 grey
};

inline const uint16_t kCubeIndices[36] = {
    // -Z face
    0, 1, 2,  0, 2, 3,
    // +Z face
    4, 6, 5,  4, 7, 6,
    // -X face
    0, 3, 7,  0, 7, 4,
    // +X face
    1, 5, 6,  1, 6, 2,
    // -Y face
    0, 4, 5,  0, 5, 1,
    // +Y face
    3, 2, 6,  3, 6, 7,
};

// Compose a 4x4 column-major view*proj matrix from a yaw / pitch
// and an FOV. Output is stored as 16 floats in HLSL column-major
// order. The camera orbits the origin at radius 3.5.
inline void BuildViewProj(float yaw, float pitch, float aspect, float* out_m16) {
    float cx = std::cos(pitch), sx = std::sin(pitch);
    float cy = std::cos(yaw),   sy = std::sin(yaw);

    // Camera position on a horizontal ring.
    float cam[3] = { sy * 3.5f, 0.0f, cy * 3.5f };

    // Forward = normalize(origin - cam) and apply pitch around X.
    float fwd[3] = { -sy * cx, -sx, -cy * cx };
    float right[3] = { -fwd[2], 0.0f, fwd[0] };
    float rl = std::sqrt(right[0] * right[0] + right[2] * right[2]);
    if (rl > 1e-6f) { right[0] /= rl; right[2] /= rl; }
    float up[3] = {
        right[1] * fwd[2] - right[2] * fwd[1],
        right[2] * fwd[0] - right[0] * fwd[2],
        right[0] * fwd[1] - right[1] * fwd[0],
    };

    // View matrix (column major) = R^T * T(-cam).
    float V[16] = {
        right[0],                 up[0],                -fwd[0], 0,
        right[1],                 up[1],                -fwd[1], 0,
        right[2],                 up[2],                -fwd[2], 0,
        -right[0]*cam[0] - right[1]*cam[1] - right[2]*cam[2],
        -up[0]*cam[0]    - up[1]*cam[1]    - up[2]*cam[2],
         fwd[0]*cam[0] + fwd[1]*cam[1] + fwd[2]*cam[2],
        1,
    };

    // Right-handed perspective.
    constexpr float kFov    = 1.0471975512f; // 60 degrees
    constexpr float kZNear  = 0.1f;
    constexpr float kZFar   = 100.0f;
    float f = 1.0f / std::tan(kFov * 0.5f);
    float P[16] = {
        f / aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, kZFar / (kZFar - kZNear), 1,
        0, 0, -kZFar * kZNear / (kZFar - kZNear), 0,
    };

    // P * V (column major).
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += P[k * 4 + r] * V[c * 4 + k];
            }
            out_m16[c * 4 + r] = s;
        }
    }
}

}  // namespace omnirender::test
