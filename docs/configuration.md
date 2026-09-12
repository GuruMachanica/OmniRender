# Configuration

OmniRender uses a layered configuration system. From lowest to highest
precedence:

1. **Built-in defaults** in [`modules/common/config.cpp`](../modules/common/config.cpp).
2. **Global config** at `%USERPROFILE%\.omnirender\config.toml`.
3. **Per-game profile** at `%USERPROFILE%\.omnirender\profiles\<hash>.toml`,
   where `<hash>` is the FNV-1a 64-bit hash of the game executable.
4. **Environment variables** with the `OMNIRENDER_` prefix.

Every layer overrides the previous. The daemon loads all four at
startup; the renderer reads the merged result.

## File format

The config is a subset of TOML. Supported features:

- `#` comments
- `[section]` headers
- `key = "string"`, `key = 1`, `key = 1.0`, `key = true | false`
- inline `#` comments (with quoted-string awareness)

The parser is intentionally minimal. Anything beyond the above is
ignored.

## Schema (default profile)

```toml
[renderer]
upscaler = "auto"        # auto | dlss | fsr | cas | off
quality  = "quality"     # performance | balanced | quality | ultra
sharpen  = 0.25          # float in [0.0, 1.0]
output_scale = "screen"  # native | screen | quality | ultra | custom
output_width = 2560      # "custom" mode only
output_height = 1440     # "custom" mode only

[temporal]
enabled = true
history = 8              # integer
motion  = "auto"         # auto | reproject | optical_flow

[hud]
exclude = true

[pipeline]
enable_reconstruction = true
enable_upscale        = true
enable_tonemap        = true
enable_fsr            = true   # vendor-neutral FSR 1.0 fallback upscaler
enable_dlss           = false  # NVIDIA NGX; falls back to FSR when absent
enable_xess           = false  # Intel XeSS (libxess_dx11.dll); falls back to FSR when absent
```

### Output resolution policy

The game renders at its native swapchain size (the *input*). The daemon
decides what to *present* — this is what makes upscaling actually engage:

| `renderer.output_scale` | Presented resolution                     |
|-------------------------|------------------------------------------|
| `native`                | same as the game (no scaling)            |
| `screen` (default)      | the desktop resolution of the overlay    |
| `quality`               | input × 1.5                              |
| `ultra`                 | input × 2.0                              |
| `custom`                | `renderer.output_width` × `output_height`|

The presented resolution is never allowed to drop below the input
(downscaling is out of scope). Backend selection order: DLSS when
available and enabled, else XeSS when available and enabled
(`libxess_dx11.dll` placed next to the daemon — real neural
reconstruction via `xessD3D11Execute`), else FSR 1.0 (EASU + RCAS) on
any GPU, else passthrough. The active backend and the input→output
resolution are shown live in the HUD.

A sample profile for Skyrim is at
[`docs/profiles/skyrim.toml`](./profiles/skyrim.toml).

## Per-game profiles

Profiles are stored by executable hash so the same `.exe` always
gets the same profile even if it moves. To find your game's hash:

```bat
:: Windows
for /f "delims=" %i in ('powershell -NoProfile -Command "[System.IO.File]::ReadAllText('C:\Path\To\Game.exe').Substring(0, 4096) | %{[BitConverter]::ToString((Get-FileHash 'C:\Path\To\Game.exe' -Algorithm SHA1).Hash).Replace('-','')}"') do @echo %i
```

Or in the daemon log, look for a line like:

```
config: no profile found for <exe>; using defaults
```

The daemon will create a default profile on first launch and print
the path so you can edit it.

## Environment overrides

Every key can be overridden with an environment variable. The
mapping rule:

- Strip the section name; keep just the leaf key.
- Replace `.` and `-` with `_`.
- Upper-case.
- Prefix with `OMNIRENDER_`.

Examples:

| Config key           | Environment variable                |
|----------------------|-------------------------------------|
| `renderer.upscaler`  | `OMNIRENDER_RENDERER_UPSCALER`      |
| `temporal.history`   | `OMNIRENDER_TEMPORAL_HISTORY`       |
| `pipeline.enable_dlss`| `OMNIRENDER_PIPELINE_ENABLE_DLSS`   |

This is how the existing `OMNIRENDER_MAX_WORK_WIDTH`,
`OMNIRENDER_ENABLE_RECONSTRUCTION`, etc. knobs fit in.

## Programmatic access (C++)

```cpp
#include "common/config.h"

omnirender::config::ConfigStore cfg =
    omnirender::config::LoadFullConfig(/* exe_path = */ argv[0]);

bool upscale = cfg.GetBool("pipeline.enable_upscale", true);
int  history = cfg.GetInt ("temporal.history",       8);
std::string upscaler = cfg.GetString("renderer.upscaler", "auto");
```

## Validation

`test_config` validates the parser, the merge order, the env-var
override, the file hash, the round-trip save/load, and the default
profile. Run:

```bat
build\tests\Release\test_config.exe
```

## Compatibility

The TOML subset is forward-compatible with full `toml++` if we
ever need it. The current parser handles everything OmniRender
reads and writes, and the file format is identical to what
`toml++` would produce.
