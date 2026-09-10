// filepath: tests/test_backend_capabilities.cpp
// Unit tests for FrameContext, backend capabilities, multi-variable scoring,
// GPU resource ownership, dependency-driven RenderGraph, and DLSS adapter.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../modules/common/frame_context.h"
#include "../modules/common/backend_capabilities.h"
#include "../modules/common/provider_interfaces.h"
#include "../modules/common/backend_interfaces.h"
#include "../modules/common/gpu_resource.h"
#include "../modules/common/render_graph.h"
#include "../modules/daemon/upscalers/dlss_adapter.h"
#include "../modules/daemon/upscalers/xess_adapter.h"

using namespace omnirender;
using namespace omnirender::daemon::upscaler;

static void TestFrameContextDefaults() {
    FrameContext ctx{};
    assert(!ctx.validity.color_valid && !ctx.validity.depth_valid && !ctx.validity.motion_valid);
    assert(!ctx.CanPerformSpatial() && !ctx.CanPerformTemporal());

    ctx.validity.color_valid = true;
    assert(ctx.CanPerformSpatial() && !ctx.CanPerformTemporal());

    ctx.validity.history_valid = ctx.validity.motion_valid = true;
    assert(ctx.CanPerformTemporal());
    printf("[PASS] TestFrameContextDefaults\n");
}

static void TestBackendCapabilitiesQuery() {
    BackendCapabilities dlss = QueryBackendCapabilities(BackendType::DLSS);
    assert(dlss.requires_motion && dlss.requires_depth);
    assert(dlss.super_resolution && dlss.frame_generation && dlss.ray_reconstruction);

    BackendCapabilities fsr = QueryBackendCapabilities(BackendType::FSR);
    assert(!fsr.requires_motion && !fsr.requires_depth && fsr.super_resolution);

    BackendCapabilities spatial = QueryBackendCapabilities(BackendType::SpatialFallback);
    assert(!spatial.requires_motion && !spatial.requires_depth);
    printf("[PASS] TestBackendCapabilitiesQuery\n");
}

static void TestBackendIdentificationAndRuntimeLayers() {
    BackendId dlss_id{ VendorId::Nvidia, TechnologyId::DLSS, FeatureId::SuperResolution, 3, 7 };
    assert(dlss_id.vendor == VendorId::Nvidia && dlss_id.technology == TechnologyId::DLSS);
    assert(dlss_id.feature == FeatureId::SuperResolution && dlss_id.version_major == 3);

    BackendId fsr_id{ VendorId::Amd, TechnologyId::FSR, FeatureId::SuperResolution, 1, 0 };
    assert(fsr_id.vendor == VendorId::Amd && fsr_id.technology == TechnologyId::FSR);

    RuntimeCapabilities runtime{};
    runtime.supports_d3d11 = true;
    runtime.supports_d3d12 = runtime.supports_fg = false;
    assert(!runtime.supports_fg);
    printf("[PASS] TestBackendIdentificationAndRuntimeLayers\n");
}

static void TestEvaluateBackendAndScoring() {
    FrameContext ctx{};
    ctx.validity.color_valid = true;

    CompatibilityResult res = EvaluateBackend(BackendType::DLSS, ctx, true);
    assert(!res.compatible && res.reason == FallbackReason::MissingMotion);

    ctx.validity.motion_valid = true;
    res = EvaluateBackend(BackendType::DLSS, ctx, true);
    assert(!res.compatible && res.reason == FallbackReason::MissingDepth);

    ctx.validity.depth_valid = true;
    res = EvaluateBackend(BackendType::DLSS, ctx, true);
    assert(res.compatible && res.score.quality >= 0.90f);

    ctx.validity.history_valid = true;
    res = EvaluateBackend(BackendType::OmniTemporal, ctx, true);
    assert(res.compatible && res.score.quality >= 0.80f);

    BackendScoreWeights q_weights = GetWeightsForPreference(UserPreference::Quality);
    BackendScoreWeights p_weights = GetWeightsForPreference(UserPreference::Performance);
    assert(q_weights.quality_weight > q_weights.performance_weight);
    assert(p_weights.performance_weight > p_weights.quality_weight);
    printf("[PASS] TestEvaluateBackendAndScoring\n");
}

static void TestNegotiationLadderAndPreferences() {
    FrameContext ctx{};
    ctx.validity.color_valid = true;
    FrameCapabilities caps = FrameCapabilities::FromFrameContext(ctx);
    assert(SelectOptimalBackend(caps, true, true, true, true) == BackendType::FSR);

    ctx.validity.depth_valid = ctx.validity.motion_valid = ctx.validity.history_valid = true;
    caps = FrameCapabilities::FromFrameContext(ctx);

    assert(SelectOptimalBackend(caps, true, true, true, true, UserPreference::Quality) == BackendType::DLSS);
    assert(SelectOptimalBackend(caps, false, true, true, true, UserPreference::Quality) == BackendType::XeSS);
    assert(SelectOptimalBackend(caps, false, false, true, true, UserPreference::Quality) == BackendType::OmniTemporal);
    assert(SelectOptimalBackend(caps, true, true, true, true, UserPreference::Performance) == BackendType::FSR);
    assert(SelectOptimalBackend(caps, false, false, false, false) == BackendType::SpatialFallback);
    printf("[PASS] TestNegotiationLadderAndPreferences\n");
}

static void TestGpuResourceOwnership() {
    GpuResourceDescriptor desc{};
    desc.width = 1920;
    desc.height = 1080;
    desc.format = TextureFormat::R8G8B8A8_UNORM;
    desc.ownership = ResourceOwnership::SharedRead;
    desc.state = ResourceLifetimeState::Allocated;

    int dummy = 42;
    GpuTexture tex(&dummy, desc);
    assert(tex.IsValid() && tex.GetOwnership() == ResourceOwnership::SharedRead);
    assert(tex.TransferOwnership(ResourceOwnership::OmniRenderOwned, 1001));
    assert(tex.TransitionTo(ResourceLifetimeState::Acquired));
    assert(tex.TransitionTo(ResourceLifetimeState::InUse));
    assert(tex.GetLifetimeState() == ResourceLifetimeState::InUse);
    printf("[PASS] TestGpuResourceOwnership\n");
}

static void TestRenderGraphTopologicalScheduling() {
    RenderGraph graph;
    std::vector<const char*> execution_order;

    // Register the producer before its consumer. A graph edge can only be
    // satisfied in one direction: Disocclusion reads Motion, which the Motion
    // pass writes, so Motion must be scheduled first (RAW edge). Registering
    // Disocclusion first would additionally create a WAR edge and a cycle.
    graph.AddPass(PassType::MotionReproject, "MotionReproject", { true, false, false, false },
                  ResourceAccess::ReadDepth, ResourceAccess::WriteMotion,
                  [&](FrameContext&) { execution_order.push_back("MotionReproject"); return true; });
    graph.AddPass(PassType::Disocclusion, "Disocclusion", { true, true, false, false },
                  ResourceAccess::ReadDepth | ResourceAccess::ReadMotion, ResourceAccess::WriteDisocc,
                  [&](FrameContext&) { execution_order.push_back("Disocclusion"); return true; });

    assert(graph.Compile());

    FrameContext ctx{};
    ctx.validity.color_valid = ctx.validity.depth_valid = ctx.validity.motion_valid = true;
    assert(graph.Execute(ctx));
    assert(execution_order.size() == 2);
    // Compare content, not string-literal addresses (which assert() only
    // type-checks in Debug builds and would otherwise be UB in both).
    assert(std::strcmp(execution_order[0], "MotionReproject") == 0);
    assert(std::strcmp(execution_order[1], "Disocclusion") == 0);
    printf("[PASS] TestRenderGraphTopologicalScheduling\n");
}

static void TestDlssAdapterExecution() {
    DlssAdapter adapter;
    assert(adapter.GetType() == BackendType::DLSS);

    BackendId id = adapter.GetId();
    assert(id.vendor == VendorId::Nvidia && id.technology == TechnologyId::DLSS);

    BackendCapabilities family_caps = adapter.GetCapabilities();
    assert(family_caps.super_resolution && family_caps.frame_generation);

    RuntimeCapabilities run_caps = adapter.GetRuntimeCapabilities();
    assert(!run_caps.supports_fg); // FG requires D3D12, D3D11 returns false

    assert(adapter.Initialize(1920, 1080));
    assert(adapter.GetState() == DlssState::DllNotFound || adapter.GetState() == DlssState::GpuUnsupported);

    // Strict input rejection test
    FrameContext invalid_ctx{};
    assert(!adapter.Execute(invalid_ctx));
    assert(adapter.GetState() == DlssState::InputInvalid);

    // Valid inputs: test parameter binding. DlssEvaluationParams stores native
    // ID3D11Resource* pointers, so the probe resource must be typed as such
    // (assert() only type-checks its arguments in Debug builds).
    FrameContext ctx{};
    int dummy = 1;
    ID3D11Resource* dummy_res = reinterpret_cast<ID3D11Resource*>(&dummy);
    ctx.color.resource = dummy_res;
    ctx.depth.resource = dummy_res;
    ctx.motion.resource = dummy_res;
    ctx.color.source = DataSource::HookExtracted;
    ctx.validity.color_valid = ctx.validity.depth_valid = ctx.validity.motion_valid = true;
    ctx.resolution.input_width = 1280;
    ctx.resolution.input_height = 720;
    ctx.resolution.output_width = 1920;
    ctx.resolution.output_height = 1080;
    ctx.jitter.offset_x = 0.25f;
    ctx.jitter.offset_y = -0.5f;

    DlssEvaluationParams params{};
    assert(adapter.BindFrameContextParameters(ctx, params));
    assert(adapter.GetState() == DlssState::ParametersBound);
    assert(params.in_color == dummy_res && params.depth == dummy_res && params.motion == dummy_res);
    assert(params.render_width == 1280 && params.target_width == 1920);
    assert(params.jitter_offset_x == 0.25f && params.jitter_offset_y == -0.5f);

    // Execution via RenderGraph with deterministic fallback
    int fallback_ran = 0;
    RenderGraph graph;
    graph.AddPass(PassType::Upscale, "DLSS", { true, true, false, false },
                  ResourceAccess::ReadColor | ResourceAccess::ReadDepth | ResourceAccess::ReadMotion,
                  ResourceAccess::WriteColor, [&](FrameContext& fc) {
        if (!adapter.Execute(fc)) {
            fallback_ran++;
            return true;
        }
        return true;
    });

    assert(graph.Compile());
    assert(graph.Execute(ctx));
    assert(fallback_ran == 1);
    assert(adapter.GetState() == DlssState::ExecutionFailed);
    assert(ctx.color.source != DataSource::Reconstructed); // Never fake Reconstructed!

    // Verify all DlssState strings
    assert(GetDlssStateString(DlssState::DllNotFound) != nullptr);
    assert(GetDlssStateString(DlssState::GpuUnsupported) != nullptr);
    assert(GetDlssStateString(DlssState::SdkLoaded) != nullptr);
    assert(GetDlssStateString(DlssState::ContextCreated) != nullptr);
    assert(GetDlssStateString(DlssState::FeatureCreated) != nullptr);
    assert(GetDlssStateString(DlssState::ParametersBound) != nullptr);
    assert(GetDlssStateString(DlssState::InputInvalid) != nullptr);
    assert(GetDlssStateString(DlssState::ExecutionFailed) != nullptr);
    assert(GetDlssStateString(DlssState::ExecutionSucceeded) != nullptr);

    adapter.Shutdown();
    printf("[PASS] TestDlssAdapterExecution\n");
}

static void TestXessAdapterExecution() {
    XessAdapter adapter;
    assert(adapter.GetType() == BackendType::XeSS);

    BackendId id = adapter.GetId();
    assert(id.vendor == VendorId::Intel && id.technology == TechnologyId::XeSS);

    BackendCapabilities family_caps = adapter.GetCapabilities();
    assert(family_caps.super_resolution && family_caps.requires_motion && family_caps.requires_depth);

    RuntimeCapabilities run_caps = adapter.GetRuntimeCapabilities();
    assert(!run_caps.supports_fg);

    assert(adapter.Initialize(1920, 1080));

    FrameContext ctx{};
    assert(!adapter.Execute(ctx)); // Fails cleanly on missing inputs

    int dummy = 1;
    ctx.color.resource = &dummy;
    ctx.depth.resource = &dummy;
    ctx.motion.resource = &dummy;
    ctx.validity.color_valid = ctx.validity.depth_valid = ctx.validity.motion_valid = true;

    // Execution via RenderGraph
    RenderGraph graph;
    graph.AddPass(PassType::Upscale, "XeSS", { true, true, false, false },
                  ResourceAccess::ReadColor | ResourceAccess::ReadDepth | ResourceAccess::ReadMotion,
                  ResourceAccess::WriteColor, [&](FrameContext& fc) {
        return adapter.Execute(fc);
    });

    assert(graph.Compile());
    // Commit 9 contract: XeSS must NOT claim success until the real
    // xessD3D11Execute path is implemented. The adapter must reject
    // execution (so the pipeline falls back to the spatial path) rather
    // than fabricating DataSource::Reconstructed on absent hardware.
    assert(!graph.Execute(ctx));
    assert(adapter.Execute(ctx) == false);
    assert(ctx.color.source != DataSource::Reconstructed);

    adapter.Shutdown();
    printf("[PASS] TestXessAdapterExecution\n");
}

int main() {
    printf("Running test_backend_capabilities...\n");
    TestFrameContextDefaults();
    TestBackendCapabilitiesQuery();
    TestBackendIdentificationAndRuntimeLayers();
    TestEvaluateBackendAndScoring();
    TestNegotiationLadderAndPreferences();
    TestGpuResourceOwnership();
    TestRenderGraphTopologicalScheduling();
    TestDlssAdapterExecution();
    TestXessAdapterExecution();
    printf("All backend capabilities, scoring, DLSS, and XeSS adapter tests passed!\n");
    return 0;
}
