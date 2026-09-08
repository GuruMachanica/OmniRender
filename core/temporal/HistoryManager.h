// filepath: core/temporal/HistoryManager.h
#pragma once

#include <cstdint>
#include <vector>
#include "../resources/GpuTexture.h"
#include "HistoryState.h"

namespace omnirender::graphics {
class IGraphicsDevice;
class ICommandContext;
}

namespace omnirender::core {

class HistoryManager {
public:
    static constexpr uint32_t kDefaultMaxHistorySlots = 4;

    HistoryManager() = default;
    ~HistoryManager() { Shutdown(); }

    HistoryManager(const HistoryManager&) = delete;
    HistoryManager& operator=(const HistoryManager&) = delete;

    bool Initialize(graphics::IGraphicsDevice& device,
                    uint32_t color_width,
                    uint32_t color_height,
                    TextureFormat color_format,
                    uint32_t depth_width,
                    uint32_t depth_height,
                    TextureFormat depth_format);

    bool Initialize(graphics::IGraphicsDevice& device,
                    uint32_t width,
                    uint32_t height,
                    TextureFormat color_format,
                    TextureFormat depth_format) {
        return Initialize(device, width, height, color_format, width, height, depth_format);
    }

    void Shutdown();
    void Invalidate(InvalidationReason reason);

    [[nodiscard]] GpuTexture GetCurrentHistoryTexture() const noexcept;
    [[nodiscard]] GpuTexture GetPreviousDepthTexture() const noexcept;

    [[nodiscard]] bool HasDepthHistory() const noexcept { return has_depth_history_; }
    [[nodiscard]] bool HasValidHistory() const noexcept {
        return state_ == HistoryState::Valid || state_ == HistoryState::Rebuilding;
    }
    [[nodiscard]] bool HasValidPreviousDepth() const noexcept { return has_previous_depth_; }
    [[nodiscard]] uint32_t GetValidFrameCount() const noexcept { return valid_frames_; }
    [[nodiscard]] HistoryState GetState() const noexcept { return state_; }

    void CommitFrame(graphics::ICommandContext& ctx, GpuTexture resolved_color, GpuTexture current_depth);

private:
    std::vector<GpuTexture> history_;
    GpuTexture              previous_depth_;

    uint32_t     max_slots_          = kDefaultMaxHistorySlots;
    uint32_t     write_index_        = 0;
    uint32_t     valid_frames_       = 0;
    uint32_t     color_width_        = 0;
    uint32_t     color_height_       = 0;
    uint32_t     depth_width_        = 0;
    uint32_t     depth_height_       = 0;
    HistoryState state_              = HistoryState::Empty;
    bool         has_previous_depth_ = false;
    bool         has_depth_history_  = false; // true only if depth alloc succeeded
};

}  // namespace omnirender::core
