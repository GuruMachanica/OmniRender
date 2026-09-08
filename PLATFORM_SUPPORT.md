# OmniRender Platform Support Matrix

> **Important**: A platform-independent `core/` and `runtime/` does **not** mean platform support is complete. It establishes the architectural foundation so that platform backends can be added cleanly without refactoring the engine.

---

## 1. Platform Matrix

| Platform | Tier / Status | Capture Subsystem | Graphics HAL | Presentation | Target Release |
|---|---|---|---|---|---|
| **Windows 10 / 11** | `Tier 1: Active` | Direct3D 9, Direct3D 11, OpenGL 3.x+ (Proxy DLL) | Direct3D 11 (`graphics/d3d11`) | DXGI 1.3 Waitable Flip-Model Overlay | **v0.8.0+** (Current) |
| **Linux / SteamOS** | `Tier 2: Foundation` | Vulkan Implicit Layer / OpenGL (Planned) | Vulkan 1.3 (Planned) | Wayland `wl_surface` / Gamescope (Planned) | **v1.1.0** (Planned) |
| **macOS (Apple Silicon)** | `Tier 2: Foundation` | Dynamic loader interposition / GPTK (Planned) | Metal 3 (Planned) | `CAMetalLayer` Overlay (Planned) | **v1.2.0** (Planned) |

---

## 2. Implementation Maturity by Subsystem

###  Implemented (Production / Active)
- **Platform-Neutral Core (`core/`)**: Zero OS or graphics API headers.
- **RenderGraph DAG (`core/graph/`)**: Directed acyclic pass execution scheduler.
- **HistoryManager (`core/temporal/`)**: Multi-slot temporal history state machine.
- **Resource Abstractions (`core/resources/`)**: `GpuTexture`, `GpuBuffer`, `ResourceHandle`.
- **Runtime Orchestration (`runtime/`)**: `Pipeline` pass compilation and `BackendResolver`.
- **Direct3D 11 HAL (`graphics/d3d11/`)**: Concrete `IGraphicsDevice` and `ICommandContext` wrapping D3D11.
- **In-Process Capture Proxy (`modules/hook/`)**: D3D9, D3D11, and OpenGL shared surface export.
- **Lock-Free IPC**: SPSC shared-memory ring buffer with fixed-width 64-bit ABI.
- **AMD FSR 1.0**: Standalone EASU + RCAS spatial compute kernels.

### 🔬 Experimental (Feature Complete, Real-World Validation Ongoing)
- **3x3 YCoCg Temporal Accumulation**: Color space clipping and AABB box clamping.
- **Camera Reprojection Motion Vectors**: Depth-buffer motion vector synthesis.
- **Geometric Disocclusion**: Depth difference rejection against reprojected history.
- **Screen-Space Ray Tracing (SSRT)**: Screen-space reflections (SSR) and contact ambient occlusion (RTAO).
- **Auto-HDR Inverse Tonemapper**: Extended Reinhard luma compression.

###  Adapters & Optional Vendor Backends
- **NVIDIA DLSS**: Dynamic runtime loader (`nvsdk_ngx`) for Streamline / NGX; falls back to FSR if hardware/SDK unavailable.
- **Intel XeSS**: Dynamic loader for cross-vendor DP4a and XMX machine-learning upscaling.

### 📋 Planned (Architecturally Prepared, Implementation Pending)
- **Linux & Steam Deck**: Vulkan implicit layer (`libomnirender-hook.so`) + DMA-BUF IPC + Vulkan 1.3 daemon backend.
- **macOS Apple Silicon**: Metal 3 HAL backend + `IOSurface` cross-process texture sharing + MetalFX upscaling.
- **Hardware Optical Flow**: Dense motion vector estimation for titles lacking depth buffers.
- **Frame Generation & Low Latency**: Interpolated intermediate frame synthesis and waitable frame pacing.
- **Cross-Platform Control Center**: Avalonia / Slint cross-platform management GUI.

---

## Connected Documentation

- [Central Repository README](README.md)
- [Architecture Guide](ARCHITECTURE.md)
- [Future Work & Roadmap](FUTURE_WORK.md)
- [Build Manual](docs/build.md)
