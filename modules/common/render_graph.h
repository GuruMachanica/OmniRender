// filepath: modules/common/render_graph.h
#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
#include "frame_context.h"
#include "backend_capabilities.h"

namespace omnirender {

// Standardized pass sequence taxonomy.
enum class PassType : uint8_t {
    Capture = 0,
    DepthLinearize,
    MotionReproject,
    ReactiveMask,
    Disocclusion,
    TemporalResolve,
    RayTracing,
    Upscale,
    Tonemap,
    Present
};

// Resource access flags for read/write hazard and dependency tracking.
enum class ResourceAccess : uint16_t {
    None          = 0,
    ReadColor     = 1 << 0,
    WriteColor    = 1 << 1,
    ReadDepth     = 1 << 2,
    WriteDepth    = 1 << 3,
    ReadMotion    = 1 << 4,
    WriteMotion   = 1 << 5,
    ReadHistory   = 1 << 6,
    WriteHistory  = 1 << 7,
    ReadReactive  = 1 << 8,
    WriteReactive = 1 << 9,
    ReadDisocc    = 1 << 10,
    WriteDisocc   = 1 << 11
};

inline constexpr ResourceAccess operator|(ResourceAccess a, ResourceAccess b) noexcept {
    return static_cast<ResourceAccess>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline constexpr bool HasAccess(ResourceAccess mask, ResourceAccess flag) noexcept {
    return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(flag)) != 0;
}

// Prerequisites for pass execution.
struct PassRequirements {
    bool requires_depth = false;
    bool requires_motion = false;
    bool requires_history = false;
    bool requires_camera = false;
};

using PassExecuteFn = std::function<bool(FrameContext&)>;

// Node in the render execution sequence with dependency tracking.
struct RenderPassNode {
    PassType         type = PassType::Capture;
    const char*      name = "Pass";
    PassRequirements requirements{};
    ResourceAccess   reads = ResourceAccess::None;
    ResourceAccess   writes = ResourceAccess::None;
    PassExecuteFn    execute = nullptr;
    bool             enabled = true;
};

// Render graph scheduler with topological dependency resolution and hazard avoidance.
class RenderGraph {
public:
    RenderGraph() = default;

    void AddPass(PassType type, const char* name, PassRequirements req,
                 ResourceAccess reads, ResourceAccess writes, PassExecuteFn fn) {
        passes_.push_back({ type, name, req, reads, writes, std::move(fn), true });
        compiled_ = false;
    }

    void AddPass(PassType type, const char* name, PassRequirements req, PassExecuteFn fn) {
        AddPass(type, name, req, ResourceAccess::None, ResourceAccess::None, std::move(fn));
    }

    void SetPassEnabled(PassType type, bool enable) noexcept {
        for (auto& p : passes_) {
            if (p.type == type) p.enabled = enable;
        }
        compiled_ = false;
    }

    size_t GetPassCount() const noexcept { return passes_.size(); }
    const RenderPassNode& GetPass(size_t index) const noexcept { return passes_[index]; }
    const std::vector<size_t>& GetScheduledOrder() const noexcept { return scheduled_order_; }

    // Topologically sorts passes to guarantee producers execute before consumers (RAW)
    // and preserves registration ordering for WAW/WAR hazards.
    bool Compile() {
        const size_t n = passes_.size();
        scheduled_order_.clear();
        if (n == 0) {
            compiled_ = true;
            return true;
        }

        std::vector<std::vector<size_t>> adj(n);
        std::vector<size_t> in_degree(n, 0);

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;
                const auto& a = passes_[i];
                const auto& b = passes_[j];

                // RAW: B reads a resource produced by A
                uint16_t a_write_as_read = static_cast<uint16_t>(a.writes) >> 1;
                bool b_reads_from_a = (a_write_as_read & static_cast<uint16_t>(b.reads)) != 0;

                // WAW / WAR hazards for passes added in order i < j
                bool preserves_hazard_order = (i < j) && (
                    ((static_cast<uint16_t>(a.writes) & static_cast<uint16_t>(b.writes)) != 0) ||
                    ((static_cast<uint16_t>(b.writes) >> 1) & static_cast<uint16_t>(a.reads)) != 0
                );

                if (b_reads_from_a || preserves_hazard_order) {
                    adj[i].push_back(j);
                    in_degree[j]++;
                }
            }
        }

        std::vector<size_t> ready_queue;
        for (size_t i = 0; i < n; ++i) {
            if (in_degree[i] == 0) ready_queue.push_back(i);
        }

        scheduled_order_.reserve(n);
        while (!ready_queue.empty()) {
            size_t curr = ready_queue.front();
            ready_queue.erase(ready_queue.begin());
            scheduled_order_.push_back(curr);

            for (size_t neighbor : adj[curr]) {
                if (--in_degree[neighbor] == 0) {
                    ready_queue.push_back(neighbor);
                }
            }
        }

        compiled_ = (scheduled_order_.size() == n);
        return compiled_;
    }

    // Validates that all read dependencies are either produced or valid in FrameContext.
    bool ValidateDependencies(const FrameContext& ctx) const noexcept {
        uint16_t available = 0;
        if (ctx.validity.color_valid) available |= static_cast<uint16_t>(ResourceAccess::ReadColor);
        if (ctx.validity.depth_valid) available |= static_cast<uint16_t>(ResourceAccess::ReadDepth);
        if (ctx.validity.motion_valid) available |= static_cast<uint16_t>(ResourceAccess::ReadMotion);
        if (ctx.validity.history_valid) available |= static_cast<uint16_t>(ResourceAccess::ReadHistory);

        for (size_t idx : scheduled_order_) {
            const auto& pass = passes_[idx];
            if (!pass.enabled) continue;
            uint16_t r = static_cast<uint16_t>(pass.reads);
            if ((r & ~available) != 0) {
                // Pass reads a resource that is not yet produced or externally valid
                return false;
            }
            available |= (static_cast<uint16_t>(pass.writes) >> 1);
        }
        return true;
    }

    bool Execute(FrameContext& ctx) {
        if (!compiled_ && !Compile()) {
            return false;
        }

        for (size_t idx : scheduled_order_) {
            auto& pass = passes_[idx];
            if (!pass.enabled || !pass.execute) continue;
            if (pass.requirements.requires_depth && !ctx.validity.depth_valid) continue;
            if (pass.requirements.requires_motion && !ctx.validity.motion_valid) continue;
            if (pass.requirements.requires_history && !ctx.validity.history_valid) continue;
            if (pass.requirements.requires_camera && !ctx.validity.camera_valid) continue;

            if (!pass.execute(ctx)) {
                return false;
            }
        }
        return true;
    }

    void Clear() noexcept {
        passes_.clear();
        scheduled_order_.clear();
        compiled_ = false;
    }

private:
    std::vector<RenderPassNode> passes_;
    std::vector<size_t>         scheduled_order_;
    bool                        compiled_ = false;
};

}  // namespace omnirender
