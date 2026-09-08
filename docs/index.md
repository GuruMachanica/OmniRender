# OmniRender Documentation

Welcome to the OmniRender project documentation. This site is the central repository of architectural specifications, API references, build manuals, and operational guides.

---

## Quick Links

- **[Quick Start](../README.md#quick-start)** — Building and running in two commands.
- **[Architecture](./architecture.md)** — Process topology, layered abstraction model, zero-copy IPC.
- **[Build Guide](./build.md)** — CMake configuration, prerequisites, and developer setup.
- **[API Reference](./api_reference.md)** — Core interfaces, FrameContext, and IPC specifications.
- **[IPC Contract](./ipc.md)** — Lock-free SPSC ring buffer and memory-mapped file synchronization.
- **[Pipeline Specification](./pipeline_v0.3.md)** — RenderGraph pass sequence and temporal history lifecycle.
- **[Configuration](./configuration.md)** — INI/TOML schema, runtime overrides, and per-game profiles.
- **[QA Acceptance](./qa.md)** — Test suites, benchmarks, and performance criteria.

---

## Project at a Glance

| Property | Value |
|----------|-------|
| **Repository** | `github.com/GuruMachanica/OmniRender` |
| **License** | [GNU General Public License v3.0 (GPLv3)](../LICENSE) |
| **Target OS** | Windows 10/11 · Linux (Vulkan) · macOS (Metal) |
| **Architecture** | In-process proxy DLL + 64-bit daemon + Hardware Abstraction Layer |
| **Code Rules** | $\le$ 300 LOC per file · Zero OS headers in `core/` and `runtime/` |

---

## Module Index

| Module | Location | Purpose |
|--------|----------|---------|
| `core` | [`core/`](../core/README.md) | 100% platform-independent foundation (`FrameContext`, `RenderGraph`, `HistoryManager`) |
| `graphics` | [`graphics/`](../graphics/README.md) | Hardware Abstraction Layer (`IGraphicsDevice`, `ICommandContext`) and D3D11 backend |
| `backends` | [`backends/`](../backends/README.md) | Reconstruction adapters (`IReconstructionBackend` for DLSS, XeSS, FSR, Omni) |
| `runtime` | [`runtime/`](../runtime/README.md) | Pipeline execution and dynamic hardware capability resolver |
| `modules/common` | [`modules/common/`](../modules/common/README.md) | Shared IPC protocol headers and thread-safe logging |
| `modules/hook` | [`modules/hook/`](../modules/hook/README.md) | In-process proxy interceptor DLL (D3D9, OpenGL, DXGI) |
| `modules/daemon` | [`modules/daemon/`](../modules/daemon/README.md) | 64-bit out-of-process rendering daemon host |
| `modules/ui` | [`modules/ui/`](../modules/ui/README.md) | Native Win32 Control Center GUI and automatic game detector |
| `modules/shaders` | [`modules/shaders/`](../modules/shaders/README.md) | Direct3D 11 compute shaders for reconstruction and ray tracing |
| `tests` | [`tests/`](../tests/README.md) | Automated unit tests, GPU harness, and soak tests |

---

## Where to Start

1. New to OmniRender? Read **[`README.md`](../README.md)** and **[`architecture.md`](./architecture.md)**.
2. Building the project? See **[`build.md`](./build.md)**.
3. Writing render passes? Inspect **[`core/graph/RenderGraph.h`](../core/graph/RenderGraph.h)** and **[`runtime/Pipeline.h`](../runtime/Pipeline.h)**.
4. Contributing code? Review **[`CONTRIBUTING.md`](../CONTRIBUTING.md)** and ensure compliance with the $\le$ 300 LOC rule.