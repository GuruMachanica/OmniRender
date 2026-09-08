// filepath: core/graph/RenderGraphTypes.h
#pragma once

#include <cstdint>
#include <string_view>

namespace omnirender::core {

enum class PassType : uint8_t {
    Capture = 0,
    MotionReproject,
    ReactiveMask,
    Disocclusion,
    TemporalResolve,
    Upscale,
    RayTracing,
    Tonemap,
    Present,
    Custom
};

enum class PassResult : uint8_t {
    Success = 0,
    Skipped,
    Failed
};

enum class PassPolicy : uint8_t {
    Required = 0,
    Optional
};

enum class ResourceAccess : uint16_t {
    None          = 0,
    ReadColor     = 1 << 0,
    ReadDepth     = 1 << 1,
    ReadMotion    = 1 << 2,
    ReadReactive  = 1 << 3,
    ReadHistory   = 1 << 4,
    WriteColor    = 1 << 5,
    WriteDepth    = 1 << 6,
    WriteMotion   = 1 << 7,
    WriteReactive = 1 << 8,
    WriteDisocc   = 1 << 9,
    PresentOutput = 1 << 10
};

[[nodiscard]] constexpr ResourceAccess operator|(ResourceAccess a, ResourceAccess b) noexcept {
    return static_cast<ResourceAccess>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

[[nodiscard]] constexpr bool HasAccess(ResourceAccess mask, ResourceAccess flag) noexcept {
    return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(flag)) != 0;
}

[[nodiscard]] constexpr std::string_view PassTypeToString(PassType type) noexcept {
    switch (type) {
        case PassType::Capture:         return "Capture";
        case PassType::MotionReproject: return "MotionReproject";
        case PassType::ReactiveMask:    return "ReactiveMask";
        case PassType::Disocclusion:    return "Disocclusion";
        case PassType::TemporalResolve: return "TemporalResolve";
        case PassType::Upscale:         return "Upscale";
        case PassType::RayTracing:      return "RayTracing";
        case PassType::Tonemap:         return "Tonemap";
        case PassType::Present:         return "Present";
        default:                        return "Custom";
    }
}

}  // namespace omnirender::core
