# `modules/shaders` — HLSL Compute Shaders

DirectX 11 / 12 compute shaders used by the OmniRender daemon. All
shaders are compiled at build time with `dxc.exe`; the compiled DXIL
blobs are **not** committed to the repository.

## Files

| File | Used by | Purpose |
|------|---------|---------|
| `upscale_hlsl.hlsl` | `modules/daemon/processing.cpp` | Bounded spatial upscale for the v0.3.0-alpha pipeline. Operates on the clamped working resolution. |
| `reconstruct_hlsl.hlsl` | `modules/daemon/processing.cpp` | Temporal reconstruction placeholder for future history + motion resolve. |
| `depth_linearize_hlsl.hlsl` | `modules/daemon/processing.cpp` | Depth linearisation placeholder. Passes the color buffer through until the D3D9/DXGI depth source is plumbed in. |
| `auto_hdr_tonemap.hlsl` | `modules/daemon/tonemap_compute.cpp` | Compute-shader fallback for HDR / inverse tone mapping. |

## Build-time compilation

When `dxc.exe` is available on the build machine, the daemon CMake target
compiles every `.hlsl` in this directory to `.cso` at build time:

```bat
dxc -T cs_6_5 -E CSMain ^
    -O3 -Qstrip_debug ^
    modules/shaders/upscale_hlsl.hlsl ^
    -Fo build/modules/daemon/shaders/upscale_hlsl.cso
```

In a stock install without DXC/CSO artifacts, the daemon runs in a
bounded passthrough mode so the build still succeeds.

## VRAM policy

These shaders are designed for a **bounded internal resolution**. The
daemon does not allocate a full-resolution copy of every source frame.
It:
1. Clamps the internal work resolution.
2. Reuses one work texture + small constant buffers per frame.
3. Keeps a bounded history for reconstruction if enabled.

## Profile constraints

- **Shader model:** `cs_6_5` when available.
- **Resource binding:** `t0..t3` for SRVs, `u0` for UAVs, `b0` for
  constant buffers.
- **Thread group size:** 16×16×1 unless otherwise noted.

## Files added in v0.3.0-alpha

- `upscale_hlsl.hlsl` — bounded spatial upscale path
- `reconstruct_hlsl.hlsl` — temporal reconstruction placeholder
- `passthrough_ps.hlsl` — minimal overlay pixel shader for passthrough/presentation

## See also

- [Central Repository README](../../README.md)
- [`modules/daemon/README.md`](../daemon/README.md)
- [`docs/architecture.md`](../../docs/architecture.md)
- [`docs/pipeline_v0.3.md`](../../docs/pipeline_v0.3.md)
