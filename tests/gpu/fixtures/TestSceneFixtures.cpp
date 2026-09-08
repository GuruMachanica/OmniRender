// filepath: tests/gpu/fixtures/TestSceneFixtures.cpp
#include "TestSceneFixtures.h"
#include <cmath>
#include <cstring>
#include "../validation/DebugLogger.h"

namespace omnirender::test::gpu {

TestSceneFixtures::TestSceneFixtures() = default;

SyntheticSceneData TestSceneFixtures::GenerateSyntheticData(uint32_t width, uint32_t height, uint64_t frame_index) {
    SyntheticSceneData data;
    data.width = width;
    data.height = height;

    const size_t pixel_count = static_cast<size_t>(width) * height;
    data.color_rgba8.resize(pixel_count * 4);
    data.depth_r32.resize(pixel_count);
    data.motion_rg32f.resize(pixel_count * 2);
    data.reactive_rgba8.resize(pixel_count * 4, 0);

    const uint8_t palette[8][3] = {
        { 255, 255, 255 }, // White
        { 255, 255,   0 }, // Yellow
        {   0, 255, 255 }, // Cyan
        {   0, 255,   0 }, // Green
        { 255,   0, 255 }, // Magenta
        { 255,   0,   0 }, // Red
        {   0,   0, 255 }, // Blue
        {  32,  32,  32 }  // Dark Grey
    };

    const float phase = static_cast<float>(frame_index % 60) * (3.14159265f / 30.0f);

    for (uint32_t y = 0; y < height; ++y) {
        const float ny = static_cast<float>(y) / static_cast<float>(height);
        for (uint32_t x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            const float nx = static_cast<float>(x) / static_cast<float>(width);

            // Color: 8 vertical color bars with subtle gradient
            const int bar_idx = static_cast<int>(nx * 8.0f) % 8;
            const float shade = 0.7f + 0.3f * std::sin(nx * 10.0f + phase);
            data.color_rgba8[idx * 4 + 0] = static_cast<uint8_t>(palette[bar_idx][0] * shade);
            data.color_rgba8[idx * 4 + 1] = static_cast<uint8_t>(palette[bar_idx][1] * shade);
            data.color_rgba8[idx * 4 + 2] = static_cast<uint8_t>(palette[bar_idx][2] * shade);
            data.color_rgba8[idx * 4 + 3] = 255;

            // Depth: Linear ramp [0.1, 1.0] with center sphere depth
            const float cx = nx - 0.5f;
            const float cy = ny - 0.5f;
            const float dist_sq = cx * cx + cy * cy;
            if (dist_sq < 0.04f) {
                data.depth_r32[idx] = 0.25f + 0.1f * std::sqrt(dist_sq);
            } else {
                data.depth_r32[idx] = 0.1f + 0.85f * ny;
            }

            // Motion: Non-zero subpixel optical flow vectors (dx, dy)
            data.motion_rg32f[idx * 2 + 0] = 0.5f * std::sin(nx * 6.28f + phase);
            data.motion_rg32f[idx * 2 + 1] = 0.3f * std::cos(ny * 6.28f + phase);

            // Reactive Mask: Sparse UI box at top-left
            if (x >= 20 && x <= 160 && y >= 20 && y <= 80) {
                data.reactive_rgba8[idx * 4 + 0] = 255;
                data.reactive_rgba8[idx * 4 + 1] = 255;
                data.reactive_rgba8[idx * 4 + 2] = 255;
                data.reactive_rgba8[idx * 4 + 3] = 255;
            }
        }
    }

    DebugLogger::Instance().Debug("Generated synthetic scene data (%ux%u, frame %llu)",
                                  width, height, frame_index);
    return data;
}

std::vector<uint8_t> TestSceneFixtures::GenerateReferenceImage(uint32_t width, uint32_t height, uint64_t frame_index) {
    auto data = GenerateSyntheticData(width, height, frame_index);
    return std::move(data.color_rgba8);
}

bool TestSceneFixtures::UploadTexture2D(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                       core::GpuTexture& tex, const void* data, uint32_t row_pitch) {
    if (!dev || !ctx || !tex.IsValid() || !data) return false;
    auto* native_res = static_cast<ID3D11Resource*>(tex.Get()->GetNativeResource());
    if (!native_res) return false;

    ctx->UpdateSubresource(native_res, 0, nullptr, data, row_pitch, 0);
    return true;
}

bool TestSceneFixtures::PopulateGpuFrameContext(D3D11DeviceFixture& fixture,
                                              core::FrameContext& out_fc,
                                              uint32_t input_width,
                                              uint32_t input_height,
                                              uint32_t output_width,
                                              uint32_t output_height,
                                              uint64_t frame_index) {
    if (!fixture.IsInitialized()) {
        DebugLogger::Instance().Error("PopulateGpuFrameContext failed: fixture not initialized");
        return false;
    }

    auto dev = fixture.GetDevice();
    auto ctx = fixture.GetContext();
    auto gdev = fixture.GetGraphicsDevice();

    auto data = GenerateSyntheticData(input_width, input_height, frame_index);

    core::TextureDesc color_desc{
        input_width, input_height, 1, core::TextureFormat::R8G8B8A8_UNORM,
        core::TextureUsage::ShaderResource | core::TextureUsage::RenderTarget | core::TextureUsage::TransferDst,
        "FixtureInputColor"
    };
    core::TextureDesc depth_desc{
        input_width, input_height, 1, core::TextureFormat::R32_FLOAT,
        core::TextureUsage::ShaderResource | core::TextureUsage::TransferDst,
        "FixtureInputDepth"
    };
    core::TextureDesc motion_desc{
        input_width, input_height, 1, core::TextureFormat::R32G32_FLOAT,
        core::TextureUsage::ShaderResource | core::TextureUsage::TransferDst,
        "FixtureInputMotion"
    };
    core::TextureDesc reactive_desc{
        input_width, input_height, 1, core::TextureFormat::R8G8B8A8_UNORM,
        core::TextureUsage::ShaderResource | core::TextureUsage::TransferDst,
        "FixtureInputReactive"
    };

    auto color_tex    = gdev->CreateTexture(color_desc);
    auto depth_tex    = gdev->CreateTexture(depth_desc);
    auto motion_tex   = gdev->CreateTexture(motion_desc);
    auto reactive_tex = gdev->CreateTexture(reactive_desc);

    if (!color_tex || !depth_tex || !motion_tex || !reactive_tex) {
        DebugLogger::Instance().Error("Failed to create fixture textures on GPU device");
        return false;
    }

    out_fc.color    = core::GpuTexture(color_tex);
    out_fc.depth    = core::GpuTexture(depth_tex);
    out_fc.motion   = core::GpuTexture(motion_tex);
    out_fc.reactive = core::GpuTexture(reactive_tex);

    UploadTexture2D(dev, ctx, out_fc.color,    data.color_rgba8.data(),    input_width * 4);
    UploadTexture2D(dev, ctx, out_fc.depth,    data.depth_r32.data(),      input_width * sizeof(float));
    UploadTexture2D(dev, ctx, out_fc.motion,   data.motion_rg32f.data(),   input_width * sizeof(float) * 2);
    UploadTexture2D(dev, ctx, out_fc.reactive, data.reactive_rgba8.data(), input_width * 4);

    out_fc.input_resolution  = { input_width, input_height };
    out_fc.output_resolution = { output_width, output_height };
    out_fc.graphics_api      = core::GraphicsApi::D3D11;

    out_fc.validity.color_valid    = true;
    out_fc.validity.depth_valid    = true;
    out_fc.validity.motion_valid   = true;
    out_fc.validity.reactive_valid = true;

    out_fc.timing.frame_index    = frame_index;
    out_fc.timing.delta_time_ms  = 16.6667f;
    out_fc.timing.fps            = 60.0f;

    out_fc.jitter.jitter_x = 0.25f * std::sin(static_cast<float>(frame_index));
    out_fc.jitter.jitter_y = 0.25f * std::cos(static_cast<float>(frame_index));

    DebugLogger::Instance().Info("Successfully populated GPU FrameContext: in=%ux%u, out=%ux%u, frame=%llu",
                                 input_width, input_height, output_width, output_height, frame_index);
    return true;
}

}  // namespace omnirender::test::gpu
