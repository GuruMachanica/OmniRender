# `tests/` — OmniRender test suite

## Automated Test Suite
 
| Test | What it covers |
|------|----------------|
| `test_core_platform_neutral` | 100% platform-independent unit test suite (Linux, macOS, Windows) validating Core data structures, RenderGraph prerequisites, HistoryManager state machine, and Runtime BackendResolver. |
| `test_core_d3d11_execution` | Direct3D 11 hardware integration test validating D3D11 device/textures, FrameContext binding, RenderGraph execution, DLSS/Reconstruction staging texture readback (2560x1440 non-zero image verification), and HistoryManager rotation/invalidation lifecycle. |
| [`gpu_test_host`](./gpu/README.md) | Standalone modular GPU integration harness (`tests/gpu/`) with hardware/WARP adapter telemetry, synthetic scene fixtures, staging readback, MSE/PSNR/SSIM quality metrics, debug logs (`omnirender_gpu_test.log`), and temporal state validation. |
| `test_ring_buffer` | SPSC ring state machine — validates single-producer single-consumer slot states (`Free`, `Captured`, `Ready`, `Processing`) and that producers only reclaim `Free` slots. |
| `test_cso_compile` | Validates shader compilation bytecodes and reflections. |
| `test_halton` | Validates Halton(2,3) low-discrepancy temporal jitter sequence math. |
| `test_config` | Configuration file parsing (`omnirender.ini`), defaults, and environment overrides. |
| `test_capability_matrix` | Hardware detection, vendor ID probes, and API negotiation matrix. |
| `test_opengl_capture` | OpenGL offscreen context creation, `WGL_NV_DX_interop2` resolution, zero-copy handle export, and IPC payload verification. |
| `test_dlss_gpu` | NVIDIA DLSS Super Resolution GPU integration harness, feature lifecycle, and device recovery. |
| [`OmniRenderSoak`](./soak/README.md) | Automated multi-minute soak and stress harness monitoring process health and memory working-sets. |

## Running

### Windows (Full Suite)
```bat
:: From a Visual Studio developer prompt
cmake -B build -A x64 -DOMNIRENDER_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Linux / macOS (Platform-Neutral Core Tests)
```bash
cmake -B build -DOMNIRENDER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target test_core_platform_neutral --parallel
ctest --test-dir build -R core_platform_neutral --output-on-failure
```

## Adding a new test

1. Create `tests/test_<name>.cpp`.
2. Add it to `tests/CMakeLists.txt` as a new `add_executable(...)`.
3. Add a matching `add_test(NAME <name> COMMAND test_<name>)`.

Tests should not require GPU unless marked as hardware integration tests.

---

## Connected Documentation

- [Central Repository README](../README.md)
- [Architecture Overview](../ARCHITECTURE.md)
