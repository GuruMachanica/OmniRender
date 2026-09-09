// filepath: modules/daemon/pipeline_runtime.cpp
// Routes daemon frames through runtime::Pipeline (the new temporal path).
//
// This is the NEW execution path. The old path (pipeline.cpp/processing.cpp)
// remains compiled behind OMNIRENDER_LEGACY_PIPELINE for rollback safety.
//
// Key design: Pipeline::Initialize() is deferred to the first NewPipelineFrame()
// call so the real swapchain resolution (from the IPC payload) is used, not a
// hardcoded placeholder. On resize, OnDeviceLost()+OnDeviceRestored() rebuilds
// history textures and temporal buffers at the new dimensions.

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

std::unique_ptr<graphics::d3d11::D3D11GraphicsDevice> g_rt_device;
std::unique_ptr<graphics::d3d11::D3D11CommandContext>  g_rt_context;
std::unique_ptr<runtime::Pipeline>                     g_rt_pipeline;
std::unique_ptr<CaptureAdapter>                        g_rt_adapter;

// Last resolution at which Pipeline::Initialize() ran (0x0 = not yet).
uint32_t g_pipeline_width  = 0;
uint32_t g_pipeline_height = 0;

bool g_device_ready = false;  // device/context/adapter created
bool g_pipe_ready   = false;  // Pipeline::Initialize() succeeded at least once

// Attach the best available reconstruction backend (idempotent after resize).
void AttachBackend() {
    if (omnirender::config::g_enable_dlss) {
        auto& dlss = upscaler::GetGlobalDlssAdapter();
        if (dlss.IsRuntimeAvailable()) {
            g_rt_pipeline->SetReconstructionBackend(
                std::shared_ptr<backends::IReconstructionBackend>(
                    &dlss, [](backends::IReconstructionBackend*) {}));
            OMNI_LOG_INFO("runtime pipeline: DLSS backend attached");
            return;
        }
    }
    if (omnirender::config::g_enable_xess) {
        auto& xess = upscaler::GetGlobalXessAdapter();
        if (xess.IsRuntimeAvailable()) {
            g_rt_pipeline->SetReconstructionBackend(
                std::shared_ptr<backends::IReconstructionBackend>(
                    &xess, [](backends::IReconstructionBackend*) {}));
            OMNI_LOG_INFO("runtime pipeline: XeSS backend attached");
            return;
        }
    }
    OMNI_LOG_INFO("runtime pipeline: no reconstruction backend (spatial-only)");
}

// (Re-)initialize Pipeline at the given resolution.
// On resize, tears down old state via OnDeviceLost() first so history
// textures and temporal buffers are rebuilt at the correct dimensions.
bool InitPipelineAtResolution(uint32_t w, uint32_t h,
                              core::TextureFormat color_fmt) {
    if (g_pipe_ready) {
        OMNI_LOG_INFO("runtime pipeline: resize %ux%u -> %ux%u; flushing history",
                      g_pipeline_width, g_pipeline_height, w, h);
        g_rt_pipeline->OnDeviceLost();
    }

    const core::Resolution res{ w, h };
    if (!g_rt_pipeline->Initialize(*g_rt_device, res, res,
                                   color_fmt,
                                   core::TextureFormat::R32_FLOAT)) {
        OMNI_LOG_ERROR("runtime pipeline: Initialize failed at %ux%u", w, h);
        g_pipe_ready       = false;
        g_pipeline_width   = 0;
        g_pipeline_height  = 0;
        return false;
    }

    AttachBackend();
    g_pipe_ready      = true;
    g_pipeline_width  = w;
    g_pipeline_height = h;
    OMNI_LOG_INFO("runtime pipeline: ready at %ux%u", w, h);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool InitializeRuntimePipeline() {
    if (g_device_ready) return true;

    ID3D11Device*        dev = Device();
    ID3D11DeviceContext* ctx = Context();
    if (!dev || !ctx) {
        OMNI_LOG_ERROR("InitializeRuntimePipeline: D3D11 interop not ready");
        return false;
    }

    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device>        d(dev);
    ComPtr<ID3D11DeviceContext> c(ctx);

    g_rt_device   = std::make_unique<graphics::d3d11::D3D11GraphicsDevice>(d, c);
    g_rt_context  = std::make_unique<graphics::d3d11::D3D11CommandContext>(c);
    g_rt_pipeline = std::make_unique<runtime::Pipeline>();
    g_rt_adapter  = std::make_unique<CaptureAdapter>(*g_rt_device);

    // Pipeline::Initialize() is deferred to the first NewPipelineFrame() so
    // the real IPC surface dimensions are used, not a hardcoded placeholder.
    g_device_ready = true;
    OMNI_LOG_INFO("runtime pipeline: device ready (init deferred to first frame)");
    return true;
}

void ShutdownRuntimePipeline() {
    g_rt_pipeline.reset();
    g_rt_adapter.reset();
    g_rt_context.reset();
    g_rt_device.reset();
    g_device_ready    = false;
    g_pipe_ready      = false;
    g_pipeline_width  = 0;
    g_pipeline_height = 0;
}

int NewPipelineFrame(FrameSlot& slot) {
    if (!g_device_ready || !g_rt_pipeline || !g_rt_adapter || !g_rt_context)
        return -1;

    const uint32_t W = slot.payload.surface_width;
    const uint32_t H = slot.payload.surface_height;
    if (W == 0 || H == 0) return -1;

    // Lazy init on first frame, or re-init on resolution change.
    if (!g_pipe_ready || W != g_pipeline_width || H != g_pipeline_height) {
        const core::TextureFormat fmt =
            CaptureAdapter::ToDxgiFormat(slot.payload.color_format);
        const core::TextureFormat color_fmt =
            (fmt == core::TextureFormat::Unknown)
                ? core::TextureFormat::RGBA8_UNORM : fmt;
        if (!InitPipelineAtResolution(W, H, color_fmt))
            return -1;
    }

    core::FrameContext fc = g_rt_adapter->Adapt(slot.payload);
    if (!fc.IsValid()) {
        OMNI_LOG_WARN("NewPipelineFrame: Adapt produced invalid context (frame %llu)",
                      slot.payload.frame_index);
        return -1;
    }

    const bool ok = g_rt_pipeline->ExecuteFrame(fc, *g_rt_context);
    return ok ? 0 : -1;
}

// RuntimePipelineReady() returns true once the device is created so that
// presentation_win.cpp starts routing frames here.  The pipeline itself is
// initialized lazily on the first frame with a valid resolution.
bool RuntimePipelineReady() {
    return g_device_ready && g_rt_pipeline != nullptr;
}

}  // namespace omnirender::daemon

#endif // !OMNIRENDER_LEGACY_PIPELINE
