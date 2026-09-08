# `modules/daemon/upscalers` — Resolution Scaling & Reconstruction Backends

This directory contains the upscaling and temporal reconstruction integrations used by the OmniRender daemon. OmniRender supports multi-vendor hardware-accelerated upscalers as well as zero-dependency spatial compute fallbacks.

---

## Subfolder Structure & Files

| Upscaler Submodule | Implementation | Role | Hardware Requirements |
|-------------------|----------------|------|-----------------------|
| **AMD FSR** | [`spatial_fallback.cpp`](./spatial_fallback.cpp)<br>[`spatial_fallback.h`](./spatial_fallback.h) | **AMD FidelityFX Super Resolution (FSR 4.1 / 1.0)**:<br>Dual-pass Edge-Adaptive Spatial Upsampling (EASU) and Robust Contrast-Adaptive Sharpening (RCAS). | Universal (any Direct3D 11 compatible GPU: AMD, NVIDIA, Intel). |
| **NVIDIA DLSS** | [`dlss_pipeline.cpp`](./dlss_pipeline.cpp)<br>[`dlss_pipeline.h`](./dlss_pipeline.h) | **NVIDIA Deep Learning Super Sampling (DLSS 5 / 3.x)**:<br>AI-driven temporal reconstruction using NVIDIA Streamline interposer and NGX SDK. | NVIDIA RTX GPUs with Tensor Cores (RTX 20, 30, 40, 50 series). |
| **Intel XeSS** | [`xess_pipeline.cpp`](./xess_pipeline.cpp)<br>[`xess_pipeline.h`](./xess_pipeline.h) | **Intel Xe Super Sampling (XeSS)**:<br>Deep learning reconstruction with DP4a (universal fallback) and Intel XMX matrix acceleration. | Universal (Intel Arc GPUs, AMD RDNA/GCN, NVIDIA GTX/RTX with DP4a support). |

---

## Automatic Capability Negotiation

The `hw_negotiator.cpp` component queries adapter capabilities at daemon startup to automatically select the optimal upscaler backend:

1. **NVIDIA RTX Detection**: If an NVIDIA GPU with $\ge$ 3.5 GB VRAM is detected, DLSS reconstruction is preferred.
2. **Intel Arc / DP4a Detection**: If Intel Arc or DP4a capability is available, XeSS provides accelerated ML reconstruction.
3. **Universal Fallback**: If vendor-specific SDKs are missing or unsupported, AMD FSR / CAS spatial compute scaling executes with 100% reliability.

---

## Connected Documentation

- [Daemon Subsystem README](../README.md)
- [Render Passes Architecture](../passes/README.md)
- [Root Documentation](../../../README.md)
