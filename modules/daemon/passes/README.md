# `modules/daemon/passes` — Modular Post-Processing & Render Passes

This directory contains the individual modular post-processing passes executed by `OmniRenderDaemon` within the frame reconstruction pipeline. Decomposing these passes into dedicated sub-files enforces the strict $\le$ 300 LOC limit while maintaining clean separation of concerns and high cache locality.

---

## Subfolder Structure & Files

| Pass File | Header | Role |
|-----------|--------|------|
| [`motion_pass.cpp`](./motion_pass.cpp) | [`motion_pass.h`](./motion_pass.h) | **Motion Vector Synthesis**: Synthesizes screen-space optical flow motion vectors from consecutive color/depth frames using DIS block matching compute shaders. |
| [`disocclusion_pass.cpp`](./disocclusion_pass.cpp) | [`disocclusion_pass.h`](./disocclusion_pass.h) | **Disocclusion Mask Generation**: Detects newly exposed geometry and depth discontinuities across temporal frames to prevent ghosting during temporal upscaling. |
| [`raytrace_pass.cpp`](./raytrace_pass.cpp) | [`raytrace_pass.h`](./raytrace_pass.h) | **Ray-Traced Effects**: Computes Screen-Space Ray-Traced Reflections (SSR) and Ambient Occlusion (RTAO) using linearized depth and surface normals. |
| [`tonemap_pass.cpp`](./tonemap_pass.cpp) | [`tonemap_pass.h`](./tonemap_pass.h) | **Auto-HDR & Tonemapping**: Dispatches HDR inverse tonemapping, paper-white luminance expansion, and ACES filmic grading compute shaders. |

---

## Pass Execution Order in `processing.cpp`

```text
Source Color / Depth (Shared DXGI Surface)
                  
                  
          1. Linearize Depth
                  
                  
        2. Motion Vector Pass (motion_pass)
                  
                  
    3. Disocclusion Detection (disocclusion_pass)
                  
                  
        4. Upscaling / Reconstruction (FSR / DLSS / XeSS)
                  
                  
     5. Screen-Space Ray Tracing (raytrace_pass)
                  
                  
       6. Auto-HDR Tonemapping (tonemap_pass)
                  
                  
    Presentation Overlay (presentation_win)
```

---

## Reliability & Degradation

Every pass adheres to OmniRender's bounded fallbacks:
- If a compute shader is unavailable or fails compilation, the pass safely bypasses without crashing.
- Shaders and resources are allocated once during session initialization and reused across frames with zero heap allocations in the hot rendering loop.

---

## Connected Documentation

- [Daemon Subsystem README](../README.md)
- [Upscaler Submodules](../upscalers/README.md)
- [Shader Kernel Implementations](../../shaders/README.md)
- [Root Documentation](../../../README.md)
