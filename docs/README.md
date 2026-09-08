# `docs/` — OmniRender Documentation

Welcome to the comprehensive technical documentation and architecture guides for the OmniRender project.

---

## Documentation Directory

| Document | Description |
|----------|-------------|
| [`index.md`](./index.md) | Documentation index and navigation map. |
| [`architecture.md`](./architecture.md) | High-level system architecture, process boundaries, zero-copy handle sharing, and concurrency model. |
| [`build.md`](./build.md) | Detailed build prerequisites, CMake flags, Visual Studio configuration, and developer environment setup. |
| [`api_reference.md`](./api_reference.md) | Internal and cross-process C++ API specifications, class interfaces, and lifecycle hooks. |
| [`ipc.md`](./ipc.md) | Inter-process communication protocol specification, memory layouts, ring buffer synchronization, and named objects. |
| [`pipeline_v0.3.md`](./pipeline_v0.3.md) | Complete multi-stage rendering pipeline specification (motion vectors, disocclusion, upscaling, tonemapping). |
| [`configuration.md`](./configuration.md) | INI configuration schema (`omnirender.ini`), per-game settings, CLI flags, and environment overrides. |
| [`roadmap_v0.3.md`](./roadmap_v0.3.md) | Milestone roadmap, version progression (v0.3.0 through v1.0.0), and planned graphics backends. |
| [`3rd_party_deps.md`](./3rd_party_deps.md) | Third-party SDK integration notes (NVIDIA Streamline / NGX, Intel XeSS, AMD FSR, DirectX). |
| [`qa.md`](./qa.md) | Quality assurance guidelines, performance benchmarks, soak test methodology, and acceptance criteria. |
| [`packages.md`](./packages.md) | Packaging and distribution instructions for releases and zip bundles. |
| [`AUDIT_TODO.md`](./AUDIT_TODO.md) | Technical debt tracking, audit backlog, and maintenance tasks. |

---

## Subdirectories

- [`profiles/`](./profiles/skyrim.toml) — Game-specific configuration profiles (TOML format) tuning injection methods and depth heuristics.
- [`assets/`](./assets/logo.jpg) — Graphical assets, diagrams, and branding resources.

---

## Connected Documentation

- [Central Repository README](../README.md)
- [Core Architecture](../core/README.md)
- [Graphics HAL](../graphics/README.md)
- [Runtime Engine](../runtime/README.md)
- [Upscaler Backends](../backends/README.md)
- [Daemon Subsystem](../modules/daemon/README.md)
- [Hook Subsystem](../modules/hook/README.md)
- [Control Center UI](../modules/ui/README.md)
- [Testing Suite](../tests/README.md)
