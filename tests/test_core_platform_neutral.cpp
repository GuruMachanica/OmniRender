// filepath: tests/test_core_platform_neutral.cpp
// 100% platform-independent unit test for OmniRender Core and Runtime.
// Compiles with standard C++20 on Linux, macOS, and Windows with zero platform headers.

#include <cassert>
#include <cstdio>
#include <cmath>
#include <string>
#include "../core/frame/Resolution.h"
#include "../core/frame/FrameTiming.h"
#include "../core/frame/FrameValidity.h"
#include "../core/frame/FrameContext.h"
#include "../core/camera/CameraState.h"
#include "../core/temporal/JitterState.h"
#include "../core/temporal/HistoryState.h"
#include "../core/temporal/HistoryManager.h"
#include "../core/graph/RenderGraph.h"
#include "../runtime/BackendResolver.h"

using namespace omnirender;
using namespace omnirender::core;
using namespace omnirender::runtime;

static void TestResolution() {
    Resolution r1{ 1920, 1080 };
    Resolution r2{ 2560, 1440 };

    assert(r1.width == 1920 && r1.height == 1080);
    assert(std::fabs(r1.AspectRatio() - (16.0f / 9.0f)) < 0.001f);
    assert(r1.Is16By9());
    assert(r1 != r2);
    assert(r1 == (Resolution{ 1920, 1080 }));

    float scale = r2.ScaleFactorFrom(r1);
    assert(std::fabs(scale - (2560.0f / 1920.0f)) < 0.001f);
    printf("[PASS] TestResolution\n");
}

static void TestFrameTiming() {
    FrameTiming timing{};
    timing.frame_index = 42;
    timing.delta_time_ms = 16.6667f;
    timing.render_time_ms = 8.2f;

    assert(timing.frame_index == 42);
    assert(std::fabs(timing.InstantaneousFps() - 60.0f) < 0.1f);

    timing.Reset();
    assert(timing.frame_index == 0);
    assert(timing.delta_time_ms == 0.0f);
    printf("[PASS] TestFrameTiming\n");
}

static void TestFrameValidity() {
    FrameValidity fv{};
    assert(!fv.color_valid && !fv.depth_valid && !fv.motion_valid);

    fv.color_valid = true;
    fv.depth_valid = true;
    assert(fv.color_valid && fv.depth_valid);
    assert(!fv.motion_valid);

    fv.Reset();
    assert(!fv.color_valid && !fv.depth_valid);
    printf("[PASS] TestFrameValidity\n");
}

static void TestRenderGraphDependencies() {
    RenderGraph graph;
    int pass_exec_count = 0;

    // Pass 1: Capture
    graph.AddPass(PassType::Capture, "Capture",
                  FrameValidity{ false, false, false, false, false, false, false, false },
                  ResourceAccess::None, ResourceAccess::WriteColor | ResourceAccess::WriteDepth,
                  [&](FrameContext& ctx, graphics::ICommandContext&) {
                      pass_exec_count++;
                      ctx.validity.color_valid = true;
                      ctx.validity.depth_valid = true;
                      return true;
                  });

    // Pass 2: Motion Reprojection (requires depth)
    graph.AddPass(PassType::MotionReproject, "MotionReproject",
                  FrameValidity{ false, true, false, false, false, false, false, false },
                  ResourceAccess::ReadDepth, ResourceAccess::WriteMotion,
                  [&](FrameContext& ctx, graphics::ICommandContext&) {
                      pass_exec_count++;
                      ctx.validity.motion_valid = true;
                      return true;
                  });

    // Pass 3: DLSS Upscale (requires color + depth + motion)
    graph.AddPass(PassType::Upscale, "DLSS",
                  FrameValidity{ true, true, true, false, false, false, false, false },
                  ResourceAccess::ReadColor | ResourceAccess::ReadDepth | ResourceAccess::ReadMotion,
                  ResourceAccess::WriteColor,
                  [&](FrameContext&, graphics::ICommandContext&) {
                      pass_exec_count++;
                      return true;
                  });

    assert(graph.GetPassCount() == 3);

    // Context with zero validity
    FrameContext fc{};
    graphics::ICommandContext* null_ctx = nullptr;

    // Execution where all prerequisites chain properly
    bool ok = graph.Execute(fc, *null_ctx);
    assert(ok);
    assert(pass_exec_count == 3);
    assert(fc.validity.color_valid && fc.validity.depth_valid && fc.validity.motion_valid);

    // Negative test: When depth is not produced, Motion and Upscale should NOT run
    RenderGraph strict_graph;
    int strict_count = 0;
    strict_graph.AddPass(PassType::MotionReproject, "MotionReproject",
                         FrameValidity{ false, true, false, false, false, false, false, false },
                         ResourceAccess::ReadDepth, ResourceAccess::WriteMotion,
                         [&](FrameContext&, graphics::ICommandContext&) {
                             strict_count++;
                             return true;
                         });

    FrameContext invalid_fc{};
    invalid_fc.validity.depth_valid = false;
    bool req_ok = strict_graph.Execute(invalid_fc, *null_ctx);
    assert(!req_ok); // Required pass missing prerequisites MUST fail graph execution
    assert(strict_count == 0); // Must not execute

    // Optional pass skipped due to missing prerequisites should NOT fail graph
    RenderGraph opt_graph;
    int opt_count = 0;
    opt_graph.AddPass(PassType::Custom, "OptionalPass",
                      FrameValidity{ false, true, false, false, false, false, false, false },
                      ResourceAccess::ReadDepth, ResourceAccess::None,
                      [&](FrameContext&, graphics::ICommandContext&) {
                          opt_count++;
                          return true;
                      },
                      PassPolicy::Optional);
    bool opt_ok = opt_graph.Execute(invalid_fc, *null_ctx);
    assert(opt_ok);
    assert(opt_count == 0);

    printf("[PASS] TestRenderGraphDependencies\n");
}

static void TestHistoryStateMachine() {
    HistoryManager mgr;
    assert(mgr.GetState() == HistoryState::Empty);
    assert(mgr.GetValidFrameCount() == 0);
    assert(!mgr.HasValidHistory());

    // Invalidation reasons
    mgr.Invalidate(InvalidationReason::ResolutionChanged);
    assert(mgr.GetState() == HistoryState::Empty); // valid_frames was 0

    mgr.Invalidate(InvalidationReason::CameraCut);
    assert(mgr.GetState() == HistoryState::Empty);
    printf("[PASS] TestHistoryStateMachine\n");
}

static void TestBackendResolver() {
    RuntimeCapabilities caps{};

    // 1. Auto + NVIDIA RT hardware -> DLSS
    caps.supports_dlss = true;
    auto upscaler = BackendResolver::ResolveUpscaler(caps, PreferredUpscaler::Auto);
    assert(upscaler == PreferredUpscaler::DLSS);

    // 2. Auto + Intel hardware -> XeSS
    caps.supports_dlss = false;
    caps.supports_xess = true;
    upscaler = BackendResolver::ResolveUpscaler(caps, PreferredUpscaler::Auto);
    assert(upscaler == PreferredUpscaler::XeSS);

    // 3. Auto + AMD hardware -> FSR
    caps.supports_xess = false;
    caps.supports_fsr = true;
    upscaler = BackendResolver::ResolveUpscaler(caps, PreferredUpscaler::Auto);
    assert(upscaler == PreferredUpscaler::FSR);

    // 4. Explicit user override
    caps.supports_dlss = true;
    upscaler = BackendResolver::ResolveUpscaler(caps, PreferredUpscaler::DLSS);
    assert(upscaler == PreferredUpscaler::DLSS);

    assert(std::string(BackendResolver::GetUpscalerName(PreferredUpscaler::DLSS)) == "NVIDIA DLSS");
    assert(std::string(BackendResolver::GetUpscalerName(PreferredUpscaler::XeSS)) == "Intel XeSS");
    assert(std::string(BackendResolver::GetUpscalerName(PreferredUpscaler::FSR)) == "AMD FSR");
    printf("[PASS] TestBackendResolver\n");
}

int main() {
    printf("Running test_core_platform_neutral suite...\n");
    TestResolution();
    TestFrameTiming();
    TestFrameValidity();
    TestRenderGraphDependencies();
    TestHistoryStateMachine();
    TestBackendResolver();
    printf("All platform-neutral core tests passed successfully!\n");
    return 0;
}
