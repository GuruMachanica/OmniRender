// filepath: graphics/d3d11/D3D11CommandContext.cpp
#include "D3D11CommandContext.h"
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

void D3D11CommandContext::Dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
    if (context_) {
        context_->Dispatch(group_x, group_y, group_z);
    }
}

}  // namespace omnirender::graphics::d3d11
