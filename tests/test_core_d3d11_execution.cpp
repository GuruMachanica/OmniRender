// filepath: tests/test_core_d3d11_execution.cpp
// GPU integration test validating platform-neutral core with real D3D11 execution.

#include <d3d11.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include "../core/frame/FrameContext.h"
#include "../core/graph/RenderGraph.h"
#include "../core/temporal/HistoryManager.h"
#include "../graphics/d3d11/D3D11GraphicsDevice.h"
#include "../backends/reconstruction/dlss/DlssReconstructionBackend.h"
#include "../runtime/Pipeline.h"

using namespace omnirender;
using namespace omnirender::core;
using namespace omnirender::graphics::d3d11;
using namespace omnirender::backends::dlss;
using namespace omnirender::runtime;

static ID3D11Device* CreateD3D11Device(ID3D11DeviceContext** out_ctx) {
    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };
    ID3D11Device* dev = nullptr;
    UINT flags = 0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, feature_levels, 1, D3D11_SDK_VERSION,
                                   &dev, nullptr, out_ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               flags, feature_levels, 1, D3D11_SDK_VERSION,
                               &dev, nullptr, out_ctx);
    }
    return SUCCEEDED(hr) ? dev : nullptr;
}

// 1. Validate D3D11 Backend: D3D11 -> IGraphicsDevice -> GpuTexture -> FrameContext
static void TestD3D11DeviceAndTextures(D3D11GraphicsDevice& dev, FrameContext& fc) {
    TextureDesc color_desc{
        1920, 1080, 1, TextureFormat::R8G8B8A8_UNORM,
        TextureUsage::ShaderResource | TextureUsage::RenderTarget | TextureUsage::TransferDst,
        "TestInputColor"
    };
    TextureDesc depth_desc{
        1920, 1080, 1, TextureFormat::R32_FLOAT,
        TextureUsage::ShaderResource | TextureUsage::TransferDst,
        "TestInputDepth"
    };
    TextureDesc motion_desc{
        1920, 1080, 1, TextureFormat::R16G16_FLOAT,
        TextureUsage::ShaderResource | TextureUsage::TransferDst,
        "TestInputMotion"
    };

    auto color_tex = dev.CreateTexture(color_desc);
    auto depth_tex = dev.CreateTexture(depth_desc);
    auto motion_tex = dev.CreateTexture(motion_desc);
    assert(color_tex && depth_tex && motion_tex);

    fc.color = GpuTexture(color_tex);
    fc.depth = GpuTexture(depth_tex);
    fc.motion = GpuTexture(motion_tex);

    assert(fc.color.IsValid() && fc.color.GetWidth() == 1920 && fc.color.GetHeight() == 1080);
    assert(fc.depth.IsValid() && fc.depth.GetWidth() == 1920 && fc.depth.GetHeight() == 1080);
    assert(fc.motion.IsValid() && fc.motion.GetWidth() == 1920 && fc.motion.GetHeight() == 1080);

    fc.validity.color_valid = fc.validity.depth_valid = fc.validity.motion_valid = true;
    fc.input_resolution = { 1920, 1080 };
    fc.output_resolution = { 2560, 1440 };

    printf("[PASS] TestD3D11DeviceAndTextures\n");
}

// 2. Validate DLSS Backend Execution & Real GPU Readback (1080p -> 1440p)
static void TestDlssExecutionAndReadback(D3D11GraphicsDevice& dev, FrameContext& fc,
                                        ID3D11Device* d3d_dev, ID3D11DeviceContext* d3d_ctx) {
    auto cmd_ctx = dev.GetImmediateContext();
    assert(cmd_ctx != nullptr);

    auto dlss_backend = std::make_shared<DlssReconstructionBackend>();
    bool dlss_init_ok = dlss_backend->Initialize(dev, fc.input_resolution, fc.output_resolution);
    if (!dlss_init_ok || !dlss_backend->IsNvidiaHardware()) {
        // NGX requires an NVIDIA GPU + the Streamline SDK. CI runners use
        // WARP or other vendors, so this stage is skipped there; it only runs
        // on real NVIDIA hardware (matching the gpu_test_host policy).
        dlss_backend->Shutdown();
        printf("[SKIP] TestDlssExecutionAndReadback (non-NVIDIA or no NGX SDK)\n");
        return;
    }
    assert(dlss_backend->IsRuntimeAvailable());

    // Execute reconstruction pass
    auto res = dlss_backend->Execute(fc, *cmd_ctx);
    assert(res.success);
    assert(fc.color.IsValid());
    assert(fc.color.GetWidth() == 2560 && fc.color.GetHeight() == 1440);

    // GPU Readback via Staging Texture
    D3D11_TEXTURE2D_DESC staging_desc{
        2560, 1440, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 },
        D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ
    };
    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = d3d_dev->CreateTexture2D(&staging_desc, nullptr, &staging);
    assert(SUCCEEDED(hr) && staging != nullptr);

    auto* out_native = static_cast<ID3D11Resource*>(fc.color.Get()->GetNativeResource());
    assert(out_native != nullptr);
    d3d_ctx->CopyResource(staging, out_native);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = d3d_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    assert(SUCCEEDED(hr) && mapped.pData != nullptr);
    assert(mapped.RowPitch >= 2560 * 4);

    // Verify non-zero reconstructed image content read from GPU memory
    const auto* pixels = static_cast<const uint8_t*>(mapped.pData);
    bool has_nonzero_content = false;
    for (size_t i = 0; i < 2560 * 10; ++i) {
        if (pixels[i] > 0) {
            has_nonzero_content = true;
            break;
        }
    }
    assert(has_nonzero_content);

    d3d_ctx->Unmap(staging, 0);
    staging->Release();
    dlss_backend->Shutdown();

    printf("[PASS] TestDlssExecutionAndReadback (GPU readback verified)\n");
}

// 3. Validate RenderGraph Pipeline Execution & Resource Dependencies
static void TestRenderGraphExecution(D3D11GraphicsDevice& dev, FrameContext& fc) {
    RenderGraph graph;
    int execution_order = 0;
    int capture_step = 0, motion_step = 0, dlss_step = 0, output_step = 0;

    graph.AddPass(PassType::Capture, "Capture",
                  FrameValidity{ false, false, false, false, false, false, false, false },
                  ResourceAccess::None, ResourceAccess::WriteColor | ResourceAccess::WriteDepth,
                  [&](FrameContext&, graphics::ICommandContext&) {
                      capture_step = ++execution_order;
                      return true;
                  });

    graph.AddPass(PassType::MotionReproject, "Motion",
                  FrameValidity{ false, true, false, false, false, false, false, false },
                  ResourceAccess::ReadDepth, ResourceAccess::WriteMotion,
                  [&](FrameContext&, graphics::ICommandContext&) {
                      motion_step = ++execution_order;
                      return true;
                  });

    graph.AddPass(PassType::Upscale, "DLSS",
                  FrameValidity{ true, true, true, false, false, false, false, false },
                  ResourceAccess::ReadColor | ResourceAccess::ReadDepth | ResourceAccess::ReadMotion,
                  ResourceAccess::WriteColor,
                  [&](FrameContext&, graphics::ICommandContext&) {
                      dlss_step = ++execution_order;
                      return true;
                  });

    graph.AddPass(PassType::Present, "Output",
                  FrameValidity{ true, false, false, false, false, false, false, false },
                  ResourceAccess::ReadColor, ResourceAccess::None,
                  [&](FrameContext&, graphics::ICommandContext&) {
                      output_step = ++execution_order;
                      return true;
                  });

    auto cmd = dev.GetImmediateContext();
    assert(graph.Execute(fc, *cmd));

    assert(capture_step == 1);
    assert(motion_step == 2);
    assert(dlss_step == 3);
    assert(output_step == 4);

    printf("[PASS] TestRenderGraphExecution (4-pass dependency order confirmed)\n");
}

// 4. Validate History Rotation & Invalidation Lifecycle
static void TestHistoryRotationAndInvalidation(D3D11GraphicsDevice& dev, FrameContext& fc) {
    HistoryManager hist;
    assert(hist.Initialize(dev, 1920, 1080, TextureFormat::R8G8B8A8_UNORM, TextureFormat::R32_FLOAT));

    auto cmd = dev.GetImmediateContext();

    // Frame 0: Empty
    assert(hist.GetState() == HistoryState::Empty);
    assert(hist.GetValidFrameCount() == 0);
    assert(!hist.GetCurrentHistoryTexture().IsValid());

    // Frame 1: Warming up
    hist.CommitFrame(*cmd, fc.color, fc.depth);
    assert(hist.GetState() == HistoryState::WarmingUp);
    assert(hist.GetValidFrameCount() == 1);
    assert(hist.GetCurrentHistoryTexture().IsValid());

    // Frame 2: Valid
    hist.CommitFrame(*cmd, fc.color, fc.depth);
    assert(hist.GetState() == HistoryState::Valid);
    assert(hist.GetValidFrameCount() == 2);

    // Frame 3: Valid
    hist.CommitFrame(*cmd, fc.color, fc.depth);
    assert(hist.GetState() == HistoryState::Valid);
    assert(hist.GetValidFrameCount() == 3);

    // Resolution change -> Invalidation
    hist.Invalidate(InvalidationReason::ResolutionChanged);
    assert(hist.GetState() == HistoryState::Invalidated);
    assert(hist.GetValidFrameCount() == 0);
    assert(!hist.GetCurrentHistoryTexture().IsValid());

    // Frame 4: Recovery / Rebuilding
    hist.CommitFrame(*cmd, fc.color, fc.depth);
    assert(hist.GetState() == HistoryState::Rebuilding);
    assert(hist.GetValidFrameCount() == 1);

    // Frame 5: Re-validated
    hist.CommitFrame(*cmd, fc.color, fc.depth);
    assert(hist.GetState() == HistoryState::Valid);
    assert(hist.GetValidFrameCount() == 2);

    // Camera cut -> Invalidation
    hist.Invalidate(InvalidationReason::CameraCut);
    assert(hist.GetState() == HistoryState::Invalidated);

    hist.Shutdown();
    printf("[PASS] TestHistoryRotationAndInvalidation (Lifecycle fully verified)\n");
}

// 5. Validate Runtime Pipeline End-to-End
static void TestRuntimePipelineExecution(D3D11GraphicsDevice& dev, FrameContext& fc) {
    Pipeline pipeline;
    assert(pipeline.Initialize(dev, { 1920, 1080 }, { 2560, 1440 },
                              TextureFormat::R8G8B8A8_UNORM, TextureFormat::R32_FLOAT));

    auto dlss_backend = std::make_shared<DlssReconstructionBackend>();
    pipeline.SetReconstructionBackend(dlss_backend);

    auto cmd = dev.GetImmediateContext();
    assert(pipeline.ExecuteFrame(fc, *cmd));
    assert(pipeline.GetHistoryManager().GetState() == HistoryState::WarmingUp);

    assert(pipeline.ExecuteFrame(fc, *cmd));
    assert(pipeline.GetHistoryManager().GetState() == HistoryState::Valid);

    printf("[PASS] TestRuntimePipelineExecution\n");
}

int main() {
    printf("Starting test_core_d3d11_execution integration harness...\n");

    ID3D11DeviceContext* d3d_ctx = nullptr;
    ID3D11Device* d3d_dev = CreateD3D11Device(&d3d_ctx);
    if (!d3d_dev) {
        printf("D3D11 hardware or WARP unavailable; skipping test.\n");
        return 0;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> dev_ptr(d3d_dev);
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_ptr(d3d_ctx);
    D3D11GraphicsDevice device(dev_ptr, ctx_ptr);

    FrameContext fc{};
    TestD3D11DeviceAndTextures(device, fc);
    TestDlssExecutionAndReadback(device, fc, d3d_dev, d3d_ctx);
    TestRenderGraphExecution(device, fc);
    TestHistoryRotationAndInvalidation(device, fc);
    TestRuntimePipelineExecution(device, fc);

    d3d_ctx->Release();
    d3d_dev->Release();

    printf("All Core D3D11 GPU integration tests passed successfully!\n");
    return 0;
}
