// filepath: core/graph/RenderGraph.h
#pragma once

#include <vector>
#include <functional>
#include <string>
#include "RenderGraphTypes.h"
#include "../frame/FrameContext.h"

namespace omnirender::graphics {
class ICommandContext;
}

namespace omnirender::core {

using PassExecutionFn = std::function<PassResult(FrameContext&, graphics::ICommandContext&)>;
using LegacyPassExecutionFn = std::function<bool(FrameContext&, graphics::ICommandContext&)>;

struct PassNode {
    PassType        type;
    std::string     name;
    FrameValidity   prerequisites;
    ResourceAccess  reads  = ResourceAccess::None;
    ResourceAccess  writes = ResourceAccess::None;
    PassExecutionFn execute;
    PassPolicy      policy  = PassPolicy::Required;
    bool            enabled = true;
};

class RenderGraph {
public:
    RenderGraph() = default;

    void AddPass(PassType type,
                 std::string name,
                 FrameValidity prerequisites,
                 ResourceAccess reads,
                 ResourceAccess writes,
                 PassExecutionFn execute,
                 PassPolicy policy = PassPolicy::Required);

    void AddPass(PassType type,
                 std::string name,
                 FrameValidity prerequisites,
                 ResourceAccess reads,
                 ResourceAccess writes,
                 LegacyPassExecutionFn execute,
                 PassPolicy policy = PassPolicy::Required);

    bool Execute(FrameContext& frame_ctx, graphics::ICommandContext& cmd_ctx);
    void Clear() noexcept { passes_.clear(); }

    [[nodiscard]] size_t GetPassCount() const noexcept { return passes_.size(); }
    [[nodiscard]] const std::vector<PassNode>& GetPasses() const noexcept { return passes_; }

private:
    [[nodiscard]] bool CheckPrerequisites(const FrameValidity& current, const FrameValidity& required) const noexcept;

    std::vector<PassNode> passes_;
};

}  // namespace omnirender::core
