# OmniRender Architecture

## Overview

OmniRender is an out-of-process neural post-processing and temporal reconstruction engine for legacy 3D games (DirectX 8/9/10/11, OpenGL, and Vulkan). It decouples graphics hooking from neural inference using a dual-process client/daemon architecture so a 32-bit game never has to host neural network runtimes in its own virtual address space.

---

## Layered System Architecture

OmniRender is structured into clean, decoupled layers with strict separation of concerns:

```text
┌──────────────────────────────────────────────────────────────────┐
│                           APPLICATION                            │
│           Legacy 3D Games (x86/x64) · Control Center UI          │
└─────────────────────────────────┬────────────────────────────────┘
                                  │ Interception & Surface Capture
                                  ▼
┌──────────────────────────────────────────────────────────────────┐
│                   CAPTURE SUBSYSTEM (In-Process)                 │
│      Windows: D3D9 / D3D11 / DXGI Proxy DLL                      │
│      Linux: Vulkan Implicit Layer / OpenGL (Planned)             │
│      macOS: Metal / GPTK Layer (Planned)                         │
└─────────────────────────────────┬────────────────────────────────┘
                                  │ Zero-Copy Cross-Process Handle
                                  ▼
┌──────────────────────────────────────────────────────────────────┐
│                  SHARED MEMORY IPC & SPSC RING                   │
│   Lock-Free Modulo Ring Buffer · Fixed-Width 64-bit ABI Primitives│
└─────────────────────────────────┬────────────────────────────────┘
                                  │
                                  ▼
┌──────────────────────────────────────────────────────────────────┐
│                    OMNIRENDER DAEMON ENGINE                      │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    CORE (100% Platform-Independent)        │  │
│  │  • FrameContext, FrameValidity, Resolution, FrameTiming    │  │
│  │  • GpuTexture, GpuBuffer, ResourceHandle abstractions      │  │
│  │  • CameraState, JitterState (Halton 2,3)                   │  │
│  │  • HistoryManager (Temporal State Machine)                 │  │
│  │  • RenderGraph (DAG Execution Scheduler)                   │  │
│  │  • RuntimeCapabilities (Platform & GraphicsApi)            │  │
│  └─────────────────────────────┬──────────────────────────────┘  │
│                                │                                 │
│  ┌─────────────────────────────┴──────────────────────────────┐  │
│  │             RUNTIME (Pipeline & Capability Resolver)       │  │
│  │  • BackendResolver (Auto / DLSS / XeSS / FSR / Omni)        │  │
│  │  • Pipeline (Multi-stage DAG builder & frame commit)       │  │
│  └─────────────────────────────┬──────────────────────────────┘  │
│                                │                                 │
│  ┌─────────────────────────────┴──────────────────────────────┐  │
│  │                GRAPHICS HARDWARE ABSTRACTION (HAL)         │  │
│  │  • IGraphicsDevice, ICommandContext, IGraphicsTexture      │  │
│  │  • Direct3D 11 Backend (Windows)                           │  │
│  │  • Vulkan 1.3 Backend (Linux & Windows)                    │  │
│  │  • Metal Backend (macOS)                                   │  │
│  └─────────────────────────────┬──────────────────────────────┘  │
│                                │                                 │
│  ┌─────────────────────────────┴──────────────────────────────┐  │
│  │             RECONSTRUCTION BACKENDS (IReconstructionBackend│  │
│  │  • NVIDIA DLSS (Streamline / NGX)                          │  │
│  │  • Intel XeSS (Cross-vendor DP4a & XMX)                    │  │
│  │  • AMD FSR 1.0 / 2.x / 3.x (Compute Shaders)               │  │
│  │  • OmniRender Native YCoCg Temporal Accumulation           │  │
│  └────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────┬────────────────────────────────┘
                                  │ Low-Latency Flip-Model
                                  ▼
┌──────────────────────────────────────────────────────────────────┐
│                      PRESENTATION OVERLAY                        │
│   Windows DXGI Waitable Swapchain · Wayland / Gamescope (Linux)  │
└──────────────────────────────────────────────────────────────────┘
```

---

## Dual-Process Topology

```text
              LEGACY GAME PROCESS (x86 / x64)             
   Working set < 1.2 GB (preserves 32-bit VA ceiling)    
                                                          
   omnirender-hook.dll (injected proxy)                  
    Intercepts Present() / SwapBuffers() / vkQueuePresent
    Extracts color + depth shared surface handles         
    Calls IDXGIResource::GetSharedHandle / DMA-BUF export  
    Posts frames to lock-free SPSC ring buffer            

                           Zero-copy GPU VRAM surface handles
                          

                OMNIRENDER DAEMON (x86_64)                 
                                                          
   BackendResolver   → Negotiates DLSS / XeSS / FSR / Omni
   ipc_server        → Reads ring buffer, signals new frame
   IGraphicsDevice   → OpenSharedTexture (zero GPU copy)   
   Motion pass       → Camera reprojection motion vectors  
   Reactive pass     → Color divergence reactive mask      
   Disocclusion pass → Geometric depth difference rejection
   Reconstruction    → IReconstructionBackend execution    
   Tonemap pass      → Extended Reinhard Auto-HDR          
   Presentation      → Borderless waitable flip-model      
```

---

## Cross-Platform Roadmap

| Platform | Hook Mechanism | Surface Sharing | Graphics HAL | Presentation |
|---|---|---|---|---|
| **Windows** | Proxy DLL (`d3d9.dll`, `dxgi.dll`, `opengl32.dll`) | DXGI Shared Handles | Direct3D 11 / D3D12 | DXGI 1.3 Waitable Swapchain |
| **Linux / SteamOS** | Vulkan Implicit Layer (`libomnirender-hook.so`) | Linux DMA-BUF (`dma_buf` fd via `AF_UNIX`) | Vulkan 1.3 | Wayland `wl_surface` / Gamescope |
| **macOS** | Dynamic Loader / GPTK Interposition | Apple `IOSurfaceRef` (`IOSurfaceID`) | Metal 3 | `CAMetalLayer` Overlay |

---

## Isolation & Performance Guarantees

- **Crash Isolation (NFR-2)**: A GPU timeout (TDR) or crash in the daemon never takes down the game. The hook detects daemon absence and reverts to passthrough within 1 frame.
- **Memory Overhead (FR-1.2)**: Hook RAM usage is strictly bounded under 50 MB, preserving 32-bit game memory.
- **Pure OS IPC**: Communication is performed exclusively through standard OS primitives (memory-mapped files, named events / eventfds, and zero-copy shared texture handles). No kernel drivers are required.

---

## Connected Documentation

- [Documentation Index](index.md)
- [Central Repository README](../README.md)
- [Core Architecture](../core/README.md)
- [Graphics HAL](../graphics/README.md)
- [Runtime Engine](../runtime/README.md)
- [IPC Specification](ipc.md)