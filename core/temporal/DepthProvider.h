// filepath: core/temporal/DepthProvider.h
// Depth linearization pass (audit plan Commit 6).
//
// Old games publish raw hardware depth (non-linear NDC depth, sometimes
// reversed-Z). Downstream consumers -- disocclusion detection, DLSS depth
// input -- expect depth that is linear in view space. This pass converts
// fc.depth (raw) into fc.depth_linear (linearized [0,1]).
//
// Dual-path:
//   GPU path (preferred): DepthLinearize.cso compute dispatch, compiled from
//     shaders/temporal/DepthLinearize.hlsl (cbuffer matches ReprojectionCB).
//   CPU fallback: staging readback + per-pixel linearization. Works
//     everywhere (including WARP) but costs a GPU sync per frame, so it is
//     a fallback, not the default for real games.
//
// fc.depth itself is intentionally NOT replaced: the motion-reprojection
// pass unprojects using raw NDC depth, so it must keep consuming fc.depth.
#pragma once

#include <cstdint>
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

class DepthProvider {
public:
    DepthProvider() = default;
    ~DepthProvider() { Shutdown(); }

    DepthProvider(const DepthProvider&) = delete;
    DepthProvider& operator=(const DepthProvider&) = delete;

    // Allocates the linear-depth output texture (+ optional CSO load).
    // Returns false only if the output texture cannot be created; without
    // it the pass reports Skipped every frame.
    bool Initialize(graphics::IGraphicsDevice& device,
                    uint32_t width, uint32_t height);
    void Shutdown();

    // Raw game depth (fc.depth) -> linearized [0,1] (fc.depth_linear).
    // Returns Skipped when depth is unavailable or mismatched.
    PassResult Process(FrameContext& fc, graphics::ICommandContext& cmd);

    [[nodiscard]] const GpuTexture& GetLinearDepth() const noexcept { return output_linear_; }
    [[nodiscard]] bool IsGpuPathActive() const noexcept { return gpu_shader_ != nullptr; }

private:
    PassResult ExecuteGpu(FrameContext& fc, graphics::ICommandContext& cmd);
    PassResult ExecuteCpu(FrameContext& fc, graphics::ICommandContext& cmd);

    GpuTexture                                  output_linear_;
    std::shared_ptr<graphics::IGraphicsBuffer>  cb_buffer_;
    void*                                       gpu_shader_ = nullptr;
    uint32_t                                    width_  = 0;
    uint32_t                                    height_ = 0;
    // W*H*4 readback scratch, reused as the upload staging buffer in the
    // CPU path (bounded: exactly one input-resolution R32F image).
    std::vector<uint8_t>                        raw_pixels_;
    graphics::IGraphicsDevice*                  device_ = nullptr;
};

}  // namespace omnirender::core::temporal
