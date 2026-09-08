// filepath: modules/daemon/main.cpp
// OmniRender daemon entry point and per-frame pipeline.
//
// v0.5.0-alpha pipeline (full temporal reconstruction):
//
//   ConsumeFrame
//     ↓
//   OpenSharedResource(color)
//   OpenSharedResource(depth)        ← zero-copy from hook
//     ↓
//   FenceWait until slot.fence >= slot.frame_index
//     ↓
//   PrepareFrame →
//       depth linearize
//       motion vector synthesis (depth+VP reprojection)
//       reactive mask
//       bounded working resolution (VRAM ceiling)
//       temporal reconstruction (history ping-pong, bounded)
//       upscale toward target if requested
//       inverse tone map
//     ↓
//   Present into borderless overlay
//
// No DLSS / FSR / XeSS in this release path unless optional SDKs are
// linked. That keeps the stock build usable and VRAM-bounded.

#include <windows.h>

#include <filesystem>
#include <string>

#include "../common/config.h"
#include "../common/logging.h"
#include "capability_matrix.h"
#include "interop_d3d11.h"
#include "pipeline_runtime.h"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace omnirender::daemon {

int  RunHardwareNegotiator(void** ppAdapter);
int  InitializeIpcServer();
void ShutdownIpcServer();
int  InitializePipeline();
void ShutdownPipeline();
int  RunPresentationLoop();

}  // namespace omnirender::daemon

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)nCmdShow;

    OMNI_LOG_INFO("OmniRender daemon starting (v0.5.0-alpha)");

    // v0.5.0-alpha: layered config. Load defaults, then global, then
    // per-game (hashed from the executable), then env-var overrides.
    // The executable path comes from the first command-line argument
    // when the daemon is launched as a proxy (e.g. via the launcher).
    std::wstring exe_path;
    if (lpCmdLine && lpCmdLine[0] != L'\0') {
        exe_path = lpCmdLine;
    } else {
        wchar_t self[MAX_PATH]{};
        if (::GetModuleFileNameW(nullptr, self, MAX_PATH) > 0) {
            exe_path = self;
        }
    }

    // v0.7.0-alpha: detect the host executable's renderer API
    // (D3D8/9/10/11/12/OpenGL/Vulkan) by inspecting its PE import
    // table. The launcher uses this to pick the right hook DLL.
    omnirender::daemon::DetectedRenderers detected =
        omnirender::daemon::InspectExecutable(exe_path);
    OMNI_LOG_INFO("renderer detect: primary=%s imported_dlls=%zu",
                  omnirender::daemon::RendererApiName(detected.primary),
                  detected.imported_dlls.size());
    for (auto api : detected.detected) {
        OMNI_LOG_INFO("  - %s", omnirender::daemon::RendererApiName(api));
    }

    omnirender::config::ConfigStore cfg =
        omnirender::config::LoadFullConfig(exe_path);

    void* adapter = nullptr;
    const int hw_rc = omnirender::daemon::RunHardwareNegotiator(&adapter);
    if (hw_rc < 0) {
        OMNI_LOG_ERROR("Hardware negotiation failed (%d)", hw_rc);
        return 1;
    }
    if (omnirender::daemon::InitializeIpcServer() != 0) {
        OMNI_LOG_ERROR("IPC server failed to start");
        return 2;
    }
    if (!omnirender::daemon::InitializeInterop(adapter)) {
        OMNI_LOG_ERROR("D3D11 interop initialization failed");
        return 3;
    }

    // Apply the merged config to the runtime knobs.
    omnirender::config::g_enable_reconstruction =
        cfg.GetBool("pipeline.enable_reconstruction", true);
    omnirender::config::g_enable_upscale =
        cfg.GetBool("pipeline.enable_upscale", true);
    omnirender::config::g_enable_tonemap =
        cfg.GetBool("pipeline.enable_tonemap", true);
    omnirender::config::g_enable_optical_flow =
        cfg.GetBool("pipeline.enable_optical_flow", false);
    omnirender::config::g_enable_dlss =
        cfg.GetBool("pipeline.enable_dlss", false);
    omnirender::config::g_enable_trt_tonemap =
        cfg.GetBool("pipeline.enable_trt_tonemap", false);
    omnirender::config::g_enable_xess =
        cfg.GetBool("pipeline.enable_xess", false);
    omnirender::config::g_enable_fsr =
        cfg.GetBool("pipeline.enable_fsr", false);
    omnirender::config::g_enable_rt_effects =
        cfg.GetBool("pipeline.enable_rt_effects", false);

    omnirender::config::g_max_work_width =
        static_cast<UINT>(cfg.GetInt("pipeline.max_work_width",
                                    static_cast<int64_t>(omnirender::kDefaultMaxProcessingWidth)));
    omnirender::config::g_max_work_height =
        static_cast<UINT>(cfg.GetInt("pipeline.max_work_height",
                                    static_cast<int64_t>(omnirender::kDefaultMaxProcessingHeight)));
    omnirender::config::g_max_history =
        static_cast<UINT>(cfg.GetInt("temporal.history",
                                    static_cast<int64_t>(omnirender::kDefaultMaxHistoryFrames)));

    OMNI_LOG_INFO("config: reconstruction=%d upscale=%d tonemap=%d optical_flow=%d dlss=%d trt_tonemap=%d xess=%d fsr=%d rt=%d max_work=%ux%u history=%u",
                  omnirender::config::g_enable_reconstruction,
                  omnirender::config::g_enable_upscale,
                  omnirender::config::g_enable_tonemap,
                  omnirender::config::g_enable_optical_flow,
                  omnirender::config::g_enable_dlss,
                  omnirender::config::g_enable_trt_tonemap,
                  omnirender::config::g_enable_xess,
                  omnirender::config::g_enable_fsr,
                  omnirender::config::g_enable_rt_effects,
                  omnirender::config::g_max_work_width,
                  omnirender::config::g_max_work_height,
                  omnirender::config::g_max_history);

    // On first launch with no config file, write a default global
    // config so the user has a starting point.
    auto global_path = omnirender::config::GlobalConfigPath();
    if (!global_path.empty() && !std::filesystem::exists(global_path)) {
        cfg.SaveToFile(global_path);
        OMNI_LOG_INFO("config: wrote default config to %s",
                      global_path.string().c_str());
    }
    // Also write a per-game profile if one doesn't exist yet.
    if (!exe_path.empty()) {
        auto profile_path = omnirender::config::ProfilePathFor(exe_path);
        if (!profile_path.empty() && !std::filesystem::exists(profile_path)) {
            cfg.SaveToFile(profile_path);
            OMNI_LOG_INFO("config: wrote per-game profile to %s",
                          profile_path.string().c_str());
        }
    }

    const int pipe_rc = omnirender::daemon::InitializePipeline();
    if (pipe_rc < 0) {
        OMNI_LOG_WARN("Pipeline init failed (%d); running passthrough only", pipe_rc);
    }

#ifndef OMNIRENDER_LEGACY_PIPELINE
    if (!omnirender::daemon::InitializeRuntimePipeline()) {
        OMNI_LOG_WARN("Runtime pipeline init failed; new temporal path unavailable");
    }
#endif

    const int rc = omnirender::daemon::RunPresentationLoop();
#ifndef OMNIRENDER_LEGACY_PIPELINE
    omnirender::daemon::ShutdownRuntimePipeline();
#endif
    omnirender::daemon::ShutdownPipeline();
    omnirender::daemon::ShutdownIpcServer();
    OMNI_LOG_INFO("OmniRender daemon exiting (%d)", rc);
    return rc;
}


