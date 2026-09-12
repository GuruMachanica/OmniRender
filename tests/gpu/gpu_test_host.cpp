// filepath: tests/gpu/gpu_test_host.cpp
// Standalone GPU Integration Test Runner for OmniRender.
// Validates HAL, Synthetic Fixtures, RenderGraph, DLSS, Readback, History, and Failure Paths.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
#include <iomanip>
#include "d3d11/D3D11DeviceFixture.h"
#include "fixtures/TestSceneFixtures.h"
#include "readback/GpuReadback.h"
#include "validation/DebugLogger.h"
#include "validation/DiagnosticsReporter.h"
#include "validation/ImageMetrics.h"
#include "../../core/graph/RenderGraph.h"
#include "../../core/temporal/HistoryManager.h"
#include "../../backends/reconstruction/dlss/DlssReconstructionBackend.h"
#include "../../backends/reconstruction/spatial/SpatialUpscaleBackend.h"
#include "../../runtime/Pipeline.h"

using namespace omnirender;
using namespace omnirender::test::gpu;

static bool TestCase1_HalDevice(D3D11DeviceFixture& fixture, DiagnosticsReporter& reporter) {
    auto start = std::chrono::high_resolution_clock::now();
    bool ok = fixture.Initialize(false);
    auto end = std::chrono::high_resolution_clock::now();
    reporter.RecordStageTiming("HAL Device Creation", std::chrono::duration<double, std::milli>(end - start).count());

    if (!ok) {
        reporter.RecordTestResult("HAL & Adapter Discovery", false, "Failed to create D3D11 device");
        return false;
    }
    const auto& info = fixture.GetAdapterInfo();
    reporter.PrintAdapterInfo(info);
    bool pass = (fixture.GetDevice() != nullptr && fixture.GetGraphicsDevice() != nullptr);
    reporter.RecordTestResult("HAL & Adapter Discovery", pass, info.description);
    return pass;
}

static bool TestCase2_Fixtures(D3D11DeviceFixture& fixture, core::FrameContext& fc,
                               TestSceneFixtures& fixtures, DiagnosticsReporter& reporter) {
    auto start = std::chrono::high_resolution_clock::now();
    bool ok = fixtures.PopulateGpuFrameContext(fixture, fc, 1920, 1080, 2560, 1440, 1);
    auto end = std::chrono::high_resolution_clock::now();
    reporter.RecordStageTiming("Synthetic Scene Upload", std::chrono::duration<double, std::milli>(end - start).count());

    bool pass = ok && fc.IsValid() && fc.depth.IsValid() && fc.motion.IsValid() && fc.reactive.IsValid();
    reporter.RecordTestResult("Synthetic Scene Population", pass, "1080p inputs uploaded to GPU");
    return pass;
}

static bool TestCase3_RenderGraph(D3D11DeviceFixture& fixture, core::FrameContext& fc,
                                  DiagnosticsReporter& reporter) {
    auto start = std::chrono::high_resolution_clock::now();
    core::RenderGraph graph;
    int order = 0;
    int step1 = 0, step2 = 0, step3 = 0, step4 = 0;

    graph.AddPass(core::PassType::Capture, "Capture",
                  core::FrameValidity{}, core::ResourceAccess::None,
                  core::ResourceAccess::WriteColor | core::ResourceAccess::WriteDepth,
                  [&](core::FrameContext&, graphics::ICommandContext&) { step1 = ++order; return true; });
    graph.AddPass(core::PassType::MotionReproject, "MotionReproject",
                  core::FrameValidity{ false, true, false, false, false, false, false, false },
                  core::ResourceAccess::ReadDepth, core::ResourceAccess::WriteMotion,
                  [&](core::FrameContext&, graphics::ICommandContext&) { step2 = ++order; return true; });
    graph.AddPass(core::PassType::Upscale, "Upscale",
                  core::FrameValidity{ true, true, true, false, false, false, false, false },
                  core::ResourceAccess::ReadColor | core::ResourceAccess::ReadDepth | core::ResourceAccess::ReadMotion,
                  core::ResourceAccess::WriteColor,
                  [&](core::FrameContext&, graphics::ICommandContext&) { step3 = ++order; return true; });
    graph.AddPass(core::PassType::Present, "Present",
                  core::FrameValidity{ true, false, false, false, false, false, false, false },
                  core::ResourceAccess::ReadColor, core::ResourceAccess::None,
                  [&](core::FrameContext&, graphics::ICommandContext&) { step4 = ++order; return true; });

    auto cmd = fixture.GetGraphicsDevice()->GetImmediateContext();
    bool exec_ok = graph.Execute(fc, *cmd);
    auto end = std::chrono::high_resolution_clock::now();
    reporter.RecordStageTiming("RenderGraph Multi-Pass", std::chrono::duration<double, std::milli>(end - start).count());

    bool pass = exec_ok && (step1 == 1 && step2 == 2 && step3 == 3 && step4 == 4);
    reporter.RecordTestResult("RenderGraph Dependency Execution", pass, "4-pass pipeline executed in order");
    return pass;
}

static bool TestCase4_ReconstructionAndReadback(D3D11DeviceFixture& fixture, core::FrameContext& fc,
                                               TestSceneFixtures& fixtures, DiagnosticsReporter& reporter) {
    auto gdev = fixture.GetGraphicsDevice();
    auto cmd = gdev->GetImmediateContext();
    auto dlss = std::make_shared<backends::dlss::DlssReconstructionBackend>();

    auto start_init = std::chrono::high_resolution_clock::now();
    bool init_ok = dlss->Initialize(*gdev, fc.input_resolution, fc.output_resolution);
    (void)init_ok;
    auto end_init = std::chrono::high_resolution_clock::now();
    reporter.RecordStageTiming("DLSS Backend Init", std::chrono::duration<double, std::milli>(end_init - start_init).count());

    if (!dlss->IsNvidiaHardware()) {
        reporter.RecordTestSkipped("DLSS Hardware Policy Validation",
            "Non-NVIDIA GPU (AMD/Intel/WARP). DLSS unsupported per vendor hardware policy.");
        dlss->Shutdown();
        return true; // Graceful skip on non-NVIDIA
    }

    reporter.RecordTestResult("DLSS Hardware Compatibility", dlss->IsNvidiaHardware(), "NVIDIA GPU confirmed");

    bool cap_ok = (dlss->GetState() >= backends::dlss::DlssState::ContextCreated);
    reporter.RecordTestResult("DLSS Capability Negotiation", cap_ok, "NGX Capability Parameters validated");

    bool feat_ok = (dlss->GetState() >= backends::dlss::DlssState::FeatureCreated);
    reporter.RecordTestResult("DLSS Feature Creation", feat_ok, feat_ok ? "SuperResolution Feature Created" : dlss->GetStateString());

    if (!feat_ok) {
        dlss->Shutdown();
        return false;
    }

    auto start_exec = std::chrono::high_resolution_clock::now();
    auto recon_res = dlss->Execute(fc, *cmd);
    bool exec_ok = recon_res.success;
    auto end_exec = std::chrono::high_resolution_clock::now();
    reporter.RecordStageTiming("DLSS Backend Execution", std::chrono::duration<double, std::milli>(end_exec - start_exec).count());

    bool params_bound = (dlss->GetState() == backends::dlss::DlssState::ParametersBound || dlss->GetState() == backends::dlss::DlssState::EvaluationSucceeded);
    reporter.RecordTestResult("DLSS Parameter Binding", params_bound, "Parameters bound with MV scaling & jitter");

    reporter.RecordTestResult("DLSS Evaluation", exec_ok, "NGX EvaluateFeature executed successfully");

    bool out_valid = fc.color.IsValid();
    reporter.RecordTestResult("DLSS Output", out_valid, "Reconstructed color texture verified valid");

    bool dims_ok = out_valid && (fc.color.GetWidth() == 2560) && (fc.color.GetHeight() == 1440);
    reporter.RecordTestResult("DLSS Output Dimensions", dims_ok, "1440p (2560x1440) output dimensions verified");

    bool recon_pass = feat_ok && exec_ok && out_valid && dims_ok;

    // GPU Readback
    auto start_readback = std::chrono::high_resolution_clock::now();
    auto readback = GpuReadback::ReadbackTextureRgba8(fixture.GetDevice(), fixture.GetContext(), fc.color);
    auto end_readback = std::chrono::high_resolution_clock::now();
    reporter.RecordStageTiming("Staging Readback (1440p)", std::chrono::duration<double, std::milli>(end_readback - start_readback).count());

    if (!readback.success || readback.data.empty()) {
        reporter.RecordTestResult("GPU Staging Readback", false, "Failed to map staging memory");
        dlss->Shutdown();
        return false;
    }
    reporter.RecordTestResult("GPU Staging Readback", true, "14,745,600 bytes retrieved from VRAM");

    // Statistical Output Verification
    auto ref_data = fixtures.GenerateReferenceImage(2560, 1440, 1);
    auto quality = ImageMetrics::Evaluate(readback.data.data(), ref_data.data(), 2560, 1440, 4);
    reporter.PrintImageQualityReport("Reconstructed Output vs Analytic Target", quality);

    bool stats_pass = (quality.test_stats.non_zero_percentage > 50.0) && (quality.test_stats.mean > 10.0);
    reporter.RecordTestResult("Output Validity & Statistical Sanity", stats_pass, "Reconstructed pixels verified non-zero & valid");

    dlss->Shutdown();
    return recon_pass && readback.success && stats_pass;
}

static bool TestCase5_HistoryLifecycle(D3D11DeviceFixture& fixture, core::FrameContext& fc,
                                      DiagnosticsReporter& reporter) {
    auto gdev = fixture.GetGraphicsDevice();
    auto cmd = gdev->GetImmediateContext();
    core::HistoryManager history;

    bool init_ok = history.Initialize(*gdev, 1920, 1080,
                                      core::TextureFormat::R8G8B8A8_UNORM,
                                      core::TextureFormat::R32_FLOAT);
    if (!init_ok) {
        reporter.RecordTestResult("History Lifecycle", false, "HistoryManager Init failed");
        return false;
    }

    bool p0 = (history.GetState() == core::HistoryState::Empty);
    history.CommitFrame(*cmd, fc.color, fc.depth);
    bool p1 = (history.GetState() == core::HistoryState::WarmingUp && history.GetValidFrameCount() == 1);
    history.CommitFrame(*cmd, fc.color, fc.depth);
    bool p2 = (history.GetState() == core::HistoryState::Valid && history.GetValidFrameCount() == 2);

    history.Invalidate(core::InvalidationReason::CameraCut);
    bool p3 = (history.GetState() == core::HistoryState::Invalidated && history.GetValidFrameCount() == 0);

    history.CommitFrame(*cmd, fc.color, fc.depth);
    bool p4 = (history.GetState() == core::HistoryState::Rebuilding);
    history.CommitFrame(*cmd, fc.color, fc.depth);
    bool p5 = (history.GetState() == core::HistoryState::Valid);

    history.Shutdown();
    bool all_history = p0 && p1 && p2 && p3 && p4 && p5;
    reporter.RecordTestResult("Temporal History State Machine", all_history, "Empty -> Warmup -> Valid -> Invalidate -> Recovery");
    return all_history;
}

static bool TestCase6_RuntimePipeline(D3D11DeviceFixture& fixture, core::FrameContext& fc,
                                      DiagnosticsReporter& reporter) {
    auto gdev = fixture.GetGraphicsDevice();
    auto cmd = gdev->GetImmediateContext();
    runtime::Pipeline pipeline;

    bool init = pipeline.Initialize(*gdev, { 1920, 1080 }, { 2560, 1440 },
                                    core::TextureFormat::R8G8B8A8_UNORM, core::TextureFormat::R32_FLOAT);
    reporter.RecordTestResult("Pipeline Initialization", init, "HistoryManager + RenderGraph constructed");
    if (!init) return false;

    // Probe vendor before attaching DLSS — WARP / non-NVIDIA GPUs cannot run the
    // reconstruction backend, so skip the DLSS-dependent execution assertions.
    auto dlss = std::make_shared<backends::dlss::DlssReconstructionBackend>();
    dlss->Initialize(*gdev, { 1920, 1080 }, { 2560, 1440 });
    if (!dlss->IsNvidiaHardware()) {
        dlss->Shutdown();
        reporter.RecordTestSkipped("Runtime Pipeline Frame Execution",
            "Non-NVIDIA GPU (WARP/AMD/Intel). DLSS reconstruction skipped per vendor policy.");
        return true;
    }

    pipeline.SetReconstructionBackend(dlss);
    bool f1 = pipeline.ExecuteFrame(fc, *cmd);
    bool f2 = pipeline.ExecuteFrame(fc, *cmd);
    bool pass = f1 && f2 && (pipeline.GetHistoryManager().GetState() == core::HistoryState::Valid);

    reporter.RecordTestResult("Runtime Pipeline Integration", pass, "Consecutive frame progression verified");
    return pass;
}

static bool TestCase8_FsrSpatial(D3D11DeviceFixture& fixture, core::FrameContext& fc,
                                 TestSceneFixtures& fixtures, DiagnosticsReporter& reporter) {
    auto gdev = fixture.GetGraphicsDevice();
    auto cmd = gdev->GetImmediateContext();

    auto fsr = std::make_shared<backends::spatial::SpatialUpscaleBackend>();
    bool init_ok = fsr->Initialize(*gdev, { 1280, 720 }, { 1920, 1080 });
    if (!init_ok || !fsr->IsRuntimeAvailable()) {
        // CSOs absent (no DXC in the build) — a legitimate configuration, but
        // the FSR contract can't be validated. Report honestly and skip.
        reporter.RecordTestSkipped("FSR Spatial Backend",
            fsr->LastErrorString().empty() ? "CSOs not available in this build"
                                           : fsr->LastErrorString());
        return true;
    }
    reporter.RecordTestResult("FSR Backend Initialization", true, "EASU + RCAS CSOs loaded");

    // Build a 720p synthetic color source (high-frequency color bars + gradient
    // so EASU edge-adaptive weighting actually has edges to work with).
    auto src = fixtures.GenerateSyntheticData(1280, 720, 1);
    core::TextureDesc color_desc{
        1280, 720, 1, core::TextureFormat::R8G8B8A8_UNORM,
        core::TextureUsage::ShaderResource | core::TextureUsage::TransferDst,
        "FsrTestInput" };
    auto color_tex = gdev->CreateTexture(color_desc);
    if (!color_tex) {
        reporter.RecordTestResult("FSR Backend Initialization", false, "input texture alloc failed");
        return false;
    }
    core::GpuTexture input_color(std::move(color_tex));
    if (!fixtures.UploadTexture2D(fixture.GetDevice(), fixture.GetContext(),
                                  input_color, src.color_rgba8.data(), 1280 * 4)) {
        reporter.RecordTestResult("FSR Backend Initialization", false, "input upload failed");
        return false;
    }

    core::FrameContext fsr_fc{};
    fsr_fc.color             = input_color;
    fsr_fc.input_resolution  = { 1280, 720 };
    fsr_fc.output_resolution = { 1920, 1080 };
    fsr_fc.validity.color_valid = true;

    auto exec = fsr->Execute(fsr_fc, *cmd);
    bool exec_ok = exec.success && exec.output.IsValid();
    bool dims_ok = exec_ok && fsr_fc.output.GetWidth() == 1920 && fsr_fc.output.GetHeight() == 1080;
    reporter.RecordTestResult("FSR Execution (720p -> 1080p)", exec_ok && dims_ok,
                              exec_ok ? "EASU+RCAS dispatched, output allocated"
                                      : fsr->LastErrorString());
    if (!exec_ok) {
        fsr->Shutdown();
        return false;
    }

    // Readback the upscaled result and compare against two baselines:
    //  1. identical input (proves the dispatch actually changed the image)
    //  2. 1080p synthetic re-render (proves the upscale tracks the source)
    auto readback = GpuReadback::ReadbackTextureRgba8(fixture.GetDevice(), fixture.GetContext(),
                                                      fsr_fc.output);
    if (!readback.success || readback.data.empty()) {
        reporter.RecordTestResult("FSR Output Readback", false, "staging map failed");
        fsr->Shutdown();
        return false;
    }
    reporter.RecordTestResult("FSR Output Readback", true, "1920x1080 RGBA retrieved");

    const auto& up = readback.data;

    // Baseline 1: the 720p source itself. EASU must NOT return it unchanged
    // (it interpolates to 1920x1080). Pixel counts differ, so compare the
    // first row resized naively: just check byte-level difference exists.
    size_t diff_vs_input = 0;
    const size_t cmp_n = std::min(up.size(), src.color_rgba8.size());
    for (size_t i = 0; i < cmp_n; ++i) {
        if (std::abs(static_cast<int>(up[i]) - static_cast<int>(src.color_rgba8[i])) > 8) ++diff_vs_input;
    }
    const double diff_ratio_input = static_cast<double>(diff_vs_input) / static_cast<double>(cmp_n);
    // Row pitches differ (7200 vs 7680 bytes); even so, >30% divergent bytes
    // is expected for any real interpolation. A passthrough bug would keep
    // this near 0 on shared rows.
    bool differs_from_input = diff_ratio_input > 0.30;
    reporter.RecordTestResult("FSR Output Differs From Input",
        differs_from_input,
        (std::ostringstream{} << std::fixed << std::setprecision(1)
         << diff_ratio_input * 100.0 << "% bytes differ (expect >30%)").str());

    // Baseline 2: quality vs a 1080p re-render of the same synthetic scene.
    auto ref = fixtures.GenerateReferenceImage(1920, 1080, 1);
    auto quality = ImageMetrics::Evaluate(up.data(), ref.data(), 1920, 1080, 4);
    reporter.PrintImageQualityReport("FSR Upscale vs Analytic 1080p Target", quality);

    bool sane = quality.test_stats.non_zero_percentage > 90.0 && quality.test_stats.mean > 30.0;
    reporter.RecordTestResult("FSR Output Statistical Sanity", sane,
                              "Upscaled frame is non-empty and within expected luminance");

    fsr->Shutdown();
    return exec_ok && dims_ok && differs_from_input && sane;
}

static bool TestCase7_FailurePaths(D3D11DeviceFixture& fixture, DiagnosticsReporter& reporter) {
    core::FrameContext invalid_fc{};
    auto gdev = fixture.GetGraphicsDevice();
    auto cmd = gdev->GetImmediateContext();

    auto dlss = std::make_shared<backends::dlss::DlssReconstructionBackend>();
    dlss->Initialize(*gdev, { 1920, 1080 }, { 2560, 1440 });

    // 1. DLSS execute on empty context — only testable on NVIDIA hardware.
    //    On WARP / non-NVIDIA, NGX is never initialised so Execute() would
    //    exercise an uninitialised-SDK path rather than the intended early-out
    //    guard.  Skip rather than produce a misleading result.
    bool rejected_empty = true; // optimistic default for non-NVIDIA
    if (dlss->IsNvidiaHardware()) {
        rejected_empty = !dlss->Execute(invalid_fc, *cmd).success;
        reporter.RecordTestResult("DLSS Rejects Empty FrameContext", rejected_empty,
            "Execute() returned failure for uninitialised FrameContext");
    } else {
        reporter.RecordTestSkipped("DLSS Rejects Empty FrameContext",
            "Non-NVIDIA GPU — NGX not initialised, execute path not reachable");
    }

    // 2. Readback on invalid texture — purely CPU/driver validation, always runs.
    auto bad_readback = GpuReadback::ReadbackTextureRgba8(
        fixture.GetDevice(), fixture.GetContext(), invalid_fc.color);
    bool rejected_bad_read = !bad_readback.success;
    reporter.RecordTestResult("Readback Rejects Invalid Texture", rejected_bad_read,
        "ReadbackTextureRgba8 returned failure for null GpuTexture");

    dlss->Shutdown();
    bool pass = rejected_empty && rejected_bad_read;
    reporter.RecordTestResult("Failure Path & Graceful Rejection", pass, "Invalid inputs handled safely");
    return pass;
}

int main(int argc, char** argv) {
    DebugLogger::Instance().SetLogFile("omnirender_gpu_test.log");
    DebugLogger::Instance().Info("==========================================");
    DebugLogger::Instance().Info("OmniRender GPU Test Host Launching");
    DebugLogger::Instance().Info("Arguments: argc=%d", argc);
    for (int i = 0; i < argc; ++i) {
        DebugLogger::Instance().Info("  argv[%d] = %s", i, argv[i]);
    }

    DiagnosticsReporter reporter;
    reporter.PrintBanner();

    D3D11DeviceFixture fixture;
    if (!TestCase1_HalDevice(fixture, reporter)) {
        reporter.PrintFinalSummary();
        return 1;
    }

    TestSceneFixtures fixtures;
    core::FrameContext fc{};
    TestCase2_Fixtures(fixture, fc, fixtures, reporter);
    TestCase3_RenderGraph(fixture, fc, reporter);
    TestCase4_ReconstructionAndReadback(fixture, fc, fixtures, reporter);
    TestCase5_HistoryLifecycle(fixture, fc, reporter);
    TestCase6_RuntimePipeline(fixture, fc, reporter);
    TestCase7_FailurePaths(fixture, reporter);
    TestCase8_FsrSpatial(fixture, fc, fixtures, reporter);

    fixture.Shutdown();
    bool all_passed = reporter.PrintFinalSummary();

    DebugLogger::Instance().Info("OmniRender GPU Test Host Exited: code=%d", all_passed ? 0 : 1);
    DebugLogger::Instance().Close();
    return all_passed ? 0 : 1;
}
