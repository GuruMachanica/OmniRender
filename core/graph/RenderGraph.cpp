// filepath: core/graph/RenderGraph.cpp
#include "RenderGraph.h"

namespace omnirender::core {

void RenderGraph::AddPass(PassType type,
                          std::string name,
                          FrameValidity prerequisites,
                          ResourceAccess reads,
                          ResourceAccess writes,
                          PassExecutionFn execute,
                          PassPolicy policy) {
    passes_.push_back(PassNode{
        type,
        std::move(name),
        prerequisites,
        reads,
        writes,
        std::move(execute),
        policy,
        true
    });
}

void RenderGraph::AddPass(PassType type,
                          std::string name,
                          FrameValidity prerequisites,
                          ResourceAccess reads,
                          ResourceAccess writes,
                          LegacyPassExecutionFn execute,
                          PassPolicy policy) {
    auto adapter = [legacy = std::move(execute)](FrameContext& fc, graphics::ICommandContext& ctx) -> PassResult {
        if (!legacy) return PassResult::Success;
        return legacy(fc, ctx) ? PassResult::Success : PassResult::Failed;
    };
    AddPass(type, std::move(name), prerequisites, reads, writes, std::move(adapter), policy);
}

bool RenderGraph::CheckPrerequisites(const FrameValidity& current, const FrameValidity& req) const noexcept {
    if (req.color_valid && !current.color_valid) return false;
    if (req.depth_valid && !current.depth_valid) return false;
    if (req.motion_valid && !current.motion_valid) return false;
    if (req.reactive_valid && !current.reactive_valid) return false;
    if (req.disocc_valid && !current.disocc_valid) return false;
    if (req.history_valid && !current.history_valid) return false;
    if (req.camera_valid && !current.camera_valid) return false;
    if (req.jitter_valid && !current.jitter_valid) return false;
    return true;
}

bool RenderGraph::Execute(FrameContext& frame_ctx, graphics::ICommandContext& cmd_ctx) {
    bool all_succeeded = true;
    for (auto& pass : passes_) {
        if (!pass.enabled) continue;

        if (!CheckPrerequisites(frame_ctx.validity, pass.prerequisites)) {
            if (pass.policy == PassPolicy::Required) {
                all_succeeded = false;
            }
            continue;
        }

        if (pass.execute) {
            PassResult result = pass.execute(frame_ctx, cmd_ctx);
            if (result == PassResult::Failed) {
                all_succeeded = false;
            } else if (result == PassResult::Skipped && pass.policy == PassPolicy::Required) {
                all_succeeded = false;
            }
        }
    }
    return all_succeeded;
}

}  // namespace omnirender::core
