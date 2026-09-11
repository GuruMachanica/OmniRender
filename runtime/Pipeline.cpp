// filepath: runtime/Pipeline.cpp
#include "Pipeline.h"
#include "../graphics/abstraction/ICommandContext.h"
#include "../graphics/abstraction/IGraphicsDevice.h"

namespace omnirender::runtime {

namespace {

class FrameScope {
public:
    explicit FrameScope(graphics::ICommandContext& ctx) : ctx_(ctx) {}
    ~FrameScope() {
        if (!closed_) {
            ctx_.EndFrame();
        }
    }

    bool Close() {
        if (!closed_) {
            closed_ = true;
            return ctx_.EndFrame();
        }
        return true;
    }

private:
    graphics::ICommandContext& ctx_;
    bool closed_ = false;
};

}  // namespace

bool Pipeline::Initialize(graphics::IGraphicsDevice& device,
                          const core::Resolution& input_res,
                          const core::Resolution& output_res,
                          core::TextureFormat color_fmt,
                          core::TextureFormat depth_fmt) {
    device_ = &device;
    input_res_ = input_res;
    output_res_ = output_res;
    color_fmt_ = color_fmt;
    depth_fmt_ = depth_fmt;

    if (!history_mgr_.Initialize(device,
                                 output_res.width, output_res.height, color_fmt,
                                 input_res.width, input_res.height, depth_fmt)) {
        return false;
    }

    // Initialize temporal passes -- non-fatal if GPU resource allocation fails
    // (passes will return Skipped gracefully).
    depth_provider_.Initialize(device, input_res.width, input_res.height);
    motion_pass_.Initialize(device, input_res.width, input_res.height);
    disocclusion_pass_.Initialize(device, input_res.width, input_res.height);
    reactive_pass_.Initialize(device, input_res.width, input_res.height);

    if (reconstruction_backend_) {
        if (!reconstruction_backend_->Initialize(device, input_res, output_res)) {
            // Backend init failed — roll back history allocation and refuse to
            // mark the pipeline as initialized.  Callers must treat this as a
            // hard error; a broken backend must not silently become a no-op.
            history_mgr_.Shutdown();
            return false;
        }
    }

    BuildGraph();
    initialized_ = true;
    return true;
}

void Pipeline::SetReconstructionBackend(std::shared_ptr<backends::IReconstructionBackend> backend) {
    reconstruction_backend_ = std::move(backend);
    if (initialized_ && device_ && reconstruction_backend_) {
        if (!reconstruction_backend_->Initialize(*device_, input_res_, output_res_)) {
            // Post-init backend failure: null it out so BuildGraph() produces a
            // safe no-upscale graph rather than an invalid upscale pass.
            reconstruction_backend_ = nullptr;
        }
    }
    if (initialized_) {
        BuildGraph();
    }
}

void Pipeline::Configure(const PipelineConfig& config) {
    config_ = config;
    if (initialized_) {
        BuildGraph();
    }
}

void Pipeline::OnDeviceLost() {
    if (reconstruction_backend_) {
        reconstruction_backend_->OnDeviceLost();
    }
    history_mgr_.Shutdown();
    device_ = nullptr;
    initialized_ = false;
}

bool Pipeline::OnDeviceRestored(graphics::IGraphicsDevice& device) {
    return Initialize(device, input_res_, output_res_, color_fmt_, depth_fmt_);
}

void Pipeline::BuildGraph() {
    graph_.Clear();

    // --- Pass 0: depth linearization ---------------------------------------
    // Raw game depth (non-linear NDC, possibly reversed-Z) is unusable for
    // disocclusion detection and DLSS expects [0,1] view-linear depth. The
    // DepthProvider writes fc.depth_linear and leaves fc.depth untouched so
    // motion reprojection keeps consuming raw NDC depth for unprojection.
    if (config_.enable_depth_linearize) {
        graph_.AddPass(core::PassType::Capture, "DepthLinearize",
                       core::FrameValidity{ false, true, false, false, false, false, false, false },
                       core::ResourceAccess::ReadDepth, core::ResourceAccess::WriteDepth,
                       [this](core::FrameContext& fc, graphics::ICommandContext& cmd) -> core::PassResult {
            return depth_provider_.Process(fc, cmd);
        }, core::PassPolicy::Optional);
    }

    if (config_.enable_motion_vectors) {
        graph_.AddPass(core::PassType::MotionReproject, "MotionReproject",
                       core::FrameValidity{ false, true, false, false, false, false, false, false },
                       core::ResourceAccess::ReadDepth, core::ResourceAccess::WriteMotion,
                       [this](core::FrameContext& fc, graphics::ICommandContext& cmd) -> core::PassResult {
            return motion_pass_.Execute(fc, cmd);
        }, core::PassPolicy::Optional);
    }

    if (config_.enable_reactive_mask) {
        graph_.AddPass(core::PassType::ReactiveMask, "ReactiveMask",
                       core::FrameValidity{ true, true, false, false, false, false, false, false },
                       core::ResourceAccess::ReadColor | core::ResourceAccess::ReadDepth,
                       core::ResourceAccess::WriteReactive,
                       [this](core::FrameContext& fc, graphics::ICommandContext& cmd) -> core::PassResult {
            return reactive_pass_.Execute(fc, cmd, history_mgr_.GetCurrentHistoryTexture());
        }, core::PassPolicy::Optional);
    }

    if (config_.enable_disocclusion) {
        // Disocclusion compares the current depth against reprojected previous
        // depth, so motion vectors are NOT a prerequisite. Keeping the pass
        // independent of motion lets it run on games that provide depth but no
        // usable camera/motion data (depth-only temporal handling).
        graph_.AddPass(core::PassType::Disocclusion, "Disocclusion",
                       core::FrameValidity{ false, true, false, false, false, false, false, false },
                       core::ResourceAccess::ReadDepth,
                       core::ResourceAccess::WriteDisocc,
                       [this](core::FrameContext& fc, graphics::ICommandContext& cmd) -> core::PassResult {
            // Downstream consumers get linearized depth when available (raw
            // depth is unusable for disocclusion delta checks); fall back to
            // raw depth when the DepthProvider skipped this frame.
            core::GpuTexture depth_for_pass =
                fc.depth_linear.IsValid() ? fc.depth_linear : fc.depth;
            return disocclusion_pass_.Execute(fc, cmd, history_mgr_.GetPreviousDepthTexture(), depth_for_pass);
        }, core::PassPolicy::Optional);
    }

    if (reconstruction_backend_ && reconstruction_backend_->IsRuntimeAvailable()) {
        auto backend = reconstruction_backend_;
        graph_.AddPass(core::PassType::Upscale, std::string(backend->GetName()),
                       core::FrameValidity{ true, true, true, false, false, false, false, false },
                       core::ResourceAccess::ReadColor | core::ResourceAccess::ReadDepth |
                           core::ResourceAccess::ReadMotion | core::ResourceAccess::ReadReactive,
                       core::ResourceAccess::WriteColor,
                       [backend](core::FrameContext& fc, graphics::ICommandContext& ctx) -> core::PassResult {
            auto result = backend->Execute(fc, ctx);
            if (result.success && result.output.IsValid()) {
                fc.color = result.output;
                fc.output = result.output;
                fc.output_resolution = result.resolution;
                fc.validity.color_valid = true;
                return core::PassResult::Success;
            }
            return core::PassResult::Failed;
        }, core::PassPolicy::Required);
    }
}

bool Pipeline::ExecuteFrame(core::FrameContext& frame_ctx, graphics::ICommandContext& cmd_ctx) {
    if (!initialized_) return false;

    // Invalidate history preserving caller invalidation reason (ResolutionChanged, CameraCut, etc.)
    if (!frame_ctx.validity.history_valid && history_mgr_.HasValidHistory()) {
        history_mgr_.Invalidate(frame_ctx.invalidation_reason);
    }

    if (!cmd_ctx.BeginFrame()) {
        return false;
    }
    FrameScope scope{ cmd_ctx };

    frame_ctx.history = history_mgr_.GetCurrentHistoryTexture();
    frame_ctx.validity.history_valid = frame_ctx.history.IsValid();

    bool result = graph_.Execute(frame_ctx, cmd_ctx);

    // Only commit history if frame execution succeeded (do not poison history on failure)
    if (result) {
        // Commit LINEARIZED depth when available so the previous-depth history
        // stays in the same domain the Disocclusion pass consumes. Mixing raw
        // (this frame) with linear (history) would make the depth delta
        // meaningless.
        core::GpuTexture depth_for_history =
            frame_ctx.depth_linear.IsValid() ? frame_ctx.depth_linear : frame_ctx.depth;
        history_mgr_.CommitFrame(cmd_ctx, frame_ctx.color, depth_for_history);
    }

    bool end_ok = scope.Close();
    return result && end_ok;
}

}  // namespace omnirender::runtime
