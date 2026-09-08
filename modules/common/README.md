# `modules/common` — Shared Foundation

Header-only utility code shared by **both** the hook and the daemon.

| File | Purpose |
|------|---------|
| [`ipc_protocol.h`](./ipc_protocol.h) | `OmniRenderIPCFrameData` struct + named-object constants. Byte-for-byte compatible between hook and daemon. |
| [`logging.h`](./logging.h) | Thread-safe `OMNI_LOG_*` macros. Writes to stderr **and** `OutputDebugStringA`. |
| [`nvsdk_ngx/`](./nvsdk_ngx/README.md) | Canonical NVIDIA NGX dynamic runtime ABI definitions for DLSS super resolution. |

## Layout

```cpp
// From any module
#include "common/logging.h"   // OMNI_LOG_INFO, OMNI_LOG_WARN, OMNI_LOG_ERROR
#include "common/ipc_protocol.h"  // omnirender::OmniRenderIPCFrameData
```

## Why header-only?

- The hook DLL must keep its working set under 50 MB (FR-1.2). A
  compiled static library adds IAT entries we don't need.
- The daemon and the hook are versioned together — every IPC change
  must be coordinated, so a single header is the contract.

## Build

This directory is a CMake **INTERFACE** library; it has no sources of
its own. The root `CMakeLists.txt` calls `add_subdirectory(modules/common)`
and exposes the alias `omnirender::common`. Consumers link with:

```cmake
target_link_libraries(<your-target> PRIVATE omnirender::common)
```

The alias provides the `include/` path and C++17 requirement, so
`#include "common/logging.h"` works from any sibling module.

## Stability

Anything in this folder is **API surface** between the two processes.
A change here almost always requires a coordinated change in both
[`modules/hook`](../hook/README.md) and [`modules/daemon`](../daemon/README.md).
See [`docs/ipc.md`](../../docs/ipc.md) for the full inter-process contract.

---

## Connected Documentation

- [Central Repository README](../../README.md)
- [NVSDK NGX Module](./nvsdk_ngx/README.md)
- [Hook Subsystem](../hook/README.md)
- [Daemon Subsystem](../daemon/README.md)