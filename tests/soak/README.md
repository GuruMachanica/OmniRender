# `tests/soak` — Long-Run Soak & Stress Harness

The `tests/soak` directory contains the automated soak and stress testing harness (`OmniRenderSoak.exe`) for OmniRender. It runs prolonged execution sessions of the rendering daemon alongside the test host to validate long-term stability, memory leak freedom, working-set limits, and frame rate consistency.

---

## Files

| File | Purpose |
|------|---------|
| [`soak_main.cpp`](./soak_main.cpp) | Entry point for the soak testing harness. Spawns `OmniRenderDaemon.exe` and `OmniRenderTestHost.exe`, continuously monitors memory working sets via Windows PSAPI, tracks frame timings, and outputs summary statistics for CI. |

---

## Key Metrics Monitored

1. **Working-Set Memory Stability**:
   - Monitors working set size of both client host and background daemon at regular intervals (default 500 ms).
   - Validates NFR-1 and NFR-2 constraints (host memory footprint overhead < 50 MB, daemon stability under 1.2 GB).
2. **Frame Pacing & Drop Detection**:
   - Ensures continuous presentation over multi-minute sessions (default 60 seconds or configurable soak windows).
3. **Automated CI Integration**:
   - Emits a machine-parseable `OMNIRENDER_SOAK_SUMMARY` string reporting peak memory, average FPS, and exit status.

---

## Usage

```bat
:: Run soak test for 60 seconds using binaries in the build directory
OmniRenderSoak.exe --duration-seconds 60 --bin-dir .\build\bin\Release

:: Custom sample interval
OmniRenderSoak.exe --duration-seconds 300 --sample-ms 250
```

---

## Connected Documentation

- [Tests Suite README](../README.md)
- [Central Repository README](../../README.md)
- [QA Verification Guide](../../docs/qa.md)
