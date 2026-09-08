# Build and runtime dependencies — extended pipeline

This note supplements the main build docs for the v0.3.0-alpha pipeline
additions.

## What changed

The daemon now has a bounded plugin-free processing path behind:

- `modules/daemon/processing.cpp` and `processing.h`
- `modules/daemon/pipeline.cpp` and `pipeline.h`
- `modules/daemon/tonemap_compute.cpp` and `tonemap_compute.h`
- `modules/shaders/upscale_hlsl.hlsl`
- `modules/shaders/reconstruct_hlsl.hlsl`
- `modules/shaders/depth_linearize_hlsl.hlsl`
- `modules/shaders/auto_hdr_tonemap.hlsl`
- `modules/shaders/passthrough_ps.hlsl`
- `modules/common/pipeline_config.h`

## What you need to build it

### Always required

- Windows 10/11 SDK
- Visual Studio 2022 with the C++ desktop workload
- CMake 3.20+
- A C++17 compiler (MSVC v143 on Windows)

### Optional but recommended for the shader path

- DirectX Shader Compiler (`dxc.exe`)
- Windows SDK DXIL/compiler components, if your installer did not include them

If `dxc.exe` is available when CMake configures the daemon, the daemon
build will compile the bounded compute shaders to CSO and use them. If
it is not available, the daemon builds and runs in bounded passthrough
mode.

### Still deferred

The following are not required for the v0.3.0-alpha pipeline and remain
optional future backends:

- NVIDIA NGX / Streamline
- NVIDIA TensorRT + CUDA
- AMD FSR / CAS SDKs
- OpenGL or Vulkan capture layers

## Runtime behavior

- The daemon still starts without any external SDK.
- If processing buffers cannot be created, it runs in passthrough.
- If a single frame fails in the pipeline, it falls back to passthrough
  for that frame and keeps running.
- Depth is optional; missing depth does not fail the whole frame.
- VRAM usage is bounded by clamping the internal working resolution.

## Recommended install order

1. Install Visual Studio 2022 with Desktop C++.
2. Install CMake.
3. If you want the compute path, install DXC and ensure `dxc.exe` is on
   the PATH used by CMake.
4. If you want optional vendor backends later, install those SDKs and
   enable them through CMake options.

## Verification

The easiest first verification is still the unit test:

```bat
cmake -B build -A x64 -DOMNIRENDER_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For the daemon path, the main sanity check is:

- daemon starts
- IPC server attaches
- processing initializes (or degrades to passthrough cleanly)
- the overlay window appears when a hooked game presents frames

Full end-to-end verification still requires a hooked game or a future
test host.
