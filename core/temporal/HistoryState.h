// filepath: core/temporal/HistoryState.h
#pragma once

#include <cstdint>
#include <string_view>

namespace omnirender::core {

enum class HistoryState : uint8_t {
    Empty = 0,
    WarmingUp,
    Valid,
    Invalidated,
    Rebuilding
};

enum class InvalidationReason : uint8_t {
    Generic = 0,
    ResolutionChanged,
    CameraDiscontinuity,
    CameraCut = CameraDiscontinuity,
    SceneCut,
    DeviceReset,
    SwapchainRecreated,
    HDRChanged,
    FormatChanged,
    FrameDropped,
    AltTab
};

[[nodiscard]] constexpr std::string_view HistoryStateToString(HistoryState s) noexcept {
    switch (s) {
        case HistoryState::Empty:       return "Empty";
        case HistoryState::WarmingUp:   return "WarmingUp";
        case HistoryState::Valid:       return "Valid";
        case HistoryState::Invalidated: return "Invalidated";
        case HistoryState::Rebuilding:  return "Rebuilding";
        default:                        return "Unknown";
    }
}

}  // namespace omnirender::core
