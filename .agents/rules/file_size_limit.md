# Rule: Maximum 300 Lines of Code (LOC) per File

## Invariant
- **Strict Size Ceiling**: Every source code file (`.cpp`, `.h`, `.hpp`, `.hlsl`, `.c`, etc.) must strictly contain **fewer than 300 lines of code**.
- **Proactive Decomposition**: If any file reaches or exceeds 300 LOC, it must immediately be decomposed into cohesive, single-responsibility submodules.
- **Functional & API Integrity**: Decomposition must never alter public APIs, break ABI stability, or disrupt existing functionality.

## Decomposition Guidelines
1. **Data Tables & Constants**: Move large constant tables (e.g. font glyphs, bytecode arrays, look-up tables) into dedicated private header files (e.g. `<module>_font.h`, `<module>_bytecode.h`).
2. **Distinct Responsibilities**: Separate rendering passes, HUD overlays, IPC interfaces, and hardware negotiation into dedicated translation units.
3. **No Catch-All God Files**: Prevent monolithic files that combine UI rendering, post-processing shaders, state blocks, and IPC management in one unit.
