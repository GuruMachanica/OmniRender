# `modules/daemon` — 64-bit Processing Host

`OmniRenderDaemon` is the **out-of-process** half of OmniRender. It
runs as a 64-bit Windows executable, opens the shared memory and DXGI
handles published by [`modules/hook`](../hook/README.md), and runs the
full reconstruction pipeline: motion vector synthesis → temporal
upscaling (DLSS or spatial fallback) → neural inverse tone mapping →
borderless overlay presentation.

## Files & Submodules

| File / Subfolder | Role |
|------------------|------|
| [`main.cpp`](./main.cpp) | `wWinMain` entry. Calls negotiator → IPC server → presentation loop. |
| [`hw_negotiator.cpp`](./hw_negotiator.cpp) | DXGI adapter probe. Picks `PROFILE_RTX_TEMPORAL` (NVIDIA, ≥ 3.5 GB) or `PROFILE_SPATIAL_FALLBACK`. PRD §7.3. |
| [`ipc_server.cpp`](./ipc_server.cpp) | Ring-buffer consumer. Blocks on the frame-ready event. |
| [`interop_d3d11.cpp`](./interop_d3d11.cpp) | `ID3D11Device::OpenSharedResource` consumer. Zero PCIe copy. |
| [`processing.cpp`](./processing.cpp) | Orchestrator for post-processing and render passes. Strictly $\le$ 300 LOC. |
| [`shader_loader.cpp`](./shader_loader.cpp) / [`shader_loader.h`](./shader_loader.h) | Shader bytecode discovery, loading, and reflection helpers. |
| [`passes/`](./passes/README.md) | **Modular Render Passes**: [`motion_pass`](./passes/motion_pass.cpp), [`disocclusion_pass`](./passes/disocclusion_pass.cpp), [`raytrace_pass`](./passes/raytrace_pass.cpp), and [`tonemap_pass`](./passes/tonemap_pass.cpp). See [Passes README](./passes/README.md). |
| [`upscalers/`](./upscalers/README.md) | **Upscaling Backends**: [`dlss_pipeline`](./upscalers/dlss_pipeline.cpp) (DLSS 5/3.x), [`spatial_fallback`](./upscalers/spatial_fallback.cpp) (AMD FSR 4.1/1.0), and [`xess_pipeline`](./upscalers/xess_pipeline.cpp) (Intel XeSS). See [Upscalers README](./upscalers/README.md). |
| [`presentation_win.cpp`](./presentation_win.cpp) | Borderless flip-model top-level window. FR-5.1 / FR-5.2. |
| [`test_host.cpp`](./test_host.cpp) | Synthetic test host executable for end-to-end capture and injection verification. |
| [`test_host_renderer.cpp`](./test_host_renderer.cpp) / [`test_host_math.h`](./test_host_math.h) | D3D11 render loop and math helpers extracted from test_host to adhere to $\le$ 300 LOC. |

## Profile gating

```cpp
if (isNvidia && vram_mb >= 3500) {
    return PROFILE_RTX_TEMPORAL;       // DLSS + optical flow + TensorRT
}
return PROFILE_SPATIAL_FALLBACK;       // FSR 1 / CAS + DirectML HDR
```

## Per-frame pipeline

```text
ConsumeFrame()  OpenSharedResource(color)  depth_linearize.hlsl (when available)
                  OpenSharedResource(depth)  bounded upscale/reconstruct
                                              tonemap compute fallback (when available)
                                              presentation_win
```

The default v0.3.0-alpha pipeline is built to stay VRAM-bounded:

- The daemon does **not** allocate a full-resolution copy of every source
  frame. It clamps the internal working resolution, reuses one work
  texture, and keeps a small bounded history for reconstruction.
- External SDKs (NGX / TensorRT) remain optional and are still deferred.
  In a stock install the daemon runs a plugin-free compute path, or a
  bounded passthrough path when the compute shaders are not compiled in.
- If processing allocation or a single frame fails, the daemon degrades
  to passthrough and keeps running.

## Building

```bat
cmake -B build -A x64 -DOMNIRENDER_BUILD_DAEMON=ON
cmake --build build --config Release --target OmniRenderDaemon
```

Output: `build\modules\daemon\Release\OmniRenderDaemon.exe`

## Optional SDK linkage

| CMake option | Library | Effect |
|--------------|---------|--------|
| `-DOMNIRENDER_USE_NGX=ON` | `sl.interposer`, `sl.d3d11`, `ngx_d3d11` | Enables optional DLSS path later |
| `-DOMNIRENDER_USE_TRT=ON` | `nvinfer`, `nvonnxparser`, `cudart` | Enables optional neural tonemap later |

When neither option is set, the daemon uses the bounded compute path.
The build still succeeds on a stock Visual Studio install.

## Dependencies

| Library | Why |
|---------|-----|
| `d3d11`, `dxgi`, `dxguid` | D3D11 device + DXGI shared resources + presentation |
| `user32`, `gdi32` | Overlay window creation + layered window attributes |
| `omnirender::common` | Logger + IPC struct |

Shader sources live under `modules/shaders/`. They are compiled to CSO
when `dxc.exe` is available; otherwise the daemon runs in bounded
passthrough mode.

## Crash isolation (NFR-2)

The daemon runs in a **separate process** from the game. A TDR or any
GPU fault inside the daemon will only take down `OmniRenderDaemon.exe`;
the legacy game process continues running. The hook detects the
daemon's absence (event handle close) and reverts to passthrough
within one frame.

## See also

- [Central Repository README](../../README.md)
- [`docs/architecture.md`](../../docs/architecture.md)
- [`docs/api_reference.md`](../../docs/api_reference.md)
- [`docs/qa.md`](../../docs/qa.md)
- [`docs/pipeline_v0.3.md`](../../docs/pipeline_v0.3.md)