# OmniRender Compatibility Database

Community-maintained results from testing real games through OmniRender.
Fill a row for every game you test (see `docs/real_game_testing.md` for the
per-stage method — capture → depth/motion → upscaling → soak). Keep entries
honest: a ❌ with a note is worth more than a ✓ without evidence.

Status vocabulary:
- **Capture** — overlay shows the game 1:1 at native res (Stage 1).
- **Depth** — F12 depth view is coherent (walls gradient, sky saturates).
- **Motion** — F12 motion view lights edges on camera pan, silent when still.
- **Upscaler** — read the HUD `Backend:` line, not the config (`auto` may
  fall back to FSR). `Passthrough` = no upscaler attached.

| Game | API | Capture | Depth | Motion | Upscaler tested | OmniRender version | Notes |
|------|-----|---------|-------|--------|-----------------|--------------------|-------|
| *(example)* Generic D3D9 title | D3D9 | ✅ | ✅ D24S8 | ✅ VP | FSR 1.0 | 0.8.0-alpha | HUD excluded via reactive mask; 2h soak clean |
| *(example)* Generic D3D11 title | DXGI | ✅ | ✅ tracked DSV | ⚠️ optical flow | FSR 1.0 | 0.8.0-alpha | no engine matrices → flow fallback; ±1 px motion, mild softness in fast pans |
| *(example)* Generic OpenGL title | OpenGL | ✅ CPU path | ✅ CPU depth | ✅ fixed-function stacks | FSR 1.0 | 0.8.0-alpha | interop absent → CPU channel; shaders-only matrix engines show empty motion |

## Engine-specific gotchas

- **D3D9 depth→R32F StretchRect refusals**: some drivers reject the copy
  (logged at hook init: `depth capture disabled`). Depth shows ❌ but
  everything else still works — motion falls back to camera reprojection
  with mid-depth approximation.
- **GL shaders-only matrix games**: engines that never touch the
  fixed-function `GL_MODELVIEW/PROJECTION` stacks publish no matrices;
  motion shows ❌ and temporal reduces to disocclusion-only handling.
- **Exclusive fullscreen**: run the game `-windowed` or `-borderless`;
  the overlay cannot composite over true exclusive fullscreen.
- **In-game overlays** (Steam, Discord, RTSS): inject before OmniRender or
  expect double-processed HUD elements; the reactive mask usually absorbs
  them.

## Reporting a new entry

Open a PR touching only this file with: game name/version, GPU + driver,
API, per-column status, OmniRender version, and the notes column with
anything unusual (log excerpts live in `logs/omnirender.log`).
