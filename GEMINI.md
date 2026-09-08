# OmniRender Project Guidelines

## Invariants
- **Strict 300 LOC Limit**: All source files (`.cpp`, `.h`, `.hpp`, `.hlsl`) must strictly be under 300 lines of code. Decompose any file exceeding this limit into cohesive, single-responsibility modules without altering external interfaces or breaking functionality.
- **Production Safety**: Ensure D3D scene and resource lifecycles (`BeginScene`/`EndScene`, `Reset`, device changes) are hardened.
