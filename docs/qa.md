# QA Acceptance Criteria

These are the verification tests defined in PRD §8. Each test has a
stable identifier (e.g. `QA-01`) and an objective, scenario, and
acceptance gate.

| ID | Objective | Test Scenario | Acceptance Criteria |
|----|-----------|---------------|---------------------|
| **QA-01** | Address Space Safety | Launch *Assassin's Creed (2007)* at 1080p native with OmniRender active for 60 minutes. | Target game working set RAM remains ≤ 1,350 MB. 0 instances of `0xC0000005` (Access Violation) or `DXGI_ERROR_DEVICE_REMOVED`. |
| **QA-02** | Hardware Profile Gating | Execute test suite on an RTX 4050 Laptop and simulate low memory (capped via driver profile to 2 GB). | System switches gracefully to `PROFILE_SPATIAL_FALLBACK`. Peak VRAM usage stays strictly below 950 MB. |
| **QA-03** | Temporal Reconstruction | Verify DLSS reconstruction quality from 720p internal input to 1080p target display. | Target frame time ≤ 4.2 ms. Edge stability on Altaïr's robes displays 0 jittering or moiré pattern artifacts. |
| **QA-04** | Broad API Compatibility | Run target binary against *Doom 3* (OpenGL), *Tomb Raider 1996* (Glide/dgVoodoo2), and *Skyrim* (DirectX 9). | Shared texture bridge captures backbuffer across all three underlying graphics wrappers without black screen hangs. |

## Performance budget (NFR-1)

| Stage | Target (1080p → 1440p, RTX 4050) |
|-------|----------------------------------|
| Inter-process transmission | < 0.4 ms |
| Optical flow compute | < 1.2 ms |
| DLSS reconstruction | < 3.0 ms |
| Neural inverse tonemap | < 0.8 ms |
| Presentation | < 0.6 ms |
| **Total** | **< 6.0 ms** |

## VRAM budgets (NFR-3)

| Profile | Peak VRAM |
|---------|----------|
| `PROFILE_RTX_TEMPORAL`     | < 2.2 GB |
| `PROFILE_SPATIAL_FALLBACK` | < 950 MB |

## Crash isolation (NFR-2)

A `DXGI_ERROR_DEVICE_REMOVED` (TDR) or unhandled exception inside
`OmniRenderDaemon.exe` must **not** terminate the legacy game
process. Verification:

1. Force a TDR via `dxcap -forcetdr` or `!nvidia-smi -lgc` while
   OmniRender is processing frames.
2. Observe the daemon exit with a non-zero code.
3. Observe the legacy game process still running with a captured
   backbuffer on the latest frame.

## How to run

```bat
:: 1. Configure
cmake -B build -A x64 -DOMNIRENDER_USE_NGX=ON -DOMNIRENDER_USE_TRT=ON -DOMNIRENDER_BUILD_TESTS=ON
cmake --build build --config Release

:: 2. Run unit tests
ctest --test-dir build -C Release --output-on-failure

:: 3. Run manual QA scenarios (game required)
build\modules\daemon\Release\OmniRenderDaemon.exe
:: Then launch the target game with omnirender-hook.dll injected.
```

## Reporting

Open a GitHub issue with the QA ID in the title (e.g. "QA-01 failure
on Ryzen 5 3600"). Include:

- GPU model + driver version
- Game + version
- `OmniRenderDaemon.log` (set `OMNI_LOG_DEBUG=1` env var for verbose)
- Working set peak observed via Task Manager → Details → Working Set