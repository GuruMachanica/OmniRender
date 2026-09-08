# OmniRender Canonical System Architecture

This document serves as the comprehensive architectural specification for the OmniRender project.

---

## 1. High-Level Canonical Architecture

```text
                    OmniRender
                         │
          ┌──────────────┴──────────────┐
          │                             │
     Platform-neutral              Platform layer
          │                             │
   ┌──────┼────────┐          ┌─────────┼─────────┐
   │      │        │          │         │         │
 Frame  Render   Runtime    Windows    Linux     macOS
Context Graph      │           │         │         │
   │      │        │         D3D      Vulkan     Metal
   └──────┴────────┘
          │
    Backend Resolver
          │
   ┌──────┼───────┐
   │      │       │
 DLSS   XeSS     FSR
          │
    OmniTemporal
```

---

## 2. End-to-End Dataflow Pipeline

```text
Game Process
    │
    ▼
CaptureBackend (d3d9 / dxgi / opengl / vulkan)
    │
    ▼ [Zero-Copy GPU Surface Handles]
FrameContext
    ├── Color Surface        (GpuTexture)
    ├── Depth Surface        (GpuTexture)
    ├── Motion Vectors       (GpuTexture)
    ├── Reactive Mask        (GpuTexture)
    ├── Camera Matrices      (CameraState)
    ├── Subpixel Jitter      (JitterState)
    └── Temporal History     (HistoryManager)
    │
    ▼
RenderGraph (DAG Execution Scheduler)
    │
    ▼
BackendResolver
    ├── NVIDIA DLSS Super Resolution
    ├── Intel XeSS Machine Learning
    ├── AMD FidelityFX Super Resolution
    └── OmniRender Native YCoCg Accumulation
    │
    ▼
Graphics Hardware Abstraction Layer (HAL)
    ├── Direct3D 11 Backend (Windows)
    ├── Direct3D 12 Backend (Windows)
    ├── Vulkan 1.3 Backend  (Linux / SteamOS)
    └── Metal 3 Backend     (macOS)
    │
    ▼
PresentationBackend (DXGI 1.3 Waitable Swapchain / Wayland / MetalLayer)
```

---

## 3. Subsystem Breakdown & Separation of Concerns

### A. Core Foundation (`core/`) — 100% Platform-Independent
- **No OS Headers**: Zero inclusion of `<windows.h>`, `<d3d11.h>`, `<dxgi.h>`, `<vulkan/vulkan.h>`, or `<Metal/Metal.h>`.
- **`FrameContext`**: Primary payload holding abstract `GpuTexture` and `GpuBuffer` references along with camera and timing metadata.
- **`RenderGraph`**: Directed acyclic graph verifying prerequisite surface flags before dispatching pass execution lambdas.
- **`HistoryManager`**: Multi-slot history tracking with explicit invalidation triggers (resolution change, camera jump, scene cut).

### B. Graphics Hardware Abstraction Layer (`graphics/`)
- **`IGraphicsDevice`**: Factory for textures, buffers, and execution contexts. Backends implement `OpenSharedTexture` to ingest shared GPU memory with zero CPU copying.
- **`ICommandContext`**: Hardware execution interface abstracting compute shader dispatches, texture copying, and constant buffer updates.
- **`d3d11/`**: Windows concrete implementation wrapping `ID3D11Device` and `ID3D11DeviceContext`.

### C. Runtime Orchestration (`runtime/`)
- **`BackendResolver`**: Negotiates upscaler selection based on GPU vendor ID, VRAM, and feature flags.
- **`Pipeline`**: Compiles passes into the `RenderGraph` and manages end-of-frame history commitment.

### D. In-Process Capture (`modules/hook/`)
- Injected proxy library intercepting `Present()` or `SwapBuffers()`.
- Obtains OS-level shared texture handles (`IDXGIResource::GetSharedHandle` on Windows, DMA-BUF file descriptors on Linux).
- Submits per-frame metadata into the lock-free SPSC shared memory ring buffer.

### E. Inter-Process Communication (IPC)
- Shared memory ring buffer with monotonic modulo sequence indexing (`producer_seq % capacity`).
- Byte-for-byte aligned 64-bit ABI (`#pragma pack(push, 8)`) preventing struct drift across 32-bit and 64-bit boundaries.
- Cross-process event notification via OS primitives (Win32 named events / Linux `eventfd`).

---

## Connected Documentation

- [Platform Support Matrix](PLATFORM_SUPPORT.md)
- [Strategic Roadmap](FUTURE_WORK.md)
- [API Reference](docs/api_reference.md)
- [Build Guide](docs/build.md)
