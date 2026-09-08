// filepath: modules/daemon/history_manager.h
#pragma once

#include <d3d11.h>
#include <cstdint>
#include "../common/pipeline_config.h"

namespace omnirender::daemon {

// Explicit states of the temporal accumulation state machine.
enum class HistoryState : uint8_t {
    Empty = 0,
    WarmingUp,
    Valid,
    Invalidated,
    Rebuilding
};

// Reason for explicit temporal history invalidation.
enum class InvalidationReason : uint8_t {
    Generic = 0,
    ResolutionChanged,
    CameraDiscontinuity,
    SceneCut,
    DeviceReset,
    SwapchainRecreated,
    HDRChanged,
    FormatChanged,
    FrameDropped,
    AltTab
};

// Manages multi-slot temporal color accumulation and previous-depth lifecycle.
class HistoryManager {
public:
    HistoryManager() = default;
    ~HistoryManager() { Shutdown(); }

    HistoryManager(const HistoryManager&) = delete;
    HistoryManager& operator=(const HistoryManager&) = delete;

    HRESULT Initialize(ID3D11Device* device, UINT width, UINT height);
    void    Shutdown();
    void    Invalidate(InvalidationReason reason);

    // Read views for temporal and disocclusion passes
    ID3D11ShaderResourceView* GetCurrentHistorySrv() const noexcept;
    ID3D11Texture2D*          GetCurrentHistoryTexture() const noexcept;
    ID3D11ShaderResourceView* GetPreviousDepthSrv() const noexcept;

    bool HasValidHistory() const noexcept { return state_ == HistoryState::Valid || state_ == HistoryState::Rebuilding; }
    bool HasValidPreviousDepth() const noexcept { return has_previous_depth_; }
    uint32_t GetValidFrameCount() const noexcept { return valid_frames_; }
    HistoryState GetState() const noexcept { return state_; }
    const char* GetStateName() const noexcept;

    // End-of-frame commitment: copies current resolved color and depth into history
    void CommitFrame(ID3D11DeviceContext* ctx,
                     ID3D11Texture2D* resolved_color,
                     ID3D11Texture2D* current_depth);

private:
    static constexpr UINT kMaxSlots = omnirender::kDefaultMaxHistoryFrames;

    ID3D11Texture2D*          history_[kMaxSlots] = {};
    ID3D11ShaderResourceView* history_srv_[kMaxSlots] = {};
    ID3D11Texture2D*          previous_depth_ = nullptr;
    ID3D11ShaderResourceView* previous_depth_srv_ = nullptr;

    UINT         width_ = 0;
    UINT         height_ = 0;
    UINT         write_index_ = 0;
    UINT         valid_frames_ = 0;
    HistoryState state_ = HistoryState::Empty;
    bool         has_previous_depth_ = false;
};

}  // namespace omnirender::daemon
