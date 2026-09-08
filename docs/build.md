# OmniRender Build & Packaging Guide

This guide covers developer prerequisites, CMake build options, IDE integration, cross-platform builds, and automated CI/CD release workflows.

---

## 1. Prerequisites

### Windows (Full Pipeline & Presentation)
| Tool | Minimum Version | Notes |
|---|---|---|
| **Windows 10/11 SDK** | 10.0.22621.0+ | DXGI 1.4, Direct3D 11 headers |
| **Visual Studio 2022** | 17.6+ (v143) | "Desktop development with C++" workload |
| **CMake** | 3.20+ | `winget install Kitware.CMake` |
| **Git** | 2.40+ | For cloning and version tagging |
| **DXC / FXC** | 1.7+ | Shader compilation |

### Linux (Platform-Independent Core & Runtime)
| Tool | Minimum Version | Notes |
|---|---|---|
| **GCC or Clang** | GCC 11+ / Clang 14+ | C++20 standard support required |
| **CMake** | 3.20+ | `sudo apt-get install cmake g++` |

---

## 2. Command-Line Build (Windows)

```bat
:: Open a "x64 Native Tools Command Prompt for VS 2022"
git clone https://github.com/GuruMachanica/OmniRender.git
cd OmniRender

:: Configure with UI, Tests, and Shaders
cmake -B build -A x64 ^
    -DOMNIRENDER_BUILD_UI=ON ^
    -DOMNIRENDER_BUILD_HOOK=ON ^
    -DOMNIRENDER_BUILD_DAEMON=ON ^
    -DOMNIRENDER_BUILD_TESTS=ON ^
    -DCMAKE_BUILD_TYPE=Release

:: Compile all targets in parallel
cmake --build build --config Release --parallel

:: Run automated test suite
ctest --test-dir build -C Release --output-on-failure
```

---

## 3. Platform-Independent Build (Linux)

```bash
# Build the core library, graphics abstraction, and runtime orchestrator
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target OmniRenderCore OmniRenderRuntime --parallel
```

---

## 4. CMake Options Reference

| Option | Default (Win) | Default (Linux) | Description |
|---|---|---|---|
| `OMNIRENDER_BUILD_HOOK` | `ON` | `OFF` | Build in-process interceptor proxy (`omnirender-hook.dll`) |
| `OMNIRENDER_BUILD_DAEMON` | `ON` | `OFF` | Build 64-bit out-of-process daemon (`OmniRenderDaemon.exe`) |
| `OMNIRENDER_BUILD_UI` | `ON` | `OFF` | Build native Win32 Control Center (`OmniRenderControlPanel.exe`) |
| `OMNIRENDER_BUILD_TESTS` | `OFF` | `OFF` | Build automated test suite |
| `OMNIRENDER_BUILD_TEST_HOST` | `OFF` | `OFF` | Build standalone Direct3D 11 test host renderer |
| `OMNIRENDER_USE_NGX` | `OFF` | `OFF` | Link against NVIDIA NGX / Streamline SDK |

---

## 5. Build Artifacts & Distribution Packaging

When built with `Release` configuration, the following binaries are generated:

```text
build/
├── modules/
│   ├── ui/Release/OmniRenderControlPanel.exe     # One-click desktop launcher
│   ├── daemon/Release/OmniRenderDaemon.exe       # 64-bit reconstruction daemon
│   └── hook/Release/omnirender-hook.dll          # In-process proxy DLL
└── shaders/*.cso                                 # Precompiled Direct3D 11 shaders
```

### Official Release Bundle (`OmniRender-Windows-x64.zip`)
Automated packaging bundles the following structure:
```text
OmniRender-Windows-x64/
├── OmniRenderControlPanel.exe
├── OmniRenderDaemon.exe
├── omnirender-hook.dll
├── README.md
├── LICENSE (GPLv3)
├── THIRD_PARTY_LICENSES.md
└── shaders/
    ├── upscale_hlsl.cso
    ├── reconstruct_hlsl.cso
    ├── reactive_mask_hlsl.cso
    └── passthrough_ps.cso
```

---

## 6. Continuous Integration & Release Automation

- **[`ci.yml`](../.github/workflows/ci.yml)**: Validates multi-configuration builds (Debug & Release) on Windows and runs tests; builds `OmniRenderCore` on Ubuntu.
- **[`build-release.yml`](../.github/workflows/build-release.yml)**: Produces production `OmniRender-Windows-x64.zip` and attaches it to GitHub Releases upon pushing a `v*` tag.