# `runtime/` — Platform-Independent Runtime & Orchestrator

The `runtime/` module coordinates execution of OmniRender's modular graphics pipeline, resolving capabilities and executing render passes against abstract hardware contexts.

---

## Architecture

```text
runtime/
├── Pipeline.h          # API-neutral multi-stage reconstruction pipeline
├── Pipeline.cpp        # RenderGraph compilation & frame commitment
├── BackendResolver.h   # Dynamic capability-based upscaler selector
└── BackendResolver.cpp # Resolution logic matching GPU capabilities
```

---

## Design Principles

1. **API Independence**: Zero direct calls to Direct3D, Vulkan, or Metal.
2. **Dynamic Adaptation**: Leverages `BackendResolver` to gracefully negotiate between DLSS, XeSS, FSR, and native OmniRender reconstruction based on runtime hardware capabilities.
3. **Deterministic Pass Graph**: Compiles explicit prerequisite and resource access masks to ensure safe multi-stage execution and valid temporal history accumulation.

---

## Connected Documentation

- [Central Repository README](../README.md)
- [Core Architecture](../core/README.md)
- [Graphics HAL](../graphics/README.md)
- [Upscaler Backends](../backends/README.md)
