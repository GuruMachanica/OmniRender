// filepath: tests/gpu/readback/GpuReadback.cpp
#include "GpuReadback.h"
#include <cstring>
#include "../validation/DebugLogger.h"
#include "../../../graphics/abstraction/IGraphicsTexture.h"

namespace omnirender::test::gpu {

GpuReadback::GpuReadback() = default;

ReadbackResult GpuReadback::ReadbackTextureRgba8(ID3D11Device* dev,
                                                ID3D11DeviceContext* ctx,
                                                const core::GpuTexture& tex) {
    return ReadbackTextureInternal(dev, ctx, tex, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
}

ReadbackResult GpuReadback::ReadbackTextureFloat(ID3D11Device* dev,
                                                ID3D11DeviceContext* ctx,
                                                const core::GpuTexture& tex) {
    return ReadbackTextureInternal(dev, ctx, tex, DXGI_FORMAT_R32_FLOAT, sizeof(float));
}

ReadbackResult GpuReadback::ReadbackTextureInternal(ID3D11Device* dev,
                                                   ID3D11DeviceContext* ctx,
                                                   const core::GpuTexture& tex,
                                                   DXGI_FORMAT format,
                                                   size_t bytes_per_pixel) {
    ReadbackResult result{};
    if (!dev || !ctx || !tex.IsValid()) {
        DebugLogger::Instance().Error("GpuReadback failed: invalid device, context, or texture");
        return result;
    }

    auto* native_res = static_cast<ID3D11Resource*>(tex.Get()->GetNativeResource());
    if (!native_res) {
        DebugLogger::Instance().Error("GpuReadback failed: native resource is null");
        return result;
    }

    const uint32_t width = tex.GetWidth();
    const uint32_t height = tex.GetHeight();

    D3D11_TEXTURE2D_DESC staging_desc{};
    staging_desc.Width              = width;
    staging_desc.Height             = height;
    staging_desc.MipLevels          = 1;
    staging_desc.ArraySize          = 1;
    staging_desc.Format             = format;
    staging_desc.SampleDesc.Count   = 1;
    staging_desc.SampleDesc.Quality = 0;
    staging_desc.Usage              = D3D11_USAGE_STAGING;
    staging_desc.BindFlags          = 0;
    staging_desc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags          = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = dev->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf());
    if (FAILED(hr) || !staging) {
        DebugLogger::Instance().Error("GpuReadback failed: could not create staging texture (HRESULT 0x%08X)", hr);
        return result;
    }

    ctx->CopyResource(staging.Get(), native_res);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr) || !mapped.pData) {
        DebugLogger::Instance().Error("GpuReadback failed: could not map staging texture (HRESULT 0x%08X)", hr);
        return result;
    }

    result.width = width;
    result.height = height;
    result.row_pitch = mapped.RowPitch;
    const size_t tight_row_size = static_cast<size_t>(width) * bytes_per_pixel;
    result.data.resize(tight_row_size * height);

    for (uint32_t y = 0; y < height; ++y) {
        const auto* src_row = static_cast<const uint8_t*>(mapped.pData) + (static_cast<size_t>(y) * mapped.RowPitch);
        auto* dst_row = result.data.data() + (static_cast<size_t>(y) * tight_row_size);
        std::memcpy(dst_row, src_row, tight_row_size);
    }

    ctx->Unmap(staging.Get(), 0);
    result.success = true;

    DebugLogger::Instance().Debug("GpuReadback completed: %ux%u (%zu bytes read, row_pitch=%u)",
                                  width, height, result.data.size(), mapped.RowPitch);
    return result;
}

}  // namespace omnirender::test::gpu
