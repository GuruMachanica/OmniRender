# `core/` — 100% Platform-Independent Core

The `core/` module forms the architectural heart of OmniRender. It is **100% platform-independent and graphics-API-neutral**.

---

## Strict Rules

1. **Zero OS Headers**: No `#include <windows.h>`, `<d3d11.h>`, `<dxgi.h>`, `<vulkan/vulkan.h>`, `<Metal/Metal.h>`.
2. **Zero Platform Ifdefs**: No `#ifdef _WIN32`, `#ifdef __linux__`, `#ifdef __APPLE__`.
3. **Strict Size Constraint**: Every file must strictly obey the $\le 300$ LOC limit.
4. **Abstract GPU Resources**: Core interacts exclusively with `GpuTexture`, `GpuBuffer`, and the `graphics::` Hardware Abstraction Layer.

---

## Layout

```text
core/
├── capability/
│   ├── Platform.h             # Platform enum (Windows, Linux, MacOS)
│   ├── GraphicsApi.h          # GraphicsApi enum (D3D9, D3D11, D3D12, Vulkan, Metal, OpenGL)
│   └── RuntimeCapabilities.h  # Hardware capability flags (compute, fp16, ray tracing)
├── resources/
│   ├── TextureFormat.h        # Unified cross-API pixel format enumeration
│   ├── TextureDesc.h          # Dimensions, mip levels, bind and transfer flags
│   ├── BufferDesc.h           # Byte widths, structured/constant buffer usage
│   ├── ResourceHandle.h       # Generational abstract resource identifiers
│   ├── GpuTexture.h           # Type-safe handle wrapping IGraphicsTexture
│   └── GpuBuffer.h            # Type-safe handle wrapping IGraphicsBuffer
├── frame/
│   ├── Resolution.h           # Width, height, aspect ratio helpers
│   ├── FrameTiming.h          # Frame indices, delta times, microsecond timestamps
│   ├── FrameValidity.h        # Availability flags for color, depth, motion, history
│   └── FrameContext.h         # Central API-neutral frame state structure
├── camera/
│   └── CameraState.h          # View/Projection matrices and jitter compensation
├── temporal/
│   ├── JitterState.h          # Halton(2,3) low-discrepancy subpixel jitter generator
│   ├── HistoryState.h         # Temporal accumulation states and invalidation reasons
│   └── HistoryManager.h       # Platform-independent temporal history state machine
└── graph/
    ├── RenderGraphTypes.h     # PassType, ResourceAccess, PassNode descriptors
    └── RenderGraph.h          # Directed acyclic graph execution engine
```

---

## Connected Documentation

- [Central Repository README](../README.md)
- [Graphics Abstraction](../graphics/README.md)
- [Runtime Orchestration](../runtime/README.md)
- [Upscaler Backends](../backends/README.md)
