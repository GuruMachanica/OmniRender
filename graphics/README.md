# `graphics/` — Graphics Hardware Abstraction Layer (HAL)

The `graphics/` module provides OmniRender's hardware abstraction layer, isolating low-level graphics API implementations behind unified, platform-independent C++ interfaces.

---

## Architecture

```text
graphics/
├── abstraction/                ← Pure Platform-Independent Interfaces
│   ├── IGraphicsDevice.h       # Factory for textures, buffers, contexts
│   ├── IGraphicsTexture.h      # Abstract texture interface
│   ├── IGraphicsBuffer.h       # Abstract buffer interface
│   └── ICommandContext.h       # Unified command dispatch (Dispatch, Copy, Bind)
├── d3d11/                      ← Windows Direct3D 11 Implementation
│   ├── D3D11GraphicsDevice     # Concrete ID3D11Device wrapper
│   ├── D3D11GraphicsTexture    # Concrete ID3D11Texture2D + SRV + UAV wrapper
│   ├── D3D11GraphicsBuffer     # Concrete ID3D11Buffer wrapper
│   ├── D3D11CommandContext     # Concrete ID3D11DeviceContext dispatcher
│   └── D3D11TypeConversions    # TextureFormat <-> DXGI_FORMAT translation
├── vulkan/                     ← (Planned: Linux & Windows Vulkan 1.3)
└── metal/                      ← (Planned: macOS Apple Silicon Metal)
```

---

## Design Principles

1. **Clean Separation**: Core and Runtime depend exclusively on `graphics/abstraction/`.
2. **Zero-Copy Shared Surfaces**: Backends implement `OpenSharedTexture` to ingest cross-process GPU memory without CPU or VRAM copy penalties.
3. **Multi-API Ready**: New backends (Vulkan, Metal, D3D12) drop into `graphics/<api>/` by implementing `IGraphicsDevice`, `IGraphicsTexture`, and `ICommandContext`.

---

## Connected Documentation

- [Central Repository README](../README.md)
- [Core Architecture](../core/README.md)
- [Runtime Orchestration](../runtime/README.md)
- [Upscaler Backends](../backends/README.md)
