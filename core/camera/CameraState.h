// filepath: core/camera/CameraState.h
#pragma once

#include <cmath>
#include <cstring>

namespace omnirender::core {

struct CameraState {
    float view[16]                = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float proj[16]                = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float view_proj[16]           = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float prev_view_proj[16]      = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float inv_view_proj[16]       = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

    float camera_pos[3]           = { 0.0f, 0.0f, 0.0f };
    float near_z                  = 0.1f;
    float far_z                   = 1000.0f;
    float fov_y_rad               = 1.04719755f; // ~60 deg
    bool  is_reverse_z            = false;

    [[nodiscard]] bool HasMovement() const noexcept {
        for (int i = 0; i < 16; ++i) {
            if (std::abs(view_proj[i] - prev_view_proj[i]) > 1e-5f) return true;
        }
        return false;
    }
};

}  // namespace omnirender::core
