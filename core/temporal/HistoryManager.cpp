// filepath: core/temporal/HistoryManager.cpp
#include "HistoryManager.h"
#include "../../graphics/abstraction/IGraphicsDevice.h"
#include "../../graphics/abstraction/ICommandContext.h"
#include "../../graphics/abstraction/IGraphicsTexture.h"

namespace omnirender::core {

bool HistoryManager::Initialize(graphics::IGraphicsDevice& device,
                                uint32_t color_width,
                                uint32_t color_height,
                                TextureFormat color_format,
                                uint32_t depth_width,
                                uint32_t depth_height,
                                TextureFormat depth_format) {
    if (color_width == 0 || color_height == 0 || depth_width == 0 || depth_height == 0) return false;
    Shutdown();

    color_width_ = color_width;
    color_height_ = color_height;
    depth_width_ = depth_width;
    depth_height_ = depth_height;
    history_.resize(max_slots_);

    TextureDesc color_desc{
        color_width, color_height, 1, color_format,
        TextureUsage::ShaderResource | TextureUsage::RenderTarget | TextureUsage::TransferDst,
        "HistoryColorSlot"
    };

    for (uint32_t i = 0; i < max_slots_; ++i) {
        auto tex = device.CreateTexture(color_desc);
        if (!tex) {
            Shutdown();
            return false;
        }
        history_[i] = GpuTexture(std::move(tex));
    }

    TextureDesc depth_desc{
        depth_width, depth_height, 1, depth_format,
        TextureUsage::ShaderResource | TextureUsage::TransferDst,
        "HistoryPreviousDepth"
    };
    auto depth_tex = device.CreateTexture(depth_desc);
    if (depth_tex) {
        previous_depth_ = GpuTexture(std::move(depth_tex));
        has_depth_history_ = true;
    } else {
        // Depth history is an optional capability downgrade, not a hard failure.
        // Motion reprojection and disocclusion passes will query HasDepthHistory()
        // and skip their depth-dependent work when this is false.
        has_depth_history_ = false;
    }

    write_index_ = 0;
    valid_frames_ = 0;
    state_ = HistoryState::Empty;
    has_previous_depth_ = false;
    return true;
}

void HistoryManager::Shutdown() {
    history_.clear();
    previous_depth_.Reset();

    color_width_ = 0;
    color_height_ = 0;
    depth_width_ = 0;
    depth_height_ = 0;
    write_index_ = 0;
    valid_frames_ = 0;
    state_ = HistoryState::Empty;
    has_previous_depth_ = false;
    has_depth_history_  = false;
}

void HistoryManager::Invalidate(InvalidationReason) {
    state_ = (valid_frames_ > 0) ? HistoryState::Invalidated : HistoryState::Empty;
    valid_frames_ = 0;
    has_previous_depth_ = false;
}

GpuTexture HistoryManager::GetCurrentHistoryTexture() const noexcept {
    if (valid_frames_ == 0 || history_.empty()) return GpuTexture{};
    uint32_t read_idx = (write_index_ + max_slots_ - 1) % max_slots_;
    return history_[read_idx];
}

GpuTexture HistoryManager::GetPreviousDepthTexture() const noexcept {
    return has_previous_depth_ ? previous_depth_ : GpuTexture{};
}

void HistoryManager::CommitFrame(graphics::ICommandContext& ctx,
                                 GpuTexture resolved_color,
                                 GpuTexture current_depth) {
    if (resolved_color.IsValid() && !history_.empty() && history_[write_index_].IsValid()) {
        if (resolved_color.GetWidth() == color_width_ && resolved_color.GetHeight() == color_height_) {
            ctx.CopyTexture(history_[write_index_].Get(), resolved_color.Get());
            write_index_ = (write_index_ + 1) % max_slots_;
            if (valid_frames_ < max_slots_) {
                valid_frames_++;
            }
            if (valid_frames_ >= 2) {
                state_ = HistoryState::Valid;
            } else if (state_ == HistoryState::Invalidated) {
                state_ = HistoryState::Rebuilding;
            } else {
                state_ = HistoryState::WarmingUp;
            }
        }
    }

    if (current_depth.IsValid() && previous_depth_.IsValid()) {
        if (current_depth.GetWidth() == depth_width_ && current_depth.GetHeight() == depth_height_) {
            ctx.CopyTexture(previous_depth_.Get(), current_depth.Get());
            has_previous_depth_ = true;
        }
    }
}

}  // namespace omnirender::core
