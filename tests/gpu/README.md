# OmniRender Standalone GPU Integration Test Suite

The GPU Integration Test Suite (`tests/gpu/`) provides a headless test harness for validating OmniRender's hardware abstraction layer, render graph execution, reconstruction backends, staging readback, and temporal history mechanics on real Direct3D 11 GPU hardware and WARP software rasterizers.

---

## Directory Architecture

```text
tests/gpu/
├── CMakeLists.txt              # Test harness build target and CTest registration
├── README.md                   # This documentation
├── gpu_test_host.cpp           # Main test runner executing all verification stages
├── d3d11/
│   ├── D3D11DeviceFixture.h    # Headless D3D11 device and DXGI adapter telemetry
│   └── D3D11DeviceFixture.cpp  # Device initialization with debug layer and WARP fallback
├── fixtures/
│   ├── TestSceneFixtures.h     # Deterministic synthetic test patterns (Color, Depth, Motion, Reactive)
│   └── TestSceneFixtures.cpp   # GPU buffer population and FrameContext initialization
├── readback/
│   ├── GpuReadback.h           # Staging texture allocation and CPU map readback
│   └── GpuReadback.cpp         # Row-pitch aligned VRAM-to-system-memory extraction
└── validation/
    ├── DebugLogger.h           # Dual console and disk file logging facility
    ├── DebugLogger.cpp         # Thread-safe log sink writing omnirender_gpu_test.log
    ├── DiagnosticsReporter.h   # Formatted telemetry, stage timing, and test summary
    ├── DiagnosticsReporter.cpp # Report table and metric formatting
    ├── ImageMetrics.h          # Statistical pixel validation (min, max, mean, variance)
    └── ImageMetrics.cpp        # Quality metric calculations (MSE, PSNR, SSIM)
```

---

## Key Features

1. **Hardware & WARP Headless Execution**
   - Automatically probes for native hardware GPU adapters (NVIDIA, AMD, Intel).
   - Gracefully falls back to Microsoft Basic Render Driver (WARP) in headless virtualization and CI environments.
   - Discovers adapter telemetry: dedicated VRAM, shared memory, device IDs, and feature levels.

2. **Deterministic Synthetic Test Scene**
   - Color: 8-bar calibrated color bars with horizontal gradient bands and crosshairs.
   - Depth: Linear depth ramp with centered spherical depth features.
   - Motion: Smooth non-zero subpixel optical flow vector field.
   - Reactive Mask: Sparse HUD bounding overlay.

3. **GPU Staging Readback & Pixel Validation**
   - Allocates `D3D11_USAGE_STAGING` resources with `D3D11_CPU_ACCESS_READ`.
   - Copies GPU-reconstructed output from VRAM to staging memory.
   - Safely handles row-pitch stride and unpacks into tight CPU pixel arrays.

4. **Statistical & Quality Verification**
   - Pixel luminance analysis: confirms output is non-zero ($> 50\%$) and non-blank.
   - Evaluates Mean Squared Error (MSE), Peak Signal-to-Noise Ratio (PSNR), and Structural Similarity Index (SSIM) against analytic reference buffers.

5. **Temporal History State Machine**
   - Validates state transitions: `Empty` $\to$ `WarmingUp` $\to$ `Valid`.
   - Validates invalidation recovery: `CameraCut` / `ResolutionChanged` $\to$ `Invalidated` $\to$ `Rebuilding` $\to$ `Valid`.

6. **Comprehensive Debug Logging**
   - Emits structured debug logs to both console and `omnirender_gpu_test.log`.
   - Captures stage timings, DirectX HRESULT codes, adapter specifications, and memory allocations.

---

## Running the Harness

### Direct Execution
```powershell
.\build\tests\gpu\Release\gpu_test_host.exe
```

### Via CTest
```powershell
ctest --test-dir build -C Release -R gpu_test_host --output-on-failure
```

### Inspecting Debug Logs
During execution, debug logs are written to `omnirender_gpu_test.log` in the working directory:
```text
[2026-09-07 20:45:00.123] [INFO ] Started OmniRender Standalone GPU Integration Test Suite
[2026-09-07 20:45:00.145] [INFO ] GPU: NVIDIA GeForce RTX 4090, Mode: Hardware, VRAM: 24576.00 MB, Feature: 11.1
[2026-09-07 20:45:00.180] [DEBUG] Generated synthetic scene data (1920x1080, frame 1)
[2026-09-07 20:45:00.210] [INFO ] Successfully populated GPU FrameContext: in=1920x1080, out=2560x1440, frame=1
[2026-09-07 20:45:00.225] [INFO ] [PASS] RenderGraph Dependency Execution (4-pass pipeline executed in order)
[2026-09-07 20:45:00.260] [DEBUG] GpuReadback completed: 2560x1440 (14745600 bytes read, row_pitch=10240)
[2026-09-07 20:45:00.285] [INFO ] Quality [Reconstructed Output]: Mean=112.45, Var=3412.10, NonZero=98.7%, PSNR=32.40 dB, SSIM=0.9124
[2026-09-07 20:45:00.310] [INFO ] [PASS] Temporal History State Machine (Empty -> Warmup -> Valid -> Invalidate -> Recovery)
[2026-09-07 20:45:00.320] [INFO ] Final Summary: Total=7, Passed=7, Failed=0, Result=SUCCESS
```
