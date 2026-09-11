// filepath: core/temporal/DisocclusionPass.h
// Depth-based disocclusion mask generation pass.
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

class DisocclusionPass {
public:
    DisocclusionPass() = default;
    ~DisocclusionPass() { Shutdown(); }

    DisocclusionPass(const DisocclusionPass&) = delete;
    DisocclusionPass& operator=(const DisocclusionPass&) = delete;

    bool Initialize(graphics::IGraphicsDevice& device,
                    uint32_t width, uint32_t height);
    void Shutdown();

    // Requires current depth and previous depth history. `curr_depth` is
    // normally the DepthProvider's linearized output; passing raw depth is
    // allowed but the caller must then supply a matching-resolution history.
    // Returns Skipped if prerequisite resources are unavailable.
    PassResult Execute(FrameContext& fc, graphics::ICommandContext& cmd,
                       const GpuTexture& prev_depth,
                       const GpuTexture& curr_depth);

    [[nodiscard]] bool IsGpuPathActive() const noexcept { return gpu_shader_ != nullptr; }

    // Threshold (in linearised depth units) beyond which a pixel is disoccluded.
    float depth_threshold = 0.02f;

private:
    PassResult ExecuteGpu(FrameContext& fc, graphics::ICommandContext& cmd,
                          const GpuTexture& prev_depth, const GpuTexture& curr_depth);
    PassResult ExecuteCpu(FrameContext& fc, graphics::ICommandContext& cmd,
                          const GpuTexture& prev_depth, const GpuTexture& curr_depth);

    GpuTexture                                  output_disocc_;
    std::shared_ptr<graphics::IGraphicsBuffer>  cb_buffer_;
    void*                                       gpu_shader_ = nullptr;
    uint32_t width_  = 0;
    uint32_t height_ = 0;
    std::vector<uint8_t>                        cpu_pixels_; // R8 scratch
    graphics::IGraphicsDevice*                  device_ = nullptr;
};

}  // namespace omnirender::core::temporal
