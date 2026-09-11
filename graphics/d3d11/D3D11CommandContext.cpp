// filepath: graphics/d3d11/D3D11CommandContext.cpp
#include "D3D11CommandContext.h"
#include <cstring>
#include "D3D11GraphicsTexture.h"
#include "D3D11GraphicsBuffer.h"

namespace omnirender::graphics::d3d11 {

D3D11CommandContext::D3D11CommandContext(Microsoft::WRL::ComPtr<ID3D11DeviceContext> context)
    : context_(std::move(context)) {}

bool D3D11CommandContext::BeginFrame() {
    return context_ != nullptr;
}

bool D3D11CommandContext::EndFrame() {
    return context_ != nullptr;
}

void D3D11CommandContext::SetComputeShader(void* native_shader) {
    if (context_) {
        context_->CSSetShader(static_cast<ID3D11ComputeShader*>(native_shader), nullptr, 0);
    }
}

void D3D11CommandContext::SetConstantBuffers(uint32_t start_slot, uint32_t count, IGraphicsBuffer* const* buffers) {
    if (!context_ || count == 0) return;
    ID3D11Buffer* d3d_buffers[8] = {};
    uint32_t clamped_count = (count > 8) ? 8 : count;
    for (uint32_t i = 0; i < clamped_count; ++i) {
        d3d_buffers[i] = buffers[i] ? static_cast<ID3D11Buffer*>(buffers[i]->GetNativeResource()) : nullptr;
    }
    context_->CSSetConstantBuffers(start_slot, clamped_count, d3d_buffers);
}

void D3D11CommandContext::SetShaderResources(uint32_t start_slot, uint32_t count, IGraphicsTexture* const* textures) {
    if (!context_ || count == 0) return;
    ID3D11ShaderResourceView* srvs[8] = {};
    uint32_t clamped_count = (count > 8) ? 8 : count;
    for (uint32_t i = 0; i < clamped_count; ++i) {
        srvs[i] = textures[i] ? static_cast<ID3D11ShaderResourceView*>(textures[i]->GetNativeSrv()) : nullptr;
    }
    context_->CSSetShaderResources(start_slot, clamped_count, srvs);
}

void D3D11CommandContext::SetUnorderedAccessViews(uint32_t start_slot, uint32_t count, IGraphicsTexture* const* textures) {
    if (!context_ || count == 0) return;
    ID3D11UnorderedAccessView* uavs[8] = {};
    uint32_t clamped_count = (count > 8) ? 8 : count;
    for (uint32_t i = 0; i < clamped_count; ++i) {
        uavs[i] = textures[i] ? static_cast<ID3D11UnorderedAccessView*>(textures[i]->GetNativeUav()) : nullptr;
    }
    context_->CSSetUnorderedAccessViews(start_slot, clamped_count, uavs, nullptr);
}

void D3D11CommandContext::CopyTexture(IGraphicsTexture* dst, IGraphicsTexture* src) {
    if (!context_ || !dst || !src) return;
    auto* dst_res = static_cast<ID3D11Resource*>(dst->GetNativeResource());
    auto* src_res = static_cast<ID3D11Resource*>(src->GetNativeResource());
    if (dst_res && src_res) {
        context_->CopyResource(dst_res, src_res);
    }
}

void D3D11CommandContext::UpdateBuffer(IGraphicsBuffer* buffer, const void* data, size_t size) {
    if (!context_ || !buffer || !data) return;
    auto* buf_res = static_cast<ID3D11Resource*>(buffer->GetNativeResource());
    if (buf_res) {
        context_->UpdateSubresource(buf_res, 0, nullptr, data, static_cast<UINT>(size), 0);
    }
}

void D3D11CommandContext::UploadTextureData(IGraphicsTexture* dst, const void* data, uint32_t row_pitch) {
    if (!context_ || !dst || !data) return;
    auto* tex_res = static_cast<ID3D11Resource*>(dst->GetNativeResource());
    if (tex_res) {
        context_->UpdateSubresource(tex_res, 0, nullptr, data, row_pitch, 0);
    }
}

bool D3D11CommandContext::ReadbackTexture(IGraphicsTexture* src, void* out_data, size_t out_size) {
    if (!context_ || !src || !out_data || out_size == 0) return false;
    auto* src_res = static_cast<ID3D11Resource*>(src->GetNativeResource());
    if (!src_res) return false;

    // Only single-sample 2D textures with a CPU-readable row layout are
    // supported by this generic path (R32F / RGBA8 depth+color readbacks).
    ID3D11Texture2D* src_tex = nullptr;
    if (FAILED(src_res->QueryInterface(__uuidof(ID3D11Texture2D),
                                       reinterpret_cast<void**>(&src_tex))) || !src_tex) {
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    src_tex->GetDesc(&td);
    src_tex->Release();
    if (td.SampleDesc.Count != 1) return false;

    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage     = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    sd.MipLevels = 1;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    ID3D11Device* dev = nullptr;
    context_->GetDevice(&dev);
    if (!dev) return false;
    HRESULT hr = dev->CreateTexture2D(&sd, nullptr, &staging);
    dev->Release();
    if (FAILED(hr) || !staging) return false;

    context_->CopyResource(staging.Get(), src_res);

    D3D11_MAPPED_SUBRESOURCE ms{};
    hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms);
    if (FAILED(hr)) return false;

    // Copy row-by-row honouring the staging row pitch. All formats this
    // path accepts today are 4 B/px (R32F for DepthProvider; RGBA8/BGRA8
    // for potential future color readbacks).
    const size_t bytes_per_px = 4u;
    const size_t row_bytes = static_cast<size_t>(td.Width) * bytes_per_px;
    if (row_bytes * td.Height > out_size) {
        context_->Unmap(staging.Get(), 0);
        return false;
    }
    const auto* src_row = static_cast<const uint8_t*>(ms.pData);
    auto* dst_row = static_cast<uint8_t*>(out_data);
    for (UINT y = 0; y < td.Height; ++y) {
        std::memcpy(dst_row + y * row_bytes,
                    src_row + static_cast<size_t>(y) * ms.RowPitch,
                    row_bytes);
    }
    context_->Unmap(staging.Get(), 0);
    return true;
}

void D3D11CommandContext::Dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
    if (context_) {
        context_->Dispatch(group_x, group_y, group_z);
    }
}

}  // namespace omnirender::graphics::d3d11
