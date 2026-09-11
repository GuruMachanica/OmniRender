// filepath: backends/reconstruction/spatial/SpatialUpscaleBackend.cpp
// FSR 1.0 (EASU + RCAS) dispatch for the core runtime pipeline.
//
// The constant-buffer layouts here must byte-match the compiled CSOs:
//   modules/shaders/fsr_easu.hlsl  -> cbuffer FsrEasuConstants  { float4 Const0..3; }
//   modules/shaders/fsr_rcas.hlsl  -> cbuffer FsrRcasConstants  { float4 RcasConfig; }
// Both CSOs run [numthreads(16,16,1)] and clamp loads to Const1.zw (EASU) /
// RcasConfig.yz (RCAS), so no out-of-bounds dispatch guard is needed beyond
// sizing the textures to the output resolution.

#include "SpatialUpscaleBackend.h"

#if defined(_WIN32)

#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

#include "../../../core/temporal/ShaderPath.h"
#include "../../../core/resources/TextureDesc.h"
#include "../../../core/resources/BufferDesc.h"
#include "../../../graphics/abstraction/IGraphicsDevice.h"
#include "../../../graphics/abstraction/ICommandContext.h"
#include "../../../graphics/abstraction/IGraphicsBuffer.h"

namespace omnirender::backends::spatial {

namespace {

struct alignas(16) FsrEasuConstants {
    float Const0[4];  // { in_w/out_w, in_h/out_h, 0.5*x-0.5, 0.5*y-0.5 }
    float Const1[4];  // { 1/in_w, 1/in_h, in_w, in_h }  (zw = clamp bounds)
    float Const2[4];  // { out_w, out_h, 1/out_w, 1/out_h }
    float Const3[4];  // padding
};

struct alignas(16) FsrRcasConstants {
    float RcasConfig[4];  // { sharpness, out_w, out_h, 0 }
};

std::vector<char> ReadFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<char> blob(sz);
    f.read(blob.data(), static_cast<std::streamsize>(sz));
    return blob;
}

// Clamp the RCAS sharpening amount into the range the legacy path uses.
float ClampSharpness(float s) noexcept {
    return std::clamp(s, 0.0f, 1.0f);
}

}  // namespace

SpatialUpscaleBackend::~SpatialUpscaleBackend() {
    Shutdown();
}

bool SpatialUpscaleBackend::LoadShaders() {
    ReleaseShaders();
    ID3D11Device* d3d = static_cast<ID3D11Device*>(device_ ? device_->GetNativeDevice() : nullptr);
    if (!d3d) {
        last_error_ = "no native device";
        return false;
    }

    for (int pass = 0; pass < 2; ++pass) {
        const char* name = (pass == 0) ? "fsr_easu.cso" : "fsr_rcas.cso";
        auto blob = ReadFileBytes(core::temporal::GetShaderPath(name));
        if (blob.empty()) {
            last_error_ = std::string("missing ") + name;
            return false;
        }
        ID3D11ComputeShader* cs = nullptr;
        if (FAILED(d3d->CreateComputeShader(blob.data(), blob.size(), nullptr, &cs))) {
            last_error_ = std::string("CreateComputeShader failed for ") + name;
            return false;
        }
        if (pass == 0) easu_cs_ = cs; else rcas_cs_ = cs;
    }
    return true;
}

void SpatialUpscaleBackend::ReleaseShaders() {
    if (easu_cs_) { easu_cs_->Release(); easu_cs_ = nullptr; }
    if (rcas_cs_) { rcas_cs_->Release(); rcas_cs_ = nullptr; }
}

bool SpatialUpscaleBackend::EnsureTargets(graphics::IGraphicsDevice& device) {
    const bool sized =
        easu_target_.IsValid() && rcas_target_.IsValid() &&
        easu_target_.GetWidth()  == output_res_.width &&
        easu_target_.GetHeight() == output_res_.height;
    if (sized) return true;

    const core::TextureUsage usage =
        core::TextureUsage::ShaderResource | core::TextureUsage::UnorderedAccess;
    core::TextureDesc desc{
        output_res_.width, output_res_.height, 1,
        core::TextureFormat::R8G8B8A8_UNORM, usage, "FSR_EasuTarget" };
    auto easu_tex = device.CreateTexture(desc);
    if (!easu_tex) {
        last_error_ = "easu target allocation failed";
        return false;
    }
    desc.debug_name = "FSR_RcasTarget";
    auto rcas_tex = device.CreateTexture(desc);
    if (!rcas_tex) {
        last_error_ = "rcas target allocation failed";
        return false;
    }
    easu_target_ = core::GpuTexture(std::move(easu_tex));
    rcas_target_ = core::GpuTexture(std::move(rcas_tex));
    return easu_target_.IsValid() && rcas_target_.IsValid();
}

bool SpatialUpscaleBackend::Initialize(graphics::IGraphicsDevice& device,
                                       const core::Resolution& in_res,
                                       const core::Resolution& out_res) {
    Shutdown();
    device_ = &device;
    input_res_  = in_res;
    output_res_ = out_res;

    if (!EnsureTargets(device)) {
        Shutdown();
        return false;
    }
    if (!LoadShaders()) {
        Shutdown();
        return false;
    }

    // Persistent constant buffers; contents updated per frame via UpdateBuffer.
    auto eb = device.CreateBuffer(
        core::BufferDesc{ sizeof(FsrEasuConstants), 0,
                          core::BufferUsage::ConstantBuffer, "FsrEasuCB" }, nullptr);
    auto rb = device.CreateBuffer(
        core::BufferDesc{ sizeof(FsrRcasConstants), 0,
                          core::BufferUsage::ConstantBuffer, "FsrRcasCB" }, nullptr);
    if (!eb || !rb) {
        last_error_ = "constant buffer allocation failed";
        Shutdown();
        return false;
    }
    easu_cb_ = std::move(eb);
    rcas_cb_ = std::move(rb);

    runtime_available_ = true;
    return true;
}

void SpatialUpscaleBackend::Shutdown() {
    ReleaseShaders();
    easu_cb_.reset();
    rcas_cb_.reset();
    easu_target_.Reset();
    rcas_target_.Reset();
    device_ = nullptr;
    runtime_available_ = false;
}

void SpatialUpscaleBackend::OnDeviceLost() {
    Shutdown();
}

bool SpatialUpscaleBackend::OnDeviceRestored(graphics::IGraphicsDevice& device) {
    return Initialize(device, input_res_, output_res_);
}

bool SpatialUpscaleBackend::Resize(const core::Resolution& in_res,
                                   const core::Resolution& out_res) {
    if (!device_) return false;
    input_res_  = in_res;
    output_res_ = out_res;
    return EnsureTargets(*device_);
}

ReconstructionResult SpatialUpscaleBackend::Execute(core::FrameContext& fc,
                                                    graphics::ICommandContext& ctx) {
    if (!runtime_available_ || !easu_cs_ || !rcas_cs_ || !easu_cb_ || !rcas_cb_)
        return {};
    if (!fc.color.IsValid())
        return {};

    const uint32_t in_w = fc.input_resolution.width;
    const uint32_t in_h = fc.input_resolution.height;
    uint32_t out_w = fc.output_resolution.width  ? fc.output_resolution.width  : in_w;
    uint32_t out_h = fc.output_resolution.height ? fc.output_resolution.height : in_h;

    // Nothing to upscale — hand the input back unchanged.
    if (out_w == in_w && out_h == in_h)
        return { fc.color, fc.input_resolution, true };

    if (easu_target_.GetWidth() != out_w || easu_target_.GetHeight() != out_h) {
        if (!Resize({ in_w, in_h }, { out_w, out_h })) return {};
    }
    if (!easu_target_.IsValid() || !rcas_target_.IsValid())
        return {};

    ID3D11DeviceContext* d3d = static_cast<ID3D11DeviceContext*>(ctx.GetNativeContext());
    if (!d3d) return {};

    // EASU constants — identical formulation to the legacy daemon path.
    FsrEasuConstants ec{};
    ec.Const0[0] = static_cast<float>(in_w)  / static_cast<float>(out_w);
    ec.Const0[1] = static_cast<float>(in_h)  / static_cast<float>(out_h);
    ec.Const0[2] = 0.5f * ec.Const0[0] - 0.5f;
    ec.Const0[3] = 0.5f * ec.Const0[1] - 0.5f;
    ec.Const1[0] = 1.0f / static_cast<float>(in_w);
    ec.Const1[1] = 1.0f / static_cast<float>(in_h);
    ec.Const1[2] = static_cast<float>(in_w);   // EASU clamp bound x
    ec.Const1[3] = static_cast<float>(in_h);   // EASU clamp bound y
    ec.Const2[0] = static_cast<float>(out_w);
    ec.Const2[1] = static_cast<float>(out_h);
    ec.Const2[2] = 1.0f / static_cast<float>(out_w);
    ec.Const2[3] = 1.0f / static_cast<float>(out_h);

    FsrRcasConstants rc{};
    rc.RcasConfig[0] = ClampSharpness(sharpness_);
    rc.RcasConfig[1] = static_cast<float>(out_w);   // RCAS clamp bound x
    rc.RcasConfig[2] = static_cast<float>(out_h);   // RCAS clamp bound y
    rc.RcasConfig[3] = 0.0f;

    ctx.UpdateBuffer(easu_cb_.get(), &ec, sizeof(ec));
    ctx.UpdateBuffer(rcas_cb_.get(), &rc, sizeof(rc));

    const uint32_t gx = (out_w + 15u) / 16u;
    const uint32_t gy = (out_h + 15u) / 16u;

    // Pass 1 — EASU: input color -> easu_target.
    graphics::IGraphicsBuffer* cbs[]   = { easu_cb_.get() };
    graphics::IGraphicsTexture* src[]   = { fc.color.Get() };
    graphics::IGraphicsTexture* uavs[]  = { easu_target_.Get() };
    ctx.SetComputeShader(easu_cs_);
    ctx.SetConstantBuffers(0, 1, cbs);
    ctx.SetShaderResources(0, 1, src);
    ctx.SetUnorderedAccessViews(0, 1, uavs);
    ctx.Dispatch(gx, gy, 1);
    graphics::IGraphicsTexture* null_tex[] = { nullptr };
    ctx.SetShaderResources(0, 1, null_tex);
    ctx.SetUnorderedAccessViews(0, 1, null_tex);

    // Pass 2 — RCAS: easu_target -> rcas_target.
    graphics::IGraphicsTexture* easu_src[] = { easu_target_.Get() };
    graphics::IGraphicsTexture* rcas_uav[] = { rcas_target_.Get() };
    ctx.SetComputeShader(rcas_cs_);
    graphics::IGraphicsBuffer* rcbs[] = { rcas_cb_.get() };
    ctx.SetConstantBuffers(0, 1, rcbs);
    ctx.SetShaderResources(0, 1, easu_src);
    ctx.SetUnorderedAccessViews(0, 1, rcas_uav);
    ctx.Dispatch(gx, gy, 1);
    ctx.SetShaderResources(0, 1, null_tex);
    ctx.SetUnorderedAccessViews(0, 1, null_tex);
    ctx.SetComputeShader(nullptr);

    fc.output           = rcas_target_;
    fc.output_resolution = { out_w, out_h };
    return { fc.output, fc.output_resolution, true };
}

}  // namespace omnirender::backends::spatial

#endif  // _WIN32
