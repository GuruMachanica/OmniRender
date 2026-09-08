// filepath: tests/test_dlss_gpu.cpp
// GPU integration test harness for NVIDIA DLSS Super Resolution execution.

#include <d3d11.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include "../modules/common/frame_context.h"
#include "../modules/common/render_graph.h"
#include "../modules/daemon/upscalers/dlss_adapter.h"

using namespace omnirender;
using namespace omnirender::daemon::upscaler;

static ID3D11Device* CreateD3D11TestDevice(ID3D11DeviceContext** out_ctx) {
    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };
    ID3D11Device* device = nullptr;
    UINT flags = 0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, feature_levels, 1, D3D11_SDK_VERSION,
                                   &device, nullptr, out_ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               flags, feature_levels, 1, D3D11_SDK_VERSION,
                               &device, nullptr, out_ctx);
    }
    return SUCCEEDED(hr) ? device : nullptr;
}

static ID3D11Texture2D* CreateTexture(ID3D11Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT fmt, UINT bind) {
    D3D11_TEXTURE2D_DESC desc{ w, h, 1, 1, fmt, { 1, 0 }, D3D11_USAGE_DEFAULT, bind };
    ID3D11Texture2D* tex = nullptr;
    return SUCCEEDED(dev->CreateTexture2D(&desc, nullptr, &tex)) ? tex : nullptr;
}

// Case 1: NVIDIA + DLSS real evaluation & GPU readback
static void TestCase1_RealDlssExecution(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    DlssAdapter adapter;
    assert(adapter.InitializeWithDevice(dev, 2560, 1440));

    ID3D11Texture2D* color = CreateTexture(dev, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    ID3D11Texture2D* depth = CreateTexture(dev, 1920, 1080, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* motion = CreateTexture(dev, 1920, 1080, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE);
    assert(color && depth && motion);

    FrameContext fc{};
    fc.color.resource = color; fc.depth.resource = depth; fc.motion.resource = motion;
    fc.color.width = 1920; fc.color.height = 1080;
    fc.resolution.input_width = 1920; fc.resolution.input_height = 1080;
    fc.resolution.output_width = 2560; fc.resolution.output_height = 1440;
    fc.validity.color_valid = fc.validity.depth_valid = fc.validity.motion_valid = true;
    fc.jitter.offset_x = 0.125f; fc.jitter.offset_y = -0.25f;

    bool is_nvidia = adapter.DetectNvidiaHardware(dev);
    if (is_nvidia && adapter.IsRuntimeAvailable()) {
        assert(adapter.Execute(fc));
        assert(adapter.GetState() == DlssState::ExecutionSucceeded);
        assert(fc.color.source == DataSource::Reconstructed);
        assert(fc.color.width == 2560 && fc.color.height == 1440);

        // GPU readback test
        D3D11_TEXTURE2D_DESC s_desc{ 2560, 1440, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1,0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ };
        ID3D11Texture2D* staging = nullptr;
        assert(SUCCEEDED(dev->CreateTexture2D(&s_desc, nullptr, &staging)));
        ctx->CopyResource(staging, static_cast<ID3D11Resource*>(fc.color.resource));
        D3D11_MAPPED_SUBRESOURCE mapped{};
        assert(SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)));
        assert(mapped.pData != nullptr);
        ctx->Unmap(staging, 0);
        staging->Release();
    } else {
        // Deterministic fallback path on non-NVIDIA / CI runners
        assert(!adapter.Execute(fc));
        assert(fc.color.source != DataSource::Reconstructed);
    }

    color->Release(); depth->Release(); motion->Release();
    adapter.Shutdown();
    printf("[PASS] TestCase1_RealDlssExecution\n");
}

// Case 2: Invalid Depth -> Rejected -> Deterministic fallback
static void TestCase2_InvalidDepth(ID3D11Device* dev) {
    DlssAdapter adapter;
    assert(adapter.InitializeWithDevice(dev, 2560, 1440));
    ID3D11Texture2D* color = CreateTexture(dev, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* motion = CreateTexture(dev, 1920, 1080, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE);

    FrameContext fc{};
    fc.color.resource = color; fc.motion.resource = motion;
    fc.validity.color_valid = fc.validity.motion_valid = true;
    fc.validity.depth_valid = false; // Missing depth

    assert(!adapter.Execute(fc));
    assert(adapter.GetState() == DlssState::InputInvalid);
    assert(fc.color.source != DataSource::Reconstructed);

    color->Release(); motion->Release();
    adapter.Shutdown();
    printf("[PASS] TestCase2_InvalidDepth\n");
}

// Case 3: Resolution Change (1920x1080 -> 2560x1440 -> 3840x2160)
static void TestCase3_ResolutionChange(ID3D11Device* dev) {
    DlssAdapter adapter;
    assert(adapter.InitializeWithDevice(dev, 3840, 2160));
    ID3D11Texture2D* color = CreateTexture(dev, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* depth = CreateTexture(dev, 1920, 1080, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* motion = CreateTexture(dev, 1920, 1080, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE);

    FrameContext fc{};
    fc.color.resource = color; fc.depth.resource = depth; fc.motion.resource = motion;
    fc.validity.color_valid = fc.validity.depth_valid = fc.validity.motion_valid = true;

    // Res 1: 2560x1440
    fc.resolution.input_width = 1920; fc.resolution.input_height = 1080;
    fc.resolution.output_width = 2560; fc.resolution.output_height = 1440;
    DlssEvaluationParams p1{};
    assert(adapter.BindFrameContextParameters(fc, p1));
    assert(p1.target_width == 2560 && p1.target_height == 1440);

    // Res 2: 3840x2160
    fc.resolution.input_width = 2560; fc.resolution.input_height = 1440;
    fc.resolution.output_width = 3840; fc.resolution.output_height = 2160;
    DlssEvaluationParams p2{};
    assert(adapter.BindFrameContextParameters(fc, p2));
    assert(p2.target_width == 3840 && p2.target_height == 2160);

    ID3D11Texture2D* out_tex = adapter.GetOutputTexture();
    if (out_tex) {
        D3D11_TEXTURE2D_DESC desc{};
        out_tex->GetDesc(&desc);
        assert(desc.Width == 3840 && desc.Height == 2160);
    }

    color->Release(); depth->Release(); motion->Release();
    adapter.Shutdown();
    printf("[PASS] TestCase3_ResolutionChange\n");
}

// Case 4: History reset (history_valid false -> Reset 1, then true -> Reset 0)
static void TestCase4_HistoryReset(ID3D11Device* dev) {
    DlssAdapter adapter;
    assert(adapter.InitializeWithDevice(dev, 1920, 1080));
    ID3D11Texture2D* color = CreateTexture(dev, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* depth = CreateTexture(dev, 1280, 720, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* motion = CreateTexture(dev, 1280, 720, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE);

    FrameContext fc{};
    fc.color.resource = color; fc.depth.resource = depth; fc.motion.resource = motion;
    fc.color.width = 1280; fc.color.height = 720;
    fc.validity.color_valid = fc.validity.depth_valid = fc.validity.motion_valid = true;

    // Frame 1: History invalid -> Reset expected
    fc.validity.history_valid = false;
    DlssEvaluationParams p1{};
    assert(adapter.BindFrameContextParameters(fc, p1));
    assert(p1.reset_history == true);

    // Frame 2: History valid -> No reset
    fc.validity.history_valid = true;
    DlssEvaluationParams p2{};
    assert(adapter.BindFrameContextParameters(fc, p2));
    assert(p2.reset_history == false);

    color->Release(); depth->Release(); motion->Release();
    adapter.Shutdown();
    printf("[PASS] TestCase4_HistoryReset\n");
}

// Case 5: Device loss and restoration lifecycle
static void TestCase5_DeviceLoss(ID3D11Device* dev) {
    DlssAdapter adapter;
    assert(adapter.InitializeWithDevice(dev, 1920, 1080));
    ID3D11Texture2D* color = CreateTexture(dev, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* depth = CreateTexture(dev, 1280, 720, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* motion = CreateTexture(dev, 1280, 720, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE);

    FrameContext fc{};
    fc.color.resource = color; fc.depth.resource = depth; fc.motion.resource = motion;
    fc.color.width = 1280; fc.color.height = 720;
    fc.validity.color_valid = fc.validity.depth_valid = fc.validity.motion_valid = true;

    DlssEvaluationParams p{};
    assert(adapter.BindFrameContextParameters(fc, p));

    // Simulate device loss
    adapter.OnDeviceLost();
    assert(adapter.GetOutputTexture() == nullptr);

    // Simulate recreating device after device loss
    ID3D11DeviceContext* new_ctx = nullptr;
    ID3D11Device* new_dev = CreateD3D11TestDevice(&new_ctx);
    assert(new_dev != nullptr);

    adapter.OnDeviceRestored(new_dev);
    // Re-verify parameter binding on new device
    ID3D11Texture2D* new_color = CreateTexture(new_dev, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* new_depth = CreateTexture(new_dev, 1280, 720, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE);
    ID3D11Texture2D* new_motion = CreateTexture(new_dev, 1280, 720, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE);

    fc.color.resource = new_color; fc.depth.resource = new_depth; fc.motion.resource = new_motion;
    DlssEvaluationParams p_restored{};
    assert(adapter.BindFrameContextParameters(fc, p_restored));
    assert(p_restored.in_color == new_color);

    new_color->Release(); new_depth->Release(); new_motion->Release();
    color->Release(); depth->Release(); motion->Release();
    new_ctx->Release(); new_dev->Release();
    adapter.Shutdown();
    printf("[PASS] TestCase5_DeviceLoss\n");
}

int main() {
    printf("Running test_dlss_gpu integration harness...\n");
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Device* dev = CreateD3D11TestDevice(&ctx);
    if (!dev) {
        printf("D3D11 hardware/WARP device unavailable. Skipping GPU harness.\n");
        return 0;
    }

    TestCase1_RealDlssExecution(dev, ctx);
    TestCase2_InvalidDepth(dev);
    TestCase3_ResolutionChange(dev);
    TestCase4_HistoryReset(dev);
    TestCase5_DeviceLoss(dev);

    ctx->Release();
    dev->Release();
    printf("All DLSS GPU integration tests passed successfully!\n");
    return 0;
}
