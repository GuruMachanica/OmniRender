// filepath: modules/common/ipc_protocol.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <cstdint>

namespace omnirender {

// Shared named objects. Both live in the Global namespace so the daemon
// (running as a service-style process) and the hook (running inside the
// legacy game) can open them with the same name.
inline constexpr const char* kIPCBlockName             = "Global\\OmniRender_IPC_Block";
inline constexpr const char* kIPCBlockNameLocal        = "Local\\OmniRender_IPC_Block";
inline constexpr const char* kFrameReadyEventName      = "Global\\OmniRender_FrameReady";
inline constexpr const char* kFrameReadyEventNameLocal = "Local\\OmniRender_FrameReady";

// Per-frame metadata block exchanged across the IPC ring buffer. The
// layout is duplicated from PRD section 5.1 to keep it byte-for-byte
// compatible between hook and daemon.
//
// v0.4.0-alpha extends the struct with:
//   - view_proj_current[] / view_proj_previous[] for depth+VP reprojection
//   - jitter_x / jitter_y (Halton 2,3 sub-pixel offset)
//   - motion_format (DXGI_FORMAT of the motion vector texture)
//   - struct_version (1 = v0.4.0, 0 = v0.3.0 layout for back-compat)
//
// The daemon checks struct_version on every frame and gracefully falls
// back to passthrough if a v0.3.0 hook payload arrives.
#pragma pack(push, 8)
struct OmniRenderIPCFrameData {
    uint32_t magic_header;         // 0x4F4D4E49 ("OMNI")
    uint32_t struct_version;       // 1 for v0.4.0-alpha
    uint64_t frame_index;          // Monotonically increasing counter
    uint32_t surface_width;
    uint32_t surface_height;
    uint32_t target_width;
    uint32_t target_height;
    uint32_t color_format;
    uint32_t depth_format;
    uint64_t shared_color_handle;  // Fixed-width cross-process 64-bit handle
    uint64_t shared_depth_handle;  // Fixed-width cross-process 64-bit handle
    uint64_t shared_motion_handle; // Fixed-width cross-process 64-bit handle
    float    camera_near;
    float    camera_far;
    float    fov_vertical_rad;
    float    jitter_x;              // NEW v0.4.0: Halton 2 sequence
    float    jitter_y;              // NEW v0.4.0: Halton 3 sequence
    float    view_proj_current[16]; // NEW v0.4.0: view*proj of this frame
    float    view_proj_previous[16];// NEW v0.4.0: view*proj of last frame
    uint32_t motion_format;         // NEW v0.4.0: DXGI_FORMAT_R16G16_FLOAT
    uint32_t flags;                 // Bit 0: Reversed Z, Bit 1: Depth Inverted
};
#pragma pack(pop)

inline constexpr uint32_t kIpcMagic = 0x4F4D4E49; // "OMNI"

// IPC struct version. 0 = v0.3.0-alpha (no motion, no VP, no jitter).
// 1 = v0.4.0-alpha (full reprojection input). Bump on every layout change.
inline constexpr uint32_t kIpcVersion_V040 = 1;

enum class IpcFlag : uint32_t {
    None        = 0,
    ReversedZ   = 1u << 0,  // depth buffer uses reversed-Z convention
    DepthRaw    = 1u << 1,  // depth was not acquired / is unpopulated
    CameraZero  = 1u << 2,  // view_proj_current/previous are all-zero (not extracted)
};

// v0.3.0-alpha pipeline control bits travel in `flags` as well so the
// IPC struct stays a single fixed-size block. They describe what the
// daemon is permitted to run for this frame.
enum class PipelineFlag : uint32_t {
    None                = 0,
    EnableReconstruction = 1u << 2, // temporal history + resolve pass
    EnableUpscale        = 1u << 3, // upscale toward target_width/target_height
    EnableTonemap        = 1u << 4, // HDR / inverse tone pass if shader loaded
};

// Helper so callers can pack pipeline flags into the IPC `flags` field
// without manual bit math.
inline uint32_t MakePipelineFlags(PipelineFlag a, PipelineFlag b = PipelineFlag::None,
                                 PipelineFlag c = PipelineFlag::None) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b) | static_cast<uint32_t>(c);
}

// Broader AI/ML/RT enable bits for future backends. These are kept in the
// same IPC `flags` field for now so the hook/daemon can agree on feature set.
enum class AiMlFlag : uint32_t {
    None          = 0,
    EnableOpticalFlow = 1u << 5,
    EnableDLSS      = 1u << 6,
    EnableTensorRTTonemap = 1u << 7,
    EnableXeSS      = 1u << 8,
    EnableFSR       = 1u << 9,
    EnableRT       = 1u << 10,
};

} // namespace omnirender