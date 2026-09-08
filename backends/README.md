# `backends/` — Hardware & Reconstruction Adapters

The `backends/` directory houses the modular rendering and reconstruction adapters for OmniRender.

---

## Architecture

```text
backends/
└── reconstruction/
    ├── IReconstructionBackend.h # Common API-neutral interface for scaling engines
    ├── dlss/                    # NVIDIA DLSS Super Resolution adapter
    ├── fsr/                     # AMD FidelityFX Super Resolution adapter
    ├── xess/                    # Intel XeSS machine learning adapter
    └── omni/                    # Native YCoCg temporal accumulation pipeline
```

---

## Extensibility

All reconstruction providers implement `IReconstructionBackend`. The runtime selects the best available engine at runtime based on `core::RuntimeCapabilities` and user preferences.

---

## Connected Documentation

- [Central Repository README](../README.md)
- [Core Architecture](../core/README.md)
- [Graphics HAL](../graphics/README.md)
- [Runtime Engine](../runtime/README.md)
