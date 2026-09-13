# Real-Game Testing Guide

How to validate OmniRender end-to-end on actual games — the step CI cannot
do for you. Everything here assumes a **Release build** with the daemon,
hook, and launcher targets compiled.

## Quick start

1. Collect binaries into one directory (the install layout already does
   this — `bin/`):

   ```
   bin/
     OmniRenderDaemon.exe
     OmniRenderLaunch.exe
     d3d9.dll          <- hook proxy (emitted by the hook build)
     dxgi.dll          <- hook proxy
     opengl32.dll      <- hook proxy
     shaders/temporal/*.cso
   ```

2. Launch a game through the launcher:

   ```
   OmniRenderLaunch.exe "C:\Games\OldGame\game.exe" [game args...]
   ```

   The launcher:
   - backs up any original `d3d9.dll`/`dxgi.dll`/`opengl32.dll` the game
     shipped with to `<name>.omnirender.bak` (first run only, idempotent),
   - drops the matching OmniRender proxy DLLs next to the game exe,
   - writes a default `config.toml` next to the launcher (never overwrites
     an existing one — edit it to change `output_scale`, backend, etc.),
   - starts the daemon with the game exe path (per-game config hashing),
   - resumes the game.

3. The OmniRender overlay window appears once the first frame is captured.
   - **F11** — toggle the HUD (FPS, frametimes, backend, resolution).
   - **F12** — cycle the frame debugger:
     `Final Output → Linearized Depth → Motion Vectors → Reactive Mask →
     Disocclusion Mask → History Buffer`.

## What "working" looks like — per stage

### Stage 1: passthrough (do this FIRST, before upscalers)

Set in `config.toml`:

```toml
[renderer]
upscaler = "off"
output_scale = "native"
```

Success criteria:
- The overlay shows the game's image 1:1, no visible latency added.
- HUD shows `Backend: Passthrough` and input == output resolution.
- The game runs for **hours** without artifacts, black flashes, or a
  disappearing overlay (that validates the keyed-mutex + fence protocol).

### Stage 2: depth + motion (the frame debugger)

Press **F12** twice to view **Linearized Depth**:
- Walls near = dark, far = bright (or the inverse if the game uses
  reversed-Z — either is fine as long as it is *coherent*).
- Sky / no-geometry pixels saturate.
- If the view is pure black or pure noise: depth capture is not working
  for this game. Log it in the compatibility table (see below).

Press **F12** three times for **Motion Vectors**:
- Pan the camera: moving edges must light up, static scene must stay black.
- If a static scene shows noise: reprojection matrices are wrong (D3D9/GL
  games) or the MV sign convention is flipped — file the game's
  depth/motion status.

### Stage 3: upscaling

```toml
[renderer]
upscaler = "auto"
output_scale = "quality"   # 1.5x, aspect-true
```

Success criteria:
- HUD `Resolution:` line shows `1280 x 720 -> 1920 x 1080` (or per your
  scale), and `Backend:` shows DLSS / XeSS / FSR 1.0.
- Text in-game at the *upscaled* resolution looks no worse than native;
  FSR/RCAS should sharpen edges vs. plain bilinear.

### Stage 4: stability soak

- Play 2+ hours with upscaling on, HUD visible.
- Watch `logs/omnirender.log` for repeated `OpenSharedTexture(color)
  failed` or `init failed` warnings — either is a protocol bug, not a
  game quirk. Report with the log excerpt.
- Alt-tab, change game resolution mid-run, unplug/replug a monitor
  (device loss path). The pipeline must re-init on its own (backoff
  retries forever, capped at 30 s).

## Per-game compatibility table

Add a row to `docs/compatibility.md` (create it from the template below)
for every game you test. This is the project's most valuable asset —
please keep it honest.

| Game | API | Capture | Depth | Motion | Upscaler | Notes |
|------|-----|---------|-------|--------|----------|-------|
| Example Game | D3D9 | ✅ | ✅ D24S8 | ✅ VP hook | FSR 1.0 | HUD overlays double-processed, disable `enable_reactive_mask` |

Status vocabulary:
- **Capture** — overlay shows the game at native res (Stage 1).
- **Depth** — depth debug view is coherent (Stage 2).
- **Motion** — motion debug view lights edges, quiet when still (Stage 2).
- **Upscaler** — which backend actually engaged (read the HUD, not the
  config: `auto` may fall back to FSR).

## Known limitations

- **OpenGL** games get depth via a CPU readback channel
  (`glReadPixels(GL_DEPTH_COMPONENT)`) and camera matrices from the fixed-
  function `GL_MODELVIEW/PROJECTION` stacks. Games using shaders-only
  matrix uploads (custom engine Uniform uploads to `glUniformMatrix4fv`
  that never touch the classic stacks) will show no motion vectors —
  depth still works. Zero-copy interop (WGL_NV_DX_interop2) shares color
  on GPU; depth always rides the CPU channel.
- **D3D9 depth** depends on the driver allowing depth→R32F `StretchRect`
  (probed at init; disabled honestly when refused — depth shows invalid).
- **D3D8** games are supported through D3D8to9-style wrappers feeding
  the D3D9 proxy.
- The overlay presents in a separate borderless window; exclusive
  fullscreen games need `-windowed`/`-borderless` flags (most old games
  accept one of the two).

## Where the logs are

- `logs/omnirender.log` (daemon) — pipeline lifecycle, backend attach,
  per-frame warnings.
- Hook-side logging goes through `OutputDebugStringA`: attach DebugView
  or run the game under a debugger to see it.
