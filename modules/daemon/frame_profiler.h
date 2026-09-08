// filepath: modules/daemon/frame_profiler.h
// Microsecond-accurate GPU and CPU execution profiling for OmniRender.
// Uses double-buffered D3D11 timestamp queries to measure per-pass GPU
// latency without causing CPU pipeline stalls.

#pragma once

#include <d3d11.h>
#include <cstdint>
#include <chrono>
#include <string>

namespace omnirender::daemon {

enum class ProfilerStage : uint32_t {
    FrameBegin = 0,
    MotionEnd,
    ReactiveDisocclusionEnd,
    ReconstructEnd,
    TonemapEnd,
    UpscaleEnd,
    PresentEnd,
    Count
};

struct FrameMetrics {
    float    gpu_motion_ms         = 0.0f;
    float    gpu_reactive_ms       = 0.0f;
    float    gpu_reconstruct_ms    = 0.0f;
    float    gpu_upscale_ms        = 0.0f;
    float    gpu_tonemap_ms        = 0.0f;
    float    gpu_present_ms        = 0.0f;
    float    gpu_total_ms          = 0.0f;
    float    cpu_frame_ms          = 0.0f;
    uint32_t queue_depth           = 0;
    uint64_t frames_processed      = 0;
    uint64_t frames_dropped        = 0;
    float    history_retention_pct = 100.0f;
};

class FrameProfiler {
public:
    FrameProfiler() = default;
    ~FrameProfiler();

    int  Initialize(ID3D11Device* device);
    void Shutdown();

    void BeginFrame(ID3D11DeviceContext* ctx, uint32_t queue_depth);
    void Timestamp(ID3D11DeviceContext* ctx, ProfilerStage stage);
    void EndFrame(ID3D11DeviceContext* ctx);

    void RecordDroppedFrame();
    void RecordHistoryRetention(float retention_pct);

    const FrameMetrics& GetMetrics() const { return current_metrics_; }
    std::wstring FormatHudText() const;

private:
    struct QueryBuffer {
        ID3D11Query* disjoint = nullptr;
        ID3D11Query* timestamps[static_cast<size_t>(ProfilerStage::Count)]{};
        bool         issued   = false;
    };

    ID3D11Device* device_ = nullptr;
    QueryBuffer   buffers_[2];
    uint32_t      active_idx_ = 0;

    std::chrono::steady_clock::time_point cpu_frame_start_{};
    FrameMetrics  current_metrics_{};
};

FrameProfiler& GetGlobalProfiler();

}  // namespace omnirender::daemon
