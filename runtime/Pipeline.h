// filepath: runtime/Pipeline.h
#pragma once

#include <memory>
#include "../core/graph/RenderGraph.h"
#include "../core/temporal/HistoryManager.h"
#include "../core/temporal/DepthProvider.h"
#include "../core/temporal/MotionReprojectionPass.h"
#include "../core/temporal/DisocclusionPass.h"
#include "../core/temporal/ReactiveMaskPass.h"
#include "../core/frame/FrameContext.h"
#include "../backends/reconstruction/IReconstructionBackend.h"

namespace omnirender::graphics {
class IGraphicsDevice;
class ICommandContext;
}

namespace omnirender::runtime {

struct PipelineConfig {
    bool enable_depth_linearize = true;
    bool enable_motion_vectors = true;
    bool enable_reactive_mask  = true;
    bool enable_disocclusion   = true;
    bool enable_temporal_accum = true;
    bool enable_ray_tracing    = false;
    bool enable_tonemap        = true;
};

class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline() = default;

    bool Initialize(graphics::IGraphicsDevice& device,
                    const core::Resolution& input_res,
                    const core::Resolution& output_res,
                    core::TextureFormat color_fmt,
                    core::TextureFormat depth_fmt);

    void SetReconstructionBackend(std::shared_ptr<backends::IReconstructionBackend> backend);
    void Configure(const PipelineConfig& config);

    bool ExecuteFrame(core::FrameContext& frame_ctx, graphics::ICommandContext& cmd_ctx);

    [[nodiscard]] core::HistoryManager& GetHistoryManager() noexcept { return history_mgr_; }

    // F12 frame-debugger views. Each returns the pass's last written texture
    // (invalid GpuTexture when the pass has not run or is disabled). Returned
    // by value — HistoryManager::GetCurrentHistoryTexture() is itself a
    // by-value API, and GpuTexture is a cheap shared_ptr wrapper.
    [[nodiscard]] core::GpuTexture GetDebugDepthLinear() const noexcept { return depth_provider_.GetLinearDepth(); }
    [[nodiscard]] core::GpuTexture GetDebugMotion() const noexcept { return motion_pass_.GetOutput(); }
    [[nodiscard]] core::GpuTexture GetDebugReactive() const noexcept { return reactive_pass_.GetOutput(); }
    [[nodiscard]] core::GpuTexture GetDebugDisocclusion() const noexcept { return disocclusion_pass_.GetOutput(); }
    [[nodiscard]] core::GpuTexture GetDebugHistory() const noexcept { return history_mgr_.GetCurrentHistoryTexture(); }

    void OnDeviceLost();
    bool OnDeviceRestored(graphics::IGraphicsDevice& device);

private:
    void BuildGraph();

    core::RenderGraph                                  graph_;
    core::HistoryManager                               history_mgr_;
    core::temporal::DepthProvider                      depth_provider_;
    core::temporal::MotionReprojectionPass             motion_pass_;
    core::temporal::DisocclusionPass                   disocclusion_pass_;
    core::temporal::ReactiveMaskPass                   reactive_pass_;
    std::shared_ptr<backends::IReconstructionBackend>  reconstruction_backend_;
    PipelineConfig                                     config_;
    core::Resolution                                   input_res_;
    core::Resolution                                   output_res_;
    core::TextureFormat                                color_fmt_{};
    core::TextureFormat                                depth_fmt_{};
    graphics::IGraphicsDevice*                         device_ = nullptr;
    bool                                               initialized_ = false;
};

}  // namespace omnirender::runtime
