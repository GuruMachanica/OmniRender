# OmniRender Future Work & Strategic Roadmap

This document outlines the sequential phases of OmniRender's technical development following the establishment of the platform-independent core architecture.

---

## Phase 1: Real GPU Integration & Verification Milestone
*Target: v0.8.x*

- **Automated GPU Integration Test Suite**:
  - Standalone headless execution testing `IGraphicsDevice` and `ICommandContext`.
  - Validate `RenderGraph` dependency resolution: `Capture → Motion → Reactive → DLSS/FSR → Output`.
- **End-to-End Reconstruction Pixel Readback**:
  - Run real frames through DLSS / XeSS / FSR adapters and perform GPU readback (`ID3D11DeviceContext::CopyResource` to staging texture) to verify non-black, reconstructed image output.
- **Temporal History Rotation Proof**:
  - Test multi-frame temporal sequences: Frame 0 (Empty) → Frame 1 (WarmingUp) → Frame 2 (Valid) → Camera cut (Invalidated) → Rebuilding.

---

## Phase 2: Presentation & Swapchain Hardening
*Target: v0.9.x*

- **Low-Latency Pacing**:
  - DXGI 1.3 `IDXGISwapChain2::GetFrameLatencyWaitableObject` integration with precise microsecond sleep pacing.
- **True HDR10 & Wide Color Gamut**:
  - Direct3D 11 / DXGI `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020` swapchain presentation.
- **Borderless & Multi-Monitor Window Management**:
  - Automatic DPI scaling, dynamic game window tracking, and borderless window positioning.

---

## Phase 3: Linux & Steam Deck (Vulkan) Support
*Target: v1.1.0*

- **Vulkan Implicit Capture Layer**:
  - `libomnirender-hook.so` registered in `/usr/share/vulkan/implicit_layer.d/`.
  - Intercepts `vkQueuePresentKHR` across native Linux games and Proton/Wine (DXVK).
- **Zero-Copy DMA-BUF Cross-Process Interop**:
  - Export swapchain images via `VK_KHR_external_memory_fd` and `VK_EXT_external_memory_dma_buf`.
  - Transfer file descriptors over local Unix domain sockets (`AF_UNIX` with `SCM_RIGHTS`).
- **Vulkan 1.3 Daemon Pipeline**:
  - Concrete `graphics/vulkan/` implementing `IGraphicsDevice` and `ICommandContext`.
  - Compile existing HLSL compute kernels to SPIR-V via `dxc -spirv`.
- **Compositor Integration**:
  - Wayland `wl_surface` direct scanout and SteamOS **Gamescope** integration.

---

## Phase 4: macOS (Apple Silicon & Metal) Support
*Target: v1.2.0*

- **Metal Graphics HAL**:
  - Concrete `graphics/metal/` implementing `IGraphicsDevice` using Metal 3.
- **IOSurface Zero-Copy Sharing**:
  - Wrap game framebuffers into `IOSurfaceRef` and pass 32-bit `IOSurfaceID` across processes.
- **Apple MetalFX Upscaling**:
  - Native integration with `MTLFXTemporalScaler` and `MTLFXSpatialScaler` utilizing Apple Neural Engine cores.

---

## Phase 5: Optical Flow & Frame Generation
*Target: v1.3.0+*

- **Dense Optical Flow Estimation**:
  - Compute motion vectors for titles without accessible depth buffers via NVIDIA Optical Flow Accelerator (OFA) and DirectX / Vulkan compute shaders.
- **Frame Interpolation & Generation**:
  - Synthesize intermediate frames between reconstructed inputs to double displayed frame rate.
- **Reflexive Latency Management**:
  - Dynamic GPU/CPU frame queue pacing to prevent buffer bloat during frame generation.

---

## Connected Documentation

- [Platform Support Matrix](PLATFORM_SUPPORT.md)
- [Architecture Guide](ARCHITECTURE.md)
- [Central Repository README](README.md)
- [API Reference](docs/api_reference.md)
