// filepath: modules/daemon/frame_profiler.cpp
// Microsecond-accurate GPU and CPU execution profiling implementation.

#include "frame_profiler.h"
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace omnirender::daemon {

FrameProfiler& GetGlobalProfiler() {
    static FrameProfiler profiler;
    return profiler;
}

FrameProfiler::~FrameProfiler() {
    Shutdown();
}

int FrameProfiler::Initialize(ID3D11Device* device) {
    Shutdown();
    device_ = device;
    if (!device_) return -1;

    D3D11_QUERY_DESC qdesc{};
    for (int b = 0; b < 2; ++b) {
        qdesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        if (FAILED(device_->CreateQuery(&qdesc, &buffers_[b].disjoint))) {
            Shutdown();
            return -2;
        }

        qdesc.Query = D3D11_QUERY_TIMESTAMP;
        for (size_t s = 0; s < static_cast<size_t>(ProfilerStage::Count); ++s) {
            if (FAILED(device_->CreateQuery(&qdesc, &buffers_[b].timestamps[s]))) {
                Shutdown();
                return -3;
            }
        }
        buffers_[b].issued = false;
    }

    active_idx_ = 0;
    current_metrics_ = {};
    return 0;
}

void FrameProfiler::Shutdown() {
    for (int b = 0; b < 2; ++b) {
        if (buffers_[b].disjoint) {
            buffers_[b].disjoint->Release();
            buffers_[b].disjoint = nullptr;
        }
        for (size_t s = 0; s < static_cast<size_t>(ProfilerStage::Count); ++s) {
            if (buffers_[b].timestamps[s]) {
                buffers_[b].timestamps[s]->Release();
                buffers_[b].timestamps[s] = nullptr;
            }
        }
        buffers_[b].issued = false;
    }
    device_ = nullptr;
}

void FrameProfiler::BeginFrame(ID3D11DeviceContext* ctx, uint32_t queue_depth) {
    if (!ctx) return;
    cpu_frame_start_ = std::chrono::steady_clock::now();
    current_metrics_.queue_depth = queue_depth;

    auto& cur_buf = buffers_[active_idx_];
    if (cur_buf.disjoint) {
        ctx->Begin(cur_buf.disjoint);
        ctx->End(cur_buf.timestamps[static_cast<size_t>(ProfilerStage::FrameBegin)]);
    }
}

void FrameProfiler::Timestamp(ID3D11DeviceContext* ctx, ProfilerStage stage) {
    if (!ctx) return;
    auto& cur_buf = buffers_[active_idx_];
    size_t idx = static_cast<size_t>(stage);
    if (idx < static_cast<size_t>(ProfilerStage::Count) && cur_buf.timestamps[idx]) {
        ctx->End(cur_buf.timestamps[idx]);
    }
}

void FrameProfiler::EndFrame(ID3D11DeviceContext* ctx) {
    if (!ctx) return;
    auto& cur_buf = buffers_[active_idx_];
    if (cur_buf.disjoint) {
        ctx->End(cur_buf.disjoint);
        cur_buf.issued = true;
    }

    // Measure CPU duration for current frame
    auto cpu_now = std::chrono::steady_clock::now();
    current_metrics_.cpu_frame_ms =
        std::chrono::duration<float, std::milli>(cpu_now - cpu_frame_start_).count();
    current_metrics_.frames_processed++;

    // Read back previous frame query to avoid stalling GPU pipeline
    uint32_t read_idx = 1 - active_idx_;
    auto& read_buf = buffers_[read_idx];

    if (read_buf.issued && read_buf.disjoint) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data{};
        HRESULT hr = ctx->GetData(read_buf.disjoint, &disjoint_data,
                                  sizeof(disjoint_data), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_OK) {
            read_buf.issued = false;
            if (!disjoint_data.Disjoint && disjoint_data.Frequency > 0) {
                double freq = static_cast<double>(disjoint_data.Frequency);
                UINT64 ts[static_cast<size_t>(ProfilerStage::Count)]{};
                bool all_ok = true;

                for (size_t s = 0; s < static_cast<size_t>(ProfilerStage::Count); ++s) {
                    if (read_buf.timestamps[s]) {
                        HRESULT thr = ctx->GetData(read_buf.timestamps[s], &ts[s],
                                                   sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                        if (thr != S_OK) {
                            all_ok = false;
                            break;
                        }
                    }
                }

                if (all_ok) {
                    auto CalcMs = [freq](UINT64 start, UINT64 end) -> float {
                        if (end < start) return 0.0f;
                        return static_cast<float>(((end - start) / freq) * 1000.0);
                    };

                    current_metrics_.gpu_motion_ms =
                        CalcMs(ts[static_cast<size_t>(ProfilerStage::FrameBegin)],
                               ts[static_cast<size_t>(ProfilerStage::MotionEnd)]);

                    current_metrics_.gpu_reactive_ms =
                        CalcMs(ts[static_cast<size_t>(ProfilerStage::MotionEnd)],
                               ts[static_cast<size_t>(ProfilerStage::ReactiveDisocclusionEnd)]);

                    current_metrics_.gpu_reconstruct_ms =
                        CalcMs(ts[static_cast<size_t>(ProfilerStage::ReactiveDisocclusionEnd)],
                               ts[static_cast<size_t>(ProfilerStage::ReconstructEnd)]);

                    current_metrics_.gpu_tonemap_ms =
                        CalcMs(ts[static_cast<size_t>(ProfilerStage::ReconstructEnd)],
                               ts[static_cast<size_t>(ProfilerStage::TonemapEnd)]);

                    current_metrics_.gpu_upscale_ms =
                        CalcMs(ts[static_cast<size_t>(ProfilerStage::TonemapEnd)],
                               ts[static_cast<size_t>(ProfilerStage::UpscaleEnd)]);

                    current_metrics_.gpu_present_ms =
                        CalcMs(ts[static_cast<size_t>(ProfilerStage::UpscaleEnd)],
                               ts[static_cast<size_t>(ProfilerStage::PresentEnd)]);

                    current_metrics_.gpu_total_ms =
                        CalcMs(ts[static_cast<size_t>(ProfilerStage::FrameBegin)],
                               ts[static_cast<size_t>(ProfilerStage::PresentEnd)]);
                }
            }
        }
    }

    active_idx_ = 1 - active_idx_;
}

void FrameProfiler::RecordDroppedFrame() {
    current_metrics_.frames_dropped++;
}

void FrameProfiler::RecordHistoryRetention(float retention_pct) {
    current_metrics_.history_retention_pct = retention_pct;
}

std::wstring FrameProfiler::FormatHudText() const {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << L"OmniRender Latency Profiler:\n";
    ss << L"  Motion Est:     " << current_metrics_.gpu_motion_ms << L" ms\n";
    ss << L"  Disoccl/React:  " << current_metrics_.gpu_reactive_ms << L" ms\n";
    ss << L"  Reconstruction: " << current_metrics_.gpu_reconstruct_ms << L" ms\n";
    ss << L"  Upscaling:      " << current_metrics_.gpu_upscale_ms << L" ms\n";
    ss << L"  Tonemapping:    " << current_metrics_.gpu_tonemap_ms << L" ms\n";
    ss << L"  Presentation:   " << current_metrics_.gpu_present_ms << L" ms\n";
    ss << L"  ---------------------------\n";
    ss << L"  Total GPU Time: " << current_metrics_.gpu_total_ms << L" ms\n";
    ss << L"  CPU Frame Time: " << current_metrics_.cpu_frame_ms << L" ms\n";
    ss << L"  Queue Depth:    " << current_metrics_.queue_depth << L"\n";
    ss << L"  History Trust:  " << current_metrics_.history_retention_pct << L"%\n";
    return ss.str();
}

}  // namespace omnirender::daemon
