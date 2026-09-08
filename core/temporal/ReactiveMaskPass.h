// filepath: core/temporal/ReactiveMaskPass.h
// Heuristic reactive mask generation pass (luminance-delta based).
// Dual-path: GPU compute (preferred) / CPU fallback.
#pragma once

#include <memory>
#include <vector>
#include "TemporalPassBase.h"
#include "../graph/RenderGraphTypes.h"
#include "../resources/GpuTexture.h"

namespace omnirender::graphics {
class IGraphicsDevice;
class ICommandContext;
class IGraphicsBuffer;
}

namespace omnirender::core::temporal {

class ReactiveMaskPass {
public:
    ReactiveMaskPass() = default;
    ~ReactiveMaskPass() { Shutdown(); }

    ReactiveMaskPass(const ReactiveMaskPass&) = delete;
    ReactiveMaskPass& operator=(const ReactiveMaskPass&) = delete;

    bool Initialize(graphics::IGraphicsDevice& device,
                    uint32_t width, uint32_t height);
    void Shutdown();

    // Requires fc.color and a valid history texture for luminance comparison.
    // Returns Skipped if history is unavailable (first frame).
    PassResult Execute(FrameContext& fc, graphics::ICommandContext& cmd,
                       const GpuTexture& history_color);

    [[nodiscard]] bool IsGpuPathActive() const noexcept { return gpu_shader_ != nullptr; }

    // Pixels whose luminance delta exceeds this are marked reactive.
    float luminance_threshold = 0.15f;

private:
    PassResult ExecuteGpu(FrameContext& fc, graphics::ICommandContext& cmd,
                          const GpuTexture& history_color);
    PassResult ExecuteCpu(FrameContext& fc, graphics::ICommandContext& cmd);

    GpuTexture                                  output_reactive_;
    std::shared_ptr<graphics::IGraphicsBuffer>  cb_buffer_;
    void*                                       gpu_shader_ = nullptr;
    uint32_t width_  = 0;
    uint32_t height_ = 0;
    std::vector<uint8_t>                        cpu_pixels_; // R8 scratch
    graphics::IGraphicsDevice*                  device_ = nullptr;
};

}  // namespace omnirender::core::temporal
