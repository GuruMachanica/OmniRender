# Roadmap — v0.3.0-alpha

This is the immediate build-more path for the bounded pipeline.

## Already landed

- Hook-side D3D9 + DXGI capture with shared handles
- IPC ring with explicit slot states
- Daemon-side bounded processing layer
- Daemon-side pipeline orchestrator
- Passthrough degradation path
- Shader sources for depth linearization, upscale, reconstruction, and
  HDR tone fallback
- Hook-side depth format awareness

## Next build steps

1. **Shader compilation integration**
   - Make the CMake CSO path robust enough that the bounded shaders are
     compiled automatically when DXC is available.
   - Keep the passthrough fallback behavior when CSO is missing.

2. **Depth linearization wiring**
   - Feed the shared depth handle into the linearize shader once it is
     compiled.
   - Pass the linearized depth into the reconstruction module.

3. **Temporal reconstruction wiring**
   - Store the previous processed color into the bounded history.
   - Load previous color + current color into the reconstruction shader.
   - Add a simple disagreement / ghosting mask so the first temporal pass
     stays stable.

4. **Upscale quality pass**
   - Improve the Lanczos-style kernel or replace it with a higher-quality
     bounded filter.
   - Keep the internal working resolution clamped so VRAM stays bounded.

5. **Tonemap wiring**
   - Dispatch the HDR tonemap shader when enabled and when it is compiled
     in.
   - Expose exposure/key/saturation as tunable defaults before exposing a
     full config stack.

6. **Passthrough polish**
   - Make sure the passthrough path is indistinguishable from "pipeline
     failed once": daemon stays up, frame flow stays continuous, logs stay
     useful.

## VRAM discipline

All of the above should stay inside the current VRAM discipline:

- one bounded work texture
- small constant buffers
- bounded history ping-pong
- clamp large source resolutions instead of allocating more buffers
- degrade gracefully when VRAM is tight

## Out of scope for v0.3.0-alpha

These are still deferred:

- DLSS / FSR / XeSS
- TensorRT neural tonemap
- full motion-vector synthesis
- OpenGL / Vulkan capture
- manager UI, per-game profiles, benchmark mode

They can land later, but the first priority is a usable bounded path
that works without external SDKs.
