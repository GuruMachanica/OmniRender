// filepath: modules/daemon/pipeline_runtime.cpp
// Routes daemon frames through runtime::Pipeline (the new temporal path).
//
// This is the NEW execution path. The old path (pipeline.cpp/processing.cpp)
// remains compiled behind OMNIRENDER_LEGACY_PIPELINE for rollback safety.
//
// Integration point: presentation_win.cpp calls NewPipelineFrame() when
// OMNIRENDER_LEGACY_PIPELINE is NOT defined. NewPipelineFrame() returns 0
// on success, <0 on hard failure (caller falls back to passthrough).

#ifndef OMNIRENDER_LEGACY_PIPELINE

#include "pipeline_runtime.h"

#include <windows.h>
#include <wrl/client.h>

#include "../common/config.h"
#include "../common/ipc_protocol.h"
#include "../common/logging.h"
#include "../common/ring_buffer.h"
#include "capture_adapter.h"
#include "interop_d3d11.h"
#include "presentation_win.h"
#include "upscalers/dlss_adapter.h"
#include "upscalers/xess_adapter.h"

#include "../../runtime/Pipeline.h"
#include "../../graphics/d3d11/D3D11GraphicsDevice.h"
#include "../../graphics/d3d11/D3D11CommandContext.h"

namespace omnirender::daemon {

namespace {

// Singletons owned by the new pipeline path.
std::unique_ptr<graphics::d3d11::D3D11GraphicsDevice> g_rt_device;
std::unique_ptr<graphics::d3d11::D3D11CommandContext>  g_rt_context;
std::unique_ptr<runtime::Pipeline>                     g_rt_pipeline;
std::unique_ptr<CaptureAdapter>                        g_rt_adapter;

bool g_initialized = false;

}  // namespace

bool InitializeRuntimePipeline() {
    if (g_initialized) return true;

    ID3D11Device*        dev = Device();
    ID3D11DeviceContext* ctx = Context();
    if (!dev || !ctx) {
        OMNI_LOG_ERROR("InitializeRuntimePipeline: D3D11 interop not ready");
        return false;
    }

    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device>        d(dev);
    ComPtr<ID3D11DeviceContext> c(ctx);

    g_rt_device  = std::make_unique<graphics::d3d11::D3D11GraphicsDevice>(d, c);
    g_rt_context = std::make_unique<graphics::d3d11::D3D11CommandContext>(c);
    g_rt_pipeline = std::make_unique<runtime::Pipeline>();
    g_rt_adapter  = std::make_unique<CaptureAdapter>(*g_rt_device);

    // Placeholder resolution — will be resized on the first frame that
    // carries a valid surface_width/height.
    core::Resolution input_res  { 1920, 1080 };
    core::Resolution output_res { 1920, 1080 };

    if (!g_rt_pipeline->Initialize(*g_rt_device, input_res, output_res,
                                   core::TextureFormat::RGBA8_UNORM,
                                   core::TextureFormat::R32_FLOAT)) {
        OMNI_LOG_ERROR("InitializeRuntimePipeline: Pipeline::Initialize failed");
        g_rt_pipeline.reset();
        return false;
    }

    // Attach whichever reconstruction backend is available.
    // Prefer DLSS (if NGX is loaded), fall back to XeSS (if SDK loaded),
    // otherwise runtime::Pipeline uses the spatial fallback.
    if (omnirender::config::g_enable_dlss) {
        auto& dlss = upscaler::GetGlobalDlssAdapter();
        if (dlss.IsRuntimeAvailable()) {
            g_rt_pipeline->SetReconstructionBackend(
                std::shared_ptr<backends::IReconstructionBackend>(
                    &dlss, [](backends::IReconstructionBackend*) {}));
            OMNI_LOG_INFO("runtime pipeline: DLSS backend attached");
        }
    } else if (omnirender::config::g_enable_xess) {
        auto& xess = upscaler::GetGlobalXessAdapter();
        if (xess.IsRuntimeAvailable()) {
            g_rt_pipeline->SetReconstructionBackend(
                std::shared_ptr<backends::IReconstructionBackend>(
                    &xess, [](backends::IReconstructionBackend*) {}));
            OMNI_LOG_INFO("runtime pipeline: XeSS backend attached");
        }
    } else {
        OMNI_LOG_INFO("runtime pipeline: no reconstruction backend (spatial-only mode)");
    }

    g_initialized = true;
    OMNI_LOG_INFO("runtime pipeline: initialized");
    return true;
}

void ShutdownRuntimePipeline() {
    g_rt_pipeline.reset();
    g_rt_adapter.reset();
    g_rt_context.reset();
    g_rt_device.reset();
    g_initialized = false;
}

// Called from presentation_win.cpp once per consumed frame slot.
// Returns 0 on success, <0 on failure (caller falls back to passthrough).
int NewPipelineFrame(FrameSlot& slot) {
    if (!g_initialized || !g_rt_pipeline || !g_rt_adapter || !g_rt_context)
        return -1;

    core::FrameContext fc = g_rt_adapter->Adapt(slot.payload);
    if (!fc.IsValid()) {
        OMNI_LOG_WARN("NewPipelineFrame: Adapt produced invalid context (frame %llu)",
                      slot.payload.frame_index);
        return -1;
    }

    const bool ok = g_rt_pipeline->ExecuteFrame(fc, *g_rt_context);
    return ok ? 0 : -1;
}

bool RuntimePipelineReady() {
    return g_initialized && g_rt_pipeline != nullptr;
}

}  // namespace omnirender::daemon

#endif // !OMNIRENDER_LEGACY_PIPELINE
