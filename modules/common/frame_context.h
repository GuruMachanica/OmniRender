// filepath: modules/common/frame_context.h
#pragma once

#include <cstdint>
#include "ipc_protocol.h"

namespace omnirender {

// Provenance of per-frame visual and geometric information.
enum class DataSource : uint8_t {
    Unknown = 0,
    GameProvided,
    HookExtracted,
    Reconstructed,
    OpticalFlow,
    Estimated
};

// Generic GPU texture abstraction independent of any single graphics API.
struct GpuTextureView {
    void*      resource = nullptr;  // Native resource (e.g., ID3D11Texture2D*, ID3D12Resource*)
    void*      srv = nullptr;       // Read view handle (e.g., ID3D11ShaderResourceView*)
    void*      uav = nullptr;       // Read/write view handle (e.g., ID3D11UnorderedAccessView*)
    uint32_t   width = 0;
    uint32_t   height = 0;
    DataSource source = DataSource::Unknown;
    bool       is_valid = false;

    template <typename T>
    T* AsResource() const noexcept { return static_cast<T*>(resource); }

    template <typename T>
    T* AsSrv() const noexcept { return static_cast<T*>(srv); }

    template <typename T>
    T* AsUav() const noexcept { return static_cast<T*>(uav); }
};

// Camera matrices and depth range state.
struct CameraState {
    float      view_proj_current[16] = {};
    float      view_proj_previous[16] = {};
    float      inv_view_proj_current[16] = {};
    float      camera_near = 0.1f;
    float      camera_far = 1000.0f;
    bool       is_reversed_z = false;
    bool       is_valid = false;
    DataSource source = DataSource::Unknown;
};

// Temporal subpixel jitter coordinates.
struct JitterState {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float previous_offset_x = 0.0f;
    float previous_offset_y = 0.0f;
    bool  is_valid = false;
};

// Exposure and dynamic range metadata.
struct ExposureState {
    float exposure = 1.0f;
    float pre_exposure = 1.0f;
    bool  is_hdr = false;
    bool  is_valid = false;
};

// Resolution progression across capture, working, and target presentation.
struct ResolutionState {
    uint32_t input_width = 0;
    uint32_t input_height = 0;
    uint32_t work_width = 0;
    uint32_t work_height = 0;
    uint32_t output_width = 0;
    uint32_t output_height = 0;
};

// Explicit confidence and validity flags per pipeline component.
struct FrameValidity {
    bool color_valid = false;
    bool depth_valid = false;
    bool camera_valid = false;
    bool motion_valid = false;
    bool jitter_valid = false;
    bool exposure_valid = false;
    bool history_valid = false;
    bool disocclusion_valid = false;
    bool reactive_valid = false;
};

// Frame timing and sequence progression.
struct FrameTiming {
    uint64_t frame_index = 0;
    float    delta_time = 0.0f;
    uint64_t timestamp_qpc = 0;
};

// Canonical frame abstraction binding all resources, metadata, and validity.
struct FrameContext {
    uint64_t frame_id = 0;
    OmniRenderIPCFrameData ipc_payload{};

    // Primary GPU resources
    GpuTextureView color;
    GpuTextureView depth;
    GpuTextureView motion;
    GpuTextureView reactive;
    GpuTextureView disocclusion;

    // Temporal history references
    GpuTextureView previous_color;
    GpuTextureView previous_depth;

    // Camera, temporal, exposure, and viewport state
    CameraState     camera;
    JitterState     jitter;
    ExposureState   exposure;
    ResolutionState resolution;
    FrameValidity   validity;
    FrameTiming     timing;

    bool CanPerformTemporal() const noexcept {
        return validity.color_valid && validity.history_valid && validity.motion_valid;
    }

    bool CanPerformSpatial() const noexcept {
        return validity.color_valid;
    }
};

}  // namespace omnirender
