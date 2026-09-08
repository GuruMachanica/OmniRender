// filepath: modules/common/backend_interfaces.h
#pragma once

#include <cstdint>
#include "frame_context.h"
#include "backend_capabilities.h"

namespace omnirender {

// Interface for multi-frame temporal reconstruction backends (DLSS SR, XeSS SR, OmniTemporal).
class IReconstructionBackend {
public:
    virtual ~IReconstructionBackend() = default;
    virtual BackendType GetType() const noexcept = 0;
    virtual const char* GetName() const noexcept = 0;
    virtual BackendCapabilities GetCapabilities() const noexcept = 0;
    virtual bool Initialize(uint32_t max_width, uint32_t max_height) = 0;
    virtual bool Execute(FrameContext& ctx) = 0;
    virtual void Shutdown() = 0;
};

// Interface for spatial upscalers and edge sharpening passes (FSR EASU+RCAS, Spatial Bilinear).
class IUpscalerBackend {
public:
    virtual ~IUpscalerBackend() = default;
    virtual BackendType GetType() const noexcept = 0;
    virtual const char* GetName() const noexcept = 0;
    virtual BackendCapabilities GetCapabilities() const noexcept = 0;
    virtual bool Initialize(uint32_t target_width, uint32_t target_height) = 0;
    virtual bool Dispatch(FrameContext& ctx) = 0;
    virtual void Shutdown() = 0;
};

// Interface for optical flow and frame interpolation backends (DLSS FG, XeSS FG, FSR FG).
class IFrameGenerationBackend {
public:
    virtual ~IFrameGenerationBackend() = default;
    virtual BackendType GetType() const noexcept = 0;
    virtual const char* GetName() const noexcept = 0;
    virtual BackendCapabilities GetCapabilities() const noexcept = 0;
    virtual bool Initialize(uint32_t width, uint32_t height) = 0;
    virtual bool BeginFrame(uint64_t frame_index) = 0;
    virtual bool Generate(const FrameContext& current, const FrameContext& previous, FrameContext& out_generated) = 0;
    virtual bool Present(uint32_t sync_interval) = 0;
    virtual void Shutdown() = 0;
};

// Markers for low-latency pacing and pipeline stage telemetry.
enum class LatencyMarker : uint8_t {
    SimulationStart = 0,
    SimulationEnd,
    RenderSubmitStart,
    RenderSubmitEnd,
    PresentStart,
    PresentEnd,
    InputSample
};

// Interface for reflex and low-latency pacing backends (NVIDIA Reflex, Intel XeLL, AMD Anti-Lag).
class ILowLatencyBackend {
public:
    virtual ~ILowLatencyBackend() = default;
    virtual const char* GetName() const noexcept = 0;
    virtual bool Initialize() = 0;
    virtual void SetLatencyMarker(LatencyMarker marker, uint64_t frame_index) = 0;
    virtual void SetSleepMode(bool enable, uint32_t min_interval_us) = 0;
    virtual void Sleep() = 0;
    virtual void Shutdown() = 0;
};

// Interface for neural and spatial ray reconstruction / denoising (DLSS RR, NRD).
class IRayDenoiserBackend {
public:
    virtual ~IRayDenoiserBackend() = default;
    virtual const char* GetName() const noexcept = 0;
    virtual bool Initialize(uint32_t width, uint32_t height) = 0;
    virtual bool Denoise(FrameContext& ctx) = 0;
    virtual void Shutdown() = 0;
};

// Interface for swapchain presentation and frame pacing backends.
class IPresentationBackend {
public:
    virtual ~IPresentationBackend() = default;
    virtual bool Configure(uint32_t width, uint32_t height, bool vrr, bool hdr) = 0;
    virtual bool Present(const FrameContext& ctx, uint32_t sync_interval) = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual void Shutdown() = 0;
};

}  // namespace omnirender
