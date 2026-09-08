// filepath: modules/common/pipeline_config.h
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace omnirender {

// v0.3.0-alpha processing defaults.
// These are deliberately conservative so the daemon keeps VRAM bounded
// even on large source frames.
inline constexpr UINT kDefaultMaxProcessingWidth  = 1920;
inline constexpr UINT kDefaultMaxProcessingHeight = 1200;
inline constexpr UINT kDefaultMaxHistoryFrames    = 3;
inline constexpr UINT kDefaultProcessingVRamMB   = 180;
inline constexpr UINT kDefaultMaxOutputWidth      = 2560;
inline constexpr UINT kDefaultMaxOutputHeight     = 1600;

// Reconstruction defaults.
inline constexpr float kDefaultJitterX = 0.0f;
inline constexpr float kDefaultJitterY = 0.0f;

// Runtime config knobs. In a stock build these can be overridden through
// environment variables at startup so the pipeline is testable without
// recompiling.
namespace config {

// Set via OMNIRENDER_MAX_WORK_WIDTH / _HEIGHT.
inline UINT g_max_work_width  = kDefaultMaxProcessingWidth;
inline UINT g_max_work_height = kDefaultMaxProcessingHeight;

// Set via OMNIRENDER_MAX_HISTORY.
inline UINT g_max_history = kDefaultMaxHistoryFrames;

// Set via OMNIRENDER_ENABLE_RECONSTRUCTION / _UPSCALE / _TONEMAP.
inline bool g_enable_reconstruction = true;
inline bool g_enable_upscale        = true;
inline bool g_enable_tonemap        = true;

// Wider AI/ML integration flags.
// OMNIRENDER_ENABLE_OPTICAL_FLOW, OMNIRENDER_ENABLE_DLSS, OMNIRENDER_ENABLE_TRT_TONEMAP,
// OMNIRENDER_ENABLE_XESS, OMNIRENDER_ENABLE_FSR, OMNIRENDER_ENABLE_RT_EFFECTS.
inline bool g_enable_optical_flow   = false;
inline bool g_enable_dlss           = false;
inline bool g_enable_trt_tonemap    = false;
inline bool g_enable_xess           = false;
inline bool g_enable_fsr            = false;
inline bool g_enable_rt_effects     = false;

inline bool ParseBoolEnv(const char* name, bool def) {
    if (!name) return def;
    if (const char* v = std::getenv(name)) {
        if (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F') return false;
        if (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T') return true;
    }
    return def;
}

inline UINT ParseUintEnv(const char* name, UINT def) {
    if (!name) return def;
    if (const char* v = std::getenv(name)) {
        if (UINT val = 0; std::sscanf(v, "%u", &val) == 1) return val;
    }
    return def;
}

} // namespace config

} // namespace omnirender
