// filepath: graphics/d3d11/D3D11GraphicsDevice.cpp
#include "D3D11GraphicsDevice.h"
#include "D3D11GraphicsTexture.h"
#include "D3D11GraphicsBuffer.h"
#include "D3D11TypeConversions.h"

namespace omnirender::graphics::d3d11 {

D3D11GraphicsDevice::D3D11GraphicsDevice(Microsoft::WRL::ComPtr<ID3D11Device> device,
                                         Microsoft::WRL::ComPtr<ID3D11DeviceContext> context)
    : device_(std::move(device)) {
    if (!context && device_) {
        device_->GetImmediateContext(&context);
    }
    if (context) {
        immediate_context_ = std::make_shared<D3D11CommandContext>(std::move(context));
    }
}

std::shared_ptr<ICommandContext> D3D11GraphicsDevice::GetImmediateContext() {
    return immediate_context_;
}

std::shared_ptr<IGraphicsTexture> D3D11GraphicsDevice::CreateTexture(const core::TextureDesc& desc) {
    if (!device_ || desc.width == 0 || desc.height == 0) return nullptr;

    D3D11_TEXTURE2D_DESC td{};
    td.Width              = desc.width;
    td.Height             = desc.height;
    td.MipLevels          = desc.mip_levels;
    td.ArraySize          = 1;
    td.Format             = ToDxgiFormat(desc.format);
    td.SampleDesc.Count   = 1;
    td.SampleDesc.Quality = 0;
    td.Usage              = D3D11_USAGE_DEFAULT;
    td.BindFlags          = ToD3D11BindFlags(desc.usage);

    if (core::HasFlag(desc.usage, core::TextureUsage::SharedHandle)) {
        td.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &tex))) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (core::HasFlag(desc.usage, core::TextureUsage::ShaderResource)) {
        device_->CreateShaderResourceView(tex.Get(), nullptr, &srv);
    }

    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    if (core::HasFlag(desc.usage, core::TextureUsage::UnorderedAccess)) {
        device_->CreateUnorderedAccessView(tex.Get(), nullptr, &uav);
    }

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    if (core::HasFlag(desc.usage, core::TextureUsage::RenderTarget)) {
        device_->CreateRenderTargetView(tex.Get(), nullptr, &rtv);
    }

    return std::make_shared<D3D11GraphicsTexture>(desc, std::move(tex), std::move(srv), std::move(uav), std::move(rtv));
}

std::shared_ptr<IGraphicsTexture> D3D11GraphicsDevice::OpenSharedTexture(uint64_t shared_handle) {
    if (!device_ || shared_handle == 0) return nullptr;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = device_->OpenSharedResource(reinterpret_cast<HANDLE>(shared_handle),
                                            __uuidof(ID3D11Texture2D),
                                            reinterpret_cast<void**>(tex.GetAddressOf()));
    if (FAILED(hr) || !tex) return nullptr;

    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);

    core::TextureDesc desc{
        td.Width,
        td.Height,
        td.MipLevels,
        FromDxgiFormat(td.Format),
        core::TextureUsage::ShaderResource | core::TextureUsage::SharedHandle,
        "ImportedSharedTexture"
    };

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    device_->CreateShaderResourceView(tex.Get(), nullptr, &srv);

    return std::make_shared<D3D11GraphicsTexture>(desc, std::move(tex), std::move(srv));
}

std::shared_ptr<IGraphicsBuffer> D3D11GraphicsDevice::CreateBuffer(const core::BufferDesc& desc, const void* initial_data) {
    if (!device_ || desc.byte_width == 0) return nullptr;

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth           = desc.byte_width;
    bd.Usage               = D3D11_USAGE_DEFAULT;
    bd.BindFlags           = ToD3D11BindFlags(desc.usage);
    bd.StructureByteStride = desc.stride_bytes;

    D3D11_SUBRESOURCE_DATA sub{};
    if (initial_data) {
        sub.pSysMem = initial_data;
    }

    Microsoft::WRL::ComPtr<ID3D11Buffer> buf;
    if (FAILED(device_->CreateBuffer(&bd, initial_data ? &sub : nullptr, &buf))) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (core::HasFlag(desc.usage, core::BufferUsage::Structured)) {
        device_->CreateShaderResourceView(buf.Get(), nullptr, &srv);
    }

    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    if (core::HasFlag(desc.usage, core::BufferUsage::UnorderedAccess)) {
        device_->CreateUnorderedAccessView(buf.Get(), nullptr, &uav);
    }

    return std::make_shared<D3D11GraphicsBuffer>(desc, std::move(buf), std::move(srv), std::move(uav));
}

}  // namespace omnirender::graphics::d3d11
