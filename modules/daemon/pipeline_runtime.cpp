// filepath: modules/daemon/pipeline_runtime.cpp
// Routes daemon frames through runtime::Pipeline (the new temporal path).
//
// Old path (pipeline.cpp/processing.cpp) remains behind OMNIRENDER_LEGACY_PIPELINE.
//
// Design invariants:
// - Pipeline::Initialize() is deferred to the first NewPipelineFrame() call.
// - Both input AND output resolutions are tracked independently; a change in
//   either triggers OnDeviceLost() + fresh Initialize() + AttachBackend().
// - A backend is only attached when IsRuntimeAvailable() AND it can actually
//   execute (DLL loaded + GPU supported). An unexecutable backend is never
//   registered so it cannot turn the upscale pass Required and break frames.
// - Failed initializations use an exponential backoff so NGX/device-creation
//   is not hammered every frame on persistent failure.

#ifndef OMNIRENDER_LEGACY_PIPELINE

#include "pipeline_runtime.h"

#include <chrono>
#include <cmath>
#include <windows.h>
#include <wrl/client.h>

#include "../common/config.h"
#include "../common/ipc_protocol.h"
#include "../common/logging.h"
#include "../common/ring_buffer.h"
#include "capture_adapter.h"
#include "interop_d3d11.h"
#include "presentation_win.h"
#include "upscalers/xess_adapter.h"

#include "../../runtime/Pipeline.h"
#include "../../backends/reconstruction/dlss/DlssReconstructionBackend.h"
#include "../../backends/reconstruction/spatial/SpatialUpscaleBackend.h"
#include "../../graphics/d3d11/D3D11GraphicsDevice.h"
#include "../../graphics/d3d11/D3D11CommandContext.h"

namespace {

// ---------------------------------------------------------------------------
// Output resolution policy (audit fix). The hook renders at the game's native
// swapchain size; the daemon decides what it presents. Modes are documented
// in modules/common/config.h. Result is always clamped to sane bounds.
// ---------------------------------------------------------------------------
core::Resolution ResolveOutputResolution(const core::Resolution& input) {
    if (!omnirender::config::g_enable_upscale) return input;

    const std::string& mode = omnirender::config::g_output_scale_mode;
    core::Resolution out = input;

    if (mode == "native") {
        return input;
    } else if (mode == "screen") {
        // Desktop resolution of the primary display. The overlay window covers
        // the whole screen, so this is the natural "fill the monitor" target.
        HMONITOR mon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            out = { static_cast<uint32_t>(mi.rcMonitor.right - mi.rcMonitor.left),
                    static_cast<uint32_t>(mi.rcMonitor.bottom - mi.rcMonitor.top) };
        } else {
            out = { omnirender::config::g_output_width,
                    omnirender::config::g_output_height };
        }
    } else if (mode == "quality") {
        out = { input.width * 3u / 2u, input.height * 3u / 2u };
    } else if (mode == "ultra") {
        out = { input.width * 2u, input.height * 2u };
    } else if (mode == "custom") {
        out = { omnirender::config::g_output_width,
                omnirender::config::g_output_height };
    } else {
        // Unknown mode string: fail safe to the documented default.
        out = { omnirender::config::g_output_width,
                omnirender::config::g_output_height };
    }

    // Never present *below* the input resolution (that would be a downscale)
    // and never blow past reasonable 4K+ bounds.
    if (out.width < input.width)   out.width = input.width;
    if (out.height < input.height) out.height = input.height;
    if (out.width == 0 || out.height == 0) out = input;
    return out;
}

}  // namespace

namespace omnirender::daemon {

namespace {

std::unique_ptr<graphics::d3d11::D3D11GraphicsDevice> g_rt_device;
std::unique_ptr<graphics::d3d11::D3D11CommandContext>  g_rt_context;
std::unique_ptr<runtime::Pipeline>                     g_rt_pipeline;
std::unique_ptr<CaptureAdapter>                        g_rt_adapter;

// Full resolution domain tracked independently.
core::Resolution    g_input_res  {};
core::Resolution    g_output_res {};
core::TextureFormat g_color_fmt = core::TextureFormat::Unknown;

bool g_device_ready = false;
bool g_pipe_ready   = false;

// #15: last reconstructed output texture (valid until next frame).
graphics::IGraphicsTexture* g_last_output_tex = nullptr;

// Retry backoff state (issue #13).
using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
static constexpr int kMaxFailures    = 8;
static constexpr int kBaseRetryMs    = 500;
int       g_fail_count   = 0;
TimePoint g_next_retry   = Clock::now();

bool IsRetryAllowed() {
    return g_fail_count < kMaxFailures && Clock::now() >= g_next_retry;
}

void RecordInitFailure() {
    ++g_fail_count;
    int delay_ms = kBaseRetryMs * (1 << std::min(g_fail_count, 7));
    g_next_retry = Clock::now() + std::chrono::milliseconds(delay_ms);
    OMNI_LOG_WARN("runtime pipeline: init failed (attempt %d); retry in %d ms",
                  g_fail_count, delay_ms);
}

// Core-side DLSS backend (implements omnirender::backends::IReconstructionBackend,
// the interface runtime::Pipeline actually consumes). Created once and reused
// across resizes; Pipeline::SetReconstructionBackend drives its Initialize().
std::shared_ptr<backends::IReconstructionBackend> g_core_dlss;

// Attach the best available backend that can actually execute (issue #3).
// Returns true if a functional backend was attached.
bool AttachBackend(const core::Resolution& in, const core::Resolution& out) {
    if (omnirender::config::g_enable_dlss) {
        if (!g_core_dlss) {
            g_core_dlss = std::make_shared<backends::dlss::DlssReconstructionBackend>();
        }
        // Pipeline::SetReconstructionBackend calls Initialize(*device, in, out)
        // (pipeline is already initialized here) and drops the backend on failure.
        g_rt_pipeline->SetReconstructionBackend(g_core_dlss);
        auto* dlss = static_cast<backends::dlss::DlssReconstructionBackend*>(g_core_dlss.get());
        if (dlss->IsRuntimeAvailable()) {
            OMNI_LOG_INFO("runtime pipeline: DLSS attached (%ux%u -> %ux%u)",
                          in.width, in.height, out.width, out.height);
            return true;
        }
        OMNI_LOG_INFO("runtime pipeline: DLSS init failed (state=%s); falling back to spatial",
                      dlss->GetStateString());
        g_core_dlss.reset();
        // Fall through to the FSR spatial fallback below.
    }
    if (omnirender::config::g_enable_xess) {
        auto& xess = upscaler::GetGlobalXessAdapter();
        // XeSS Execute() returns false (not yet implemented), so treat it as
        // unavailable in the runtime path to avoid poisoning the Required pass.
        if (xess.IsRuntimeAvailable()) {
            OMNI_LOG_INFO("runtime pipeline: XeSS SDK present but execute unimplemented; "
                          "skipping attach (spatial fallback will be used)");
        }
    }

    // Fallback: FSR 1.0 (EASU + RCAS) spatial upscaling. This runs on every
    // vendor and needs no SDK — only the two compiled CSOs shipped next to
    // the daemon. It is what actually performs upscaling on non-DLSS GPUs
    // (and on NVIDIA boxes where NGX is absent).
    if (omnirender::config::g_enable_upscale && omnirender::config::g_enable_fsr) {
        auto spatial = std::make_shared<backends::spatial::SpatialUpscaleBackend>();
        spatial->SetSharpness(0.75f);
        // Pipeline::SetReconstructionBackend runs Initialize(*device, in, out)
        // and drops the backend on failure, mirroring the DLSS attach above.
        g_rt_pipeline->SetReconstructionBackend(spatial);
        if (spatial->IsRuntimeAvailable()) {
            OMNI_LOG_INFO("runtime pipeline: FSR spatial attached (%ux%u -> %ux%u)",
                          in.width, in.height, out.width, out.height);
            return true;
        }
        OMNI_LOG_WARN("runtime pipeline: FSR spatial init failed: %s",
                      spatial->LastErrorString().c_str());
        spatial->Shutdown();
    }
    OMNI_LOG_INFO("runtime pipeline: no reconstruction backend (passthrough)");
    return false;
}

// (Re-)initialize Pipeline for the given input+output resolution pair.
bool InitPipelineAtResolution(const core::Resolution& in,
                              const core::Resolution& out,
                              core::TextureFormat color_fmt) {
    if (g_pipe_ready) {
        OMNI_LOG_INFO("runtime pipeline: resize [%ux%u->%ux%u] -> [%ux%u->%ux%u]; "
                      "flushing history",
                      g_input_res.width, g_input_res.height,
                      g_output_res.width, g_output_res.height,
                      in.width, in.height, out.width, out.height);
        g_rt_pipeline->OnDeviceLost();
        g_pipe_ready     = false;
        g_last_output_tex = nullptr;
        // Flush handle cache so textures are re-opened at the new dimensions (#14).
        if (g_rt_adapter) g_rt_adapter->InvalidateCache();
    }

    if (!g_rt_pipeline->Initialize(*g_rt_device, in, out,
                                   color_fmt, core::TextureFormat::R32_FLOAT)) {
        OMNI_LOG_ERROR("runtime pipeline: Initialize failed [%ux%u->%ux%u]",
                       in.width, in.height, out.width, out.height);
        g_input_res = g_output_res = {};
        RecordInitFailure();
        return false;
    }

    AttachBackend(in, out);
    g_pipe_ready  = true;
    g_input_res   = in;
    g_output_res  = out;
    g_color_fmt   = color_fmt;
    g_fail_count  = 0;  // success resets backoff
    OMNI_LOG_INFO("runtime pipeline: ready [%ux%u -> %ux%u]",
                  in.width, in.height, out.width, out.height);
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
    g_rt_device   = std::make_unique<graphics::d3d11::D3D11GraphicsDevice>(
                        ComPtr<ID3D11Device>(dev), ComPtr<ID3D11DeviceContext>(ctx));
    g_rt_context  = std::make_unique<graphics::d3d11::D3D11CommandContext>(
                        ComPtr<ID3D11DeviceContext>(ctx));
    g_rt_pipeline = std::make_unique<runtime::Pipeline>();
    g_rt_adapter  = std::make_unique<CaptureAdapter>(*g_rt_device);

    g_device_ready = true;
    OMNI_LOG_INFO("runtime pipeline: device ready (init deferred to first frame)");
    return true;
}

void ShutdownRuntimePipeline() {
    g_rt_pipeline.reset();
    g_rt_adapter.reset();
    g_rt_context.reset();
    g_rt_device.reset();
    g_core_dlss.reset();
    g_device_ready = g_pipe_ready = false;
    g_input_res = g_output_res = {};
    g_color_fmt  = core::TextureFormat::Unknown;
    g_fail_count = 0;
}

int NewPipelineFrame(FrameSlot& slot) {
    if (!g_device_ready || !g_rt_pipeline || !g_rt_adapter || !g_rt_context)
        return -1;

    const uint32_t iw = slot.payload.surface_width;
    const uint32_t ih = slot.payload.surface_height;
    if (iw == 0 || ih == 0) return -1;

    // Output resolution is a daemon-side decision (renderer.output_scale),
    // not a hook-side field. Hooks publish target == surface; overriding it
    // here is what actually lets the upscale path engage.
    const core::Resolution in{ iw, ih };
    const core::Resolution out = ResolveOutputResolution(in);

    const core::TextureFormat fmt =
        CaptureAdapter::ToDxgiFormat(slot.payload.color_format);
    const core::TextureFormat color_fmt =
        (fmt == core::TextureFormat::Unknown)
            ? core::TextureFormat::R8G8B8A8_UNORM : fmt;

    // Re-init if input OR output resolution or format changed (issues #1, #2).
    const bool needs_init = !g_pipe_ready
        || in  != g_input_res
        || out != g_output_res
        || color_fmt != g_color_fmt;

    if (needs_init) {
        if (!IsRetryAllowed()) return -1;  // backoff active (issue #13)
        if (!InitPipelineAtResolution(in, out, color_fmt)) return -1;
    }

    core::FrameContext fc = g_rt_adapter->Adapt(slot.payload);
    if (!fc.IsValid()) {
        OMNI_LOG_WARN("NewPipelineFrame: invalid context (frame %llu)",
                      slot.payload.frame_index);
        return -1;
    }

    // Hooks publish target == surface (they never know the display intent);
    // the daemon-owned output policy wins here so the backend pass sees the
    // real upscale target every frame.
    fc.output_resolution = out;

    const bool ok = g_rt_pipeline->ExecuteFrame(fc, *g_rt_context);
    // #15: store the reconstructed output for GetLastOutputTexture().
    g_last_output_tex = ok && fc.output.IsValid()
                        ? fc.output.Get()
                        : nullptr;
    return ok ? 0 : -1;
}

graphics::IGraphicsTexture* GetLastOutputTexture() noexcept {
    return g_last_output_tex;
}

void GetLastOutputResolution(uint32_t* width, uint32_t* height) noexcept {
    if (width)  *width  = g_last_output_tex ? g_last_output_tex->GetWidth()  : 0;
    if (height) *height = g_last_output_tex ? g_last_output_tex->GetHeight() : 0;
}

// Issue #12: returns true only when both device AND pipeline are ready.
// Presentation loop uses this; first-frame lazy-init is separate.
bool RuntimePipelineReady() {
    return g_device_ready && g_pipe_ready;
}

// Separate query for the device-only ready state so presentation_win can
// route frames to NewPipelineFrame() even before the first lazy-init fires.
bool RuntimeDeviceReady() {
    return g_device_ready && g_rt_pipeline != nullptr;
}

}  // namespace omnirender::daemon

#endif // !OMNIRENDER_LEGACY_PIPELINE
