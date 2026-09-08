// filepath: modules/daemon/pipeline.cpp
// OmniRender daemon pipeline orchestrator and pass graph execution engine.

#include "pipeline.h"

#include <dxgi1_4.h>
#include <windows.h>
#include <atomic>

#include "../common/config.h"
#include "../common/logging.h"
#include "../common/ipc_protocol.h"
#include "../common/ring_buffer.h"
#include "../common/render_graph.h"
#include "interop_d3d11.h"
#include "ipc_server.h"
#include "processing.h"
#include "presentation_win.h"
#include "frame_profiler.h"
#include "upscalers/spatial_fallback.h"
#include "upscalers/dlss_pipeline.h"
#include "upscalers/dlss_adapter.h"
#include "upscalers/xess_adapter.h"
#include "upscalers/xess_pipeline.h"

namespace omnirender::daemon {

namespace {

std::atomic<bool> g_pipeline_enabled { true };
std::atomic<bool> g_use_reconstruction { true };
std::atomic<bool> g_use_upscale { true };
std::atomic<bool> g_use_tonemap { true };

} // namespace

// Forward declaration from tonemap_neural.cpp. The current build
// only ships the bounded compute tonemap; a TensorRT-based path
// will live alongside it in a follow-up release.
bool InitializeNeuralTonemap();
void DispatchNeuralTonemap(ID3D11DeviceContext* ctx, UINT width, UINT height);

int InitializePipeline() {
    int rc = InitializeProcessing();
    if (rc < 0) {
        OMNI_LOG_WARN("processing init failed (%d); running in passthrough mode", rc);
        g_pipeline_enabled.store(false, std::memory_order_release);
        return rc;
    }

    ID3D11Device* dev = Device();
    if (dev) {
        GetGlobalProfiler().Initialize(dev);
        upscaler::InitializeSpatialFallback(dev);
        if (omnirender::config::g_enable_dlss) {
            upscaler::InitializeDLSS(dev);
        }
        if (omnirender::config::g_enable_xess) {
            upscaler::InitializeXeSS(dev);
        }
    }

    if (!InitializeNeuralTonemap()) {
        OMNI_LOG_WARN("tonemap init failed; running without tonemap");
    }
    OMNI_LOG_INFO("pipeline enabled (bounded reconstruct=%d upscale=%d tonemap=%d fsr=%d dlss=%d xess=%d rt=%d)",
                  omnirender::config::g_enable_reconstruction,
                  omnirender::config::g_enable_upscale,
                  omnirender::config::g_enable_tonemap,
                  omnirender::config::g_enable_fsr,
                  omnirender::config::g_enable_dlss,
                  omnirender::config::g_enable_xess,
                  omnirender::config::g_enable_rt_effects);
    return 0;
}

void ShutdownPipeline() {
    GetGlobalProfiler().Shutdown();
    upscaler::ShutdownDLSS();
    upscaler::ShutdownXeSS();
    upscaler::ShutdownSpatialFallback();
    ShutdownProcessing();
}

void SetReconstructionEnabled(bool enable) { g_use_reconstruction.store(enable, std::memory_order_release); }
void SetUpscaleEnabled(bool enable) { g_use_upscale.store(enable, std::memory_order_release); }
void SetTonemapEnabled(bool enable) { g_use_tonemap.store(enable, std::memory_order_release); }

void ClampTargetResolution(UINT source_width, UINT source_height,
                           UINT& target_width, UINT& target_height) {
    constexpr UINT kMaxOutputWidth = 2560, kMaxOutputHeight = 1600;
    if (target_width > kMaxOutputWidth) target_width = kMaxOutputWidth;
    if (target_height > kMaxOutputHeight) target_height = kMaxOutputHeight;
    if (target_width < source_width) target_width = source_width;
    if (target_height < source_height) target_height = source_height;
}

int RunPipelineFrame(FrameSlot& slot) {
    if (!g_pipeline_enabled.load(std::memory_order_acquire)) return 0;

    ID3D11DeviceContext* ctx = Context();
    if (!ctx) return -1;

    GetGlobalProfiler().BeginFrame(ctx, 0);

    // Re-import the shared color handle if the hook changed it.
    ID3D11Texture2D* color = nullptr;
    if (slot.payload.shared_color_handle) {
        if (ImportColorHandle(reinterpret_cast<HANDLE>(slot.payload.shared_color_handle), &color) && color) {
            // success
        } else {
            OMNI_LOG_WARN("failed to import shared color handle for frame %llu",
                          slot.payload.frame_index);
        }
    }

    ID3D11Texture2D* depth = nullptr;
    if (slot.payload.shared_depth_handle) {
        if (ImportDepthHandle(reinterpret_cast<HANDLE>(slot.payload.shared_depth_handle), &depth) && depth) {
            // success
        }
    }

    UINT tw = slot.payload.target_width;
    UINT th = slot.payload.target_height;
    ClampTargetResolution(slot.payload.surface_width, slot.payload.surface_height, tw, th);

    int rc = PrepareFrame(slot.payload, color, depth,
                          g_use_reconstruction.load(std::memory_order_acquire),
                          g_use_upscale.load(std::memory_order_acquire));
    if (rc < 0) {
        OMNI_LOG_WARN("PrepareFrame failed (%d); skipping processing for frame %llu",
                      rc, slot.payload.frame_index);
        if (color) color->Release();
        if (depth) depth->Release();
        GetGlobalProfiler().EndFrame(ctx);
        return rc;
    }

    FrameContext frame_ctx{};
    frame_ctx.frame_id = slot.payload.frame_index;
    frame_ctx.ipc_payload = slot.payload;
    frame_ctx.validity.color_valid = (color != nullptr);
    frame_ctx.validity.depth_valid = (depth != nullptr);
    frame_ctx.validity.history_valid = (ProcessingHistorySrv() != nullptr);
    for (int i = 0; i < 16; ++i) {
        if (std::abs(slot.payload.view_proj_current[i]) > 1e-6f) { frame_ctx.validity.camera_valid = true; break; }
    }
    frame_ctx.validity.motion_valid = frame_ctx.validity.depth_valid && frame_ctx.validity.camera_valid;

    RenderGraph graph;
    graph.AddPass(PassType::MotionReproject, "Motion", { true, false, false, true },
                  ResourceAccess::ReadDepth, ResourceAccess::WriteMotion, [&](FrameContext&) {
        BuildMotionVectors(slot.payload, depth, nullptr);
        GetGlobalProfiler().Timestamp(ctx, ProfilerStage::MotionEnd);
        return true;
    });
    graph.AddPass(PassType::ReactiveMask, "Reactive", { true, false, false, false },
                  ResourceAccess::ReadColor | ResourceAccess::ReadDepth, ResourceAccess::WriteReactive, [&](FrameContext&) {
        ID3D11Texture2D* prev_color = nullptr;
        if (color) BuildReactiveMask(slot.payload, color, prev_color, depth, nullptr);
        return true;
    });
    graph.AddPass(PassType::Disocclusion, "Disocclusion", { true, true, false, false },
                  ResourceAccess::ReadDepth | ResourceAccess::ReadMotion, ResourceAccess::WriteDisocc, [&](FrameContext&) {
        BuildDisocclusionMask(slot.payload, depth, nullptr, nullptr, nullptr);
        GetGlobalProfiler().Timestamp(ctx, ProfilerStage::ReactiveDisocclusionEnd);
        return true;
    });
    graph.AddPass(PassType::TemporalResolve, "Temporal", {},
                  ResourceAccess::ReadColor | ResourceAccess::ReadHistory, ResourceAccess::WriteColor, [&](FrameContext&) {
        ResolveTemporal(ctx, 0, 0, g_use_reconstruction.load(std::memory_order_acquire));
        if (omnirender::config::g_enable_rt_effects) DispatchRayTracing(ctx, tw, th);
        GetGlobalProfiler().Timestamp(ctx, ProfilerStage::ReconstructEnd);
        return true;
    });
    if (omnirender::config::g_enable_dlss && upscaler::GetGlobalDlssAdapter().IsRuntimeAvailable()) {
        graph.AddPass(PassType::Upscale, "DLSS", { true, true, false, false },
                      ResourceAccess::ReadColor | ResourceAccess::ReadDepth | ResourceAccess::ReadMotion,
                      ResourceAccess::WriteColor, [&](FrameContext& fc) {
            return upscaler::GetGlobalDlssAdapter().Execute(fc);
        });
    }
    if (omnirender::config::g_enable_xess && upscaler::GetGlobalXessAdapter().IsRuntimeAvailable()) {
        graph.AddPass(PassType::Upscale, "XeSS", { true, true, false, false },
                      ResourceAccess::ReadColor | ResourceAccess::ReadDepth | ResourceAccess::ReadMotion,
                      ResourceAccess::WriteColor, [&](FrameContext& fc) {
            return upscaler::GetGlobalXessAdapter().Execute(fc);
        });
    }
    if (g_use_tonemap.load(std::memory_order_acquire)) {
        graph.AddPass(PassType::Tonemap, "Tonemap", {}, ResourceAccess::ReadColor, ResourceAccess::WriteColor, [&](FrameContext&) {
            DispatchTonemap(ctx, tw, th);
            GetGlobalProfiler().Timestamp(ctx, ProfilerStage::TonemapEnd);
            return true;
        });
    }
    graph.Execute(frame_ctx);
    if (!g_use_tonemap.load(std::memory_order_acquire)) {
        GetGlobalProfiler().Timestamp(ctx, ProfilerStage::TonemapEnd);
    }

    // Ensure swapchain is created
    if (!SwapChain() || !RenderTargetView()) {
        CreateSwapChain(tw, th);
    }

    // In-Engine Frame Debugger Check (F12)
    int debug_mode = GetDebugMode();
    if (debug_mode != 0) {
        ID3D11ShaderResourceView* debug_srv = GetDebugChannelSrv(debug_mode);
        if (debug_srv) PresentProcessed(ctx, debug_srv, tw, th, false);
        EndFrameProcessing(ctx, depth);
        GetGlobalProfiler().Timestamp(ctx, ProfilerStage::UpscaleEnd);
        GetGlobalProfiler().Timestamp(ctx, ProfilerStage::PresentEnd);
        GetGlobalProfiler().EndFrame(ctx);
        if (color) color->Release();
        if (depth) depth->Release();
        return 0;
    }

    // Stage 5: Upscaling
    bool upscaled = false;
    if (g_use_upscale.load(std::memory_order_acquire) && SwapChain()) {
        ID3D11Texture2D* backbuffer = nullptr;
        if (SUCCEEDED(SwapChain()->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer))) && backbuffer) {
            ID3D11UnorderedAccessView* back_uav = nullptr;
            if (SUCCEEDED(Device()->CreateUnorderedAccessView(backbuffer, nullptr, &back_uav)) && back_uav) {
                if (omnirender::config::g_enable_fsr && upscaler::SpatialFallbackAvailable()) {
                    D3D11_TEXTURE2D_DESC td{};
                    backbuffer->GetDesc(&td);
                    if (ProcessingWorkSrv()) {
                        upscaler::DispatchSpatialFallback(ctx, ProcessingWorkSrv(), back_uav,
                                                          slot.payload.surface_width, slot.payload.surface_height,
                                                          td.Width, td.Height);
                        upscaled = true;
                    }
                }
                back_uav->Release();
            }
            backbuffer->Release();
        }
    }
    GetGlobalProfiler().Timestamp(ctx, ProfilerStage::UpscaleEnd);

    // Stage 6: Present
    if (!upscaled) {
        PresentProcessed(ctx, ProcessingWorkSrv(), tw, th, g_use_upscale.load(std::memory_order_acquire));
    }
    EndFrameProcessing(ctx, depth);
    GetGlobalProfiler().Timestamp(ctx, ProfilerStage::PresentEnd);

    GetGlobalProfiler().EndFrame(ctx);

    if (color) color->Release();
    if (depth) depth->Release();
    return 0;
}

int RunPassthroughFrame(FrameSlot& slot) {
    ID3D11DeviceContext* ctx = Context();
    if (!ctx) return -1;

    ID3D11Texture2D* color = nullptr;
    if (slot.payload.shared_color_handle) {
        if (ImportColorHandle(reinterpret_cast<HANDLE>(slot.payload.shared_color_handle), &color) && color) {
            // success
        } else {
            OMNI_LOG_WARN("PassthroughFrame: failed to import shared color handle");
        }
    }

    if (!SwapChain() || !RenderTargetView()) {
        CreateSwapChain(slot.payload.target_width, slot.payload.target_height);
    }

    ID3D11ShaderResourceView* srv = nullptr;
    if (color) {
        Device()->CreateShaderResourceView(color, nullptr, &srv);
    }
    PresentProcessed(Context(), srv,
                     slot.payload.target_width  ? slot.payload.target_width  : slot.payload.surface_width,
                     slot.payload.target_height ? slot.payload.target_height : slot.payload.surface_height,
                     false);
    if (srv) srv->Release();

    if (color) color->Release();
    return 0;
}

bool PipelineReady() {
    return g_pipeline_enabled.load(std::memory_order_acquire);
}

}  // namespace omnirender::daemon
