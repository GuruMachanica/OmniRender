# Pipeline — v0.3.0-alpha

This documents the bounded reconstruction and upscale path added in
v0.3.0-alpha. It replaces the earlier "DLSS-only" mental model with a
default path that works without external SDKs and does not blow VRAM.

## Intent

The biggest mistake an early renderer can make is to assume every frame
can be processed at full internal resolution with unbounded temporaries.
OmniRender instead:

- captures once via the hook
- keeps a bounded internal work resolution
- reuses one work texture per frame
- keeps a small bounded history for reconstruction
- upscales only if the user requested output differs from the working
  resolution
- degrades to passthrough if processing allocation fails

## Data flow

```
hook
  ↓
D3D9/DXGI capture → shared color + shared depth
  ↓
IPC ring (Global\OmniRender_IPC_Block)
  ↓
daemon
  ↓
OpenSharedResource(color)
OpenSharedResource(depth)
  ↓
FenceWait
  ↓
PrepareFrame
   choose bounded working resolution
   import source as SRV
   downscale/copy into work texture
   reconstruction (history ping-pong if enabled)
   upscale toward target if requested
  ↓
PresentProcessed → overlay swapchain
```

## VRAM budget

- One shared color handle and one shared depth handle come from the hook.
- The daemon adds a bounded work texture plus small constant buffers.
- If reconstruction is enabled, it keeps a small bounded history.
- If the source is very large, the daemon clamps internal resolution
  instead of allocating more VRAM.

This is deliberate: the first usable product is one that runs on a wide
range of GPUs without turning the daemon into a VRAM hog.

## Compute shaders

The bounded path now has shader sources for:

- `modules/shaders/depth_linearize_hlsl.hlsl` — depth linearization
- `modules/shaders/upscale_hlsl.hlsl` — Lanczos-style bicubic upscale
- `modules/shaders/reconstruct_hlsl.hlsl` — basic temporal blend + disagreement mask
- `modules/shaders/auto_hdr_tonemap.hlsl` — lightweight HDR/AGX-style tonemap
- `modules/shaders/passthrough_ps.hlsl` — minimal overlay pixel shader

When `dxc.exe` is available, the daemon CMake target compiles these to
CSO and the pipeline can use them. When it is not, the daemon builds and
runs in bounded passthrough mode.

## External SDKs

DLSS, FSR, XeSS, and TensorRT are still deferred in the default path.
The v0.3.0-alpha pipeline is plugin-free and compute-based. Optional SDK
linkage can be added later and swapped in via the capability layer.

## Passthrough degradation

- If processing buffers cannot be created, the daemon falls back to
  passthrough instead of failing the whole session.
- If a frame encounters a pipeline error, the daemon falls back to
  passthrough for that frame and keeps running.
- If depth is missing, the pipeline still runs without it.
- If the daemon cannot import a shared handle, it logs and skips that
  frame.
- If the compute shaders are not compiled in, the daemon still builds and
  presents the captured frames, just without the compute processing path.

## Configuration knobs

| Flag | Meaning |
|------|---------|
| EnableUpscale | Enable upscale toward target_width/target_height |
| EnableReconstruction | Enable temporal history + resolve pass |
| EnableTonemap | Enable HDR/tone pass if shader loaded |

These flags travel in IPC `flags` so the hook and daemon stay in sync
without a separate control channel.

## Current status

As of the current scaffold, the bounded path is real code, not a doc-only
idea.

 - hook-side capture already carries color + depth handles to the daemon
 - the daemon now has a real bounded processing layer with its own header
 - the daemon has a real pipeline orchestrator that can choose passthrough
   or bounded processing per frame
 - the shader sources for depth linearization, upscale, reconstruction,
   and HDR tone fallback exist and are designed for bounded VRAM usage

## Architectural Evolution

The pipeline has evolved into a 100% platform-independent architecture:
- **`core::RenderGraph`**: API-neutral directed acyclic graph managing execution passes.
- **`core::FrameContext`**: API-neutral payload with abstract `GpuTexture` handles.
- **`core::HistoryManager`**: Multi-slot temporal accumulation state machine managing history textures and previous depth.
- **`backends::IReconstructionBackend`**: Hardware-agnostic interface wrapping NVIDIA DLSS, Intel XeSS, AMD FSR, and native OmniRender reconstruction.
- **`runtime::Pipeline`**: Multi-stage pass orchestrator compiling and executing passes against the `graphics::ICommandContext` abstraction.
