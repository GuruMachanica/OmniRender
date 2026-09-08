// filepath: modules/common/backend_capabilities.h
#pragma once

#include <cstdint>
#include "frame_context.h"

namespace omnirender {

enum class BackendType : uint8_t {
    None = 0,
    SpatialFallback,
    FSR,
    OmniTemporal,
    XeSS,
    DLSS
};

enum class UserPreference : uint8_t {
    Quality = 0,
    Balanced,
    Performance,
    Compatibility,
    Latency
};

enum class FallbackReason : uint8_t {
    None = 0,
    MissingColor,
    MissingDepth,
    MissingMotion,
    MissingCamera,
    MissingHistory,
    UnsupportedHDR,
    BackendUnavailable,
    DisabledByConfig
};

enum class VendorId : uint8_t { Unknown = 0, Nvidia, Intel, Amd, Generic };
enum class TechnologyId : uint8_t { None = 0, DLSS, XeSS, FSR, OmniTemporal, Spatial };
enum class FeatureId : uint8_t { None = 0, SuperResolution, FrameGeneration, RayReconstruction, LowLatency };

struct BackendId {
    VendorId     vendor = VendorId::Generic;
    TechnologyId technology = TechnologyId::None;
    FeatureId    feature = FeatureId::None;
    uint16_t     version_major = 1;
    uint16_t     version_minor = 0;
};

// Theoretical family capability declaration.
struct BackendCapabilities {
    bool super_resolution = false;
    bool frame_generation = false;
    bool multi_frame_generation = false;
    bool ray_reconstruction = false;
    bool ray_tracing = false;
    bool denoising = false;
    bool low_latency = false;
    bool requires_motion = false;
    bool requires_depth = false;
    bool requires_exposure = false;
    bool requires_jitter = false;
    bool supports_d3d11 = true;
    bool supports_d3d12 = false;
    bool supports_hdr = false;
    bool supports_vrr = false;
};

// Live runtime execution capability on currently active device/driver/DLL.
struct RuntimeCapabilities {
    bool is_sdk_loaded    = false;
    bool is_gpu_supported = false;
    bool supports_sr      = false;
    bool supports_fg      = false;
    bool supports_rr      = false;
    bool supports_d3d11   = true;
    bool supports_d3d12   = false;
};

struct FrameCapabilities {
    bool has_color = false, has_depth = false, has_motion = false, has_camera = false;
    bool has_jitter = false, has_exposure = false, has_history = false, is_hdr = false;

    static FrameCapabilities FromFrameContext(const FrameContext& ctx) noexcept {
        FrameCapabilities caps;
        caps.has_color    = ctx.validity.color_valid;
        caps.has_depth    = ctx.validity.depth_valid;
        caps.has_motion   = ctx.validity.motion_valid;
        caps.has_camera   = ctx.validity.camera_valid;
        caps.has_jitter   = ctx.validity.jitter_valid;
        caps.has_exposure = ctx.validity.exposure_valid;
        caps.has_history  = ctx.validity.history_valid;
        caps.is_hdr       = ctx.exposure.is_hdr;
        return caps;
    }
};

struct BackendScore {
    float quality = 0.0f;       // 0.0 to 1.0 reconstruction fidelity
    float performance = 0.0f;   // 0.0 to 1.0 relative execution throughput
    float compatibility = 0.0f; // 0.0 to 1.0 hardware / API breadth
    float latency = 0.0f;       // 0.0 to 1.0 pipeline delay rating (1.0 = lowest lag)
    float confidence = 0.0f;    // 0.0 to 1.0 input data trust
};

struct BackendScoreWeights {
    float quality_weight = 0.40f;
    float performance_weight = 0.25f;
    float compatibility_weight = 0.15f;
    float latency_weight = 0.10f;
    float confidence_weight = 0.10f;
};

inline constexpr BackendScoreWeights GetWeightsForPreference(UserPreference pref) noexcept {
    switch (pref) {
        case UserPreference::Quality:       return { 0.50f, 0.10f, 0.15f, 0.05f, 0.20f };
        case UserPreference::Performance:   return { 0.05f, 0.55f, 0.15f, 0.20f, 0.05f };
        case UserPreference::Latency:       return { 0.05f, 0.25f, 0.15f, 0.50f, 0.05f };
        case UserPreference::Compatibility: return { 0.15f, 0.05f, 0.50f, 0.05f, 0.25f };
        case UserPreference::Balanced:
        default:                            return { 0.30f, 0.30f, 0.10f, 0.10f, 0.20f };
    }
}

inline constexpr float ComputeTotalScore(const BackendScore& s, const BackendScoreWeights& w) noexcept {
    return (s.quality * w.quality_weight) +
           (s.performance * w.performance_weight) +
           (s.compatibility * w.compatibility_weight) +
           (s.latency * w.latency_weight) +
           (s.confidence * w.confidence_weight);
}

struct CompatibilityResult {
    bool           compatible = false;
    BackendScore   score{};
    float          quality_score = 0.0f;
    float          performance_score = 0.0f;
    float          confidence_score = 0.0f;
    FallbackReason reason = FallbackReason::None;
    const char*    detail = "OK";
};

inline BackendCapabilities QueryBackendCapabilities(BackendType type) noexcept {
    BackendCapabilities caps;
    switch (type) {
        case BackendType::DLSS:
            caps.super_resolution = caps.frame_generation = caps.ray_reconstruction = caps.low_latency = true;
            caps.requires_motion = caps.requires_depth = caps.supports_d3d12 = caps.supports_hdr = true;
            break;
        case BackendType::XeSS:
            caps.super_resolution = caps.frame_generation = caps.low_latency = true;
            caps.requires_motion = caps.requires_depth = caps.supports_hdr = true;
            break;
        case BackendType::OmniTemporal:
            caps.super_resolution = caps.requires_motion = caps.requires_depth = caps.supports_d3d11 = true;
            break;
        case BackendType::FSR:
            caps.super_resolution = caps.supports_d3d11 = true;
            break;
        case BackendType::SpatialFallback:
            caps.supports_d3d11 = true;
            break;
        default:
            break;
    }
    return caps;
}

inline const char* GetBackendName(BackendType type) noexcept {
    switch (type) {
        case BackendType::DLSS:            return "NVIDIA DLSS";
        case BackendType::XeSS:            return "Intel XeSS";
        case BackendType::OmniTemporal:    return "OmniTemporal Native";
        case BackendType::FSR:             return "AMD FSR 1.0 (EASU + RCAS)";
        case BackendType::SpatialFallback: return "Spatial Bilinear Fallback";
        default:                           return "Passthrough";
    }
}

inline CompatibilityResult EvaluateBackend(BackendType backend,
                                           const FrameCapabilities& frame_caps,
                                           bool is_backend_available = true) noexcept {
    CompatibilityResult res;
    if (!is_backend_available) {
        res.reason = FallbackReason::BackendUnavailable;
        res.detail = "Backend runtime library not available";
        return res;
    }
    if (!frame_caps.has_color) {
        res.reason = FallbackReason::MissingColor;
        res.detail = "Color buffer is invalid or missing";
        return res;
    }

    auto apply_score = [&](float q, float p, float comp, float lat, float conf, const char* msg) {
        res.compatible = true;
        res.score = { q, p, comp, lat, conf };
        res.quality_score = q;
        res.performance_score = p;
        res.confidence_score = conf;
        res.detail = msg;
    };

    switch (backend) {
        case BackendType::DLSS:
            if (!frame_caps.has_motion) { res.reason = FallbackReason::MissingMotion; res.detail = "Motion vectors unavailable"; return res; }
            if (!frame_caps.has_depth)  { res.reason = FallbackReason::MissingDepth; res.detail = "Linearized depth unavailable"; return res; }
            apply_score(0.95f, 0.80f, 0.85f, 0.85f, 0.92f, "Compatible (DLSS Tensor Reconstruct)");
            return res;

        case BackendType::XeSS:
            if (!frame_caps.has_motion) { res.reason = FallbackReason::MissingMotion; res.detail = "Motion vectors unavailable"; return res; }
            if (!frame_caps.has_depth)  { res.reason = FallbackReason::MissingDepth; res.detail = "Linearized depth unavailable"; return res; }
            apply_score(0.88f, 0.75f, 0.90f, 0.80f, 0.95f, "Compatible (Intel XeSS Neural Reconstruct)");
            return res;

        case BackendType::OmniTemporal:
            if (!frame_caps.has_motion) { res.reason = FallbackReason::MissingMotion; res.detail = "Motion vectors unavailable"; return res; }
            if (!frame_caps.has_depth)  { res.reason = FallbackReason::MissingDepth; res.detail = "Linearized depth unavailable"; return res; }
            if (!frame_caps.has_history) { res.reason = FallbackReason::MissingHistory; res.detail = "History buffer warming up"; return res; }
            apply_score(0.82f, 0.85f, 0.95f, 0.88f, 0.90f, "Compatible (OmniRender 3x3 YCoCg Temporal)");
            return res;

        case BackendType::FSR:
            apply_score(0.70f, 0.96f, 1.00f, 0.95f, 0.98f, "Compatible (FSR EASU Spatial Edge + RCAS Sharpen)");
            return res;

        case BackendType::SpatialFallback:
            apply_score(0.40f, 0.99f, 1.00f, 0.98f, 1.00f, "Compatible (Spatial Bilinear Filter)");
            return res;

        default:
            res.reason = FallbackReason::DisabledByConfig;
            res.detail = "No upscaling requested";
            return res;
    }
}

inline CompatibilityResult EvaluateBackend(BackendType backend,
                                           const FrameContext& ctx,
                                           bool is_backend_available = true) noexcept {
    return EvaluateBackend(backend, FrameCapabilities::FromFrameContext(ctx), is_backend_available);
}

inline bool IsBackendCompatible(BackendType backend, const FrameCapabilities& frame_caps) noexcept {
    return EvaluateBackend(backend, frame_caps, true).compatible;
}

// Multi-variable weighted scoring selection engine across user preferences.
inline BackendType SelectOptimalBackend(const FrameCapabilities& frame_caps,
                                        bool dlss_available,
                                        bool xess_available,
                                        bool fsr_available,
                                        bool temporal_available,
                                        UserPreference pref = UserPreference::Quality) noexcept {
    const BackendScoreWeights weights = GetWeightsForPreference(pref);
    BackendType best_type = BackendType::SpatialFallback;
    float best_score = -1.0f;

    const struct { BackendType type; bool available; } candidates[] = {
        { BackendType::DLSS, dlss_available },
        { BackendType::XeSS, xess_available },
        { BackendType::OmniTemporal, temporal_available },
        { BackendType::FSR, fsr_available },
        { BackendType::SpatialFallback, true }
    };

    for (const auto& c : candidates) {
        if (!c.available) continue;
        CompatibilityResult res = EvaluateBackend(c.type, frame_caps, true);
        if (!res.compatible) continue;
        float score = ComputeTotalScore(res.score, weights);
        if (score > best_score) {
            best_score = score;
            best_type = c.type;
        }
    }
    return best_type;
}

}  // namespace omnirender
