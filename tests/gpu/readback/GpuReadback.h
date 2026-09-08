// filepath: tests/gpu/readback/GpuReadback.h
#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include "../../../core/resources/GpuTexture.h"

namespace omnirender::test::gpu {

struct ReadbackResult {
    bool                 success = false;
    uint32_t             width = 0;
    uint32_t             height = 0;
    uint32_t             row_pitch = 0;
    std::vector<uint8_t> data;
};

class GpuReadback {
public:
    GpuReadback();
    ~GpuReadback() = default;

    static ReadbackResult ReadbackTextureRgba8(ID3D11Device* dev,
                                              ID3D11DeviceContext* ctx,
                                              const core::GpuTexture& tex);

    static ReadbackResult ReadbackTextureFloat(ID3D11Device* dev,
                                              ID3D11DeviceContext* ctx,
                                              const core::GpuTexture& tex);

private:
    static ReadbackResult ReadbackTextureInternal(ID3D11Device* dev,
                                                  ID3D11DeviceContext* ctx,
                                                  const core::GpuTexture& tex,
                                                  DXGI_FORMAT format,
                                                  size_t bytes_per_pixel);
};

}  // namespace omnirender::test::gpu
