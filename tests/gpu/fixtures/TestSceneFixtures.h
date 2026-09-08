// filepath: tests/gpu/fixtures/TestSceneFixtures.h
#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <d3d11.h>
#include "../../../core/frame/FrameContext.h"
#include "../../../graphics/d3d11/D3D11GraphicsDevice.h"
#include "../d3d11/D3D11DeviceFixture.h"

namespace omnirender::test::gpu {

struct SyntheticSceneData {
    uint32_t             width = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> color_rgba8;
    std::vector<float>   depth_r32;
    std::vector<float>   motion_rg32f;
    std::vector<uint8_t> reactive_rgba8;
};

class TestSceneFixtures {
public:
    TestSceneFixtures();
    ~TestSceneFixtures() = default;

    SyntheticSceneData GenerateSyntheticData(uint32_t width, uint32_t height, uint64_t frame_index = 0);

    bool PopulateGpuFrameContext(D3D11DeviceFixture& fixture,
                                 core::FrameContext& out_fc,
                                 uint32_t input_width = 1920,
                                 uint32_t input_height = 1080,
                                 uint32_t output_width = 2560,
                                 uint32_t output_height = 1440,
                                 uint64_t frame_index = 0);

    std::vector<uint8_t> GenerateReferenceImage(uint32_t width, uint32_t height, uint64_t frame_index = 0);

private:
    bool UploadTexture2D(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                         core::GpuTexture& tex, const void* data, uint32_t row_pitch);
};

}  // namespace omnirender::test::gpu
