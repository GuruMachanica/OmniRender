# GitHub Packages

OmniRender publishes a Windows container image to the
**GitHub Container Registry (GHCR)** on every `v*.*.*` tag.

## Image

```
ghcr.io/guru-machanaica/omnirender:v1.0.0
ghcr.io/guru-machanaica/omnirender:1.0
ghcr.io/guru-machanaica/omnirender:latest
```

## What's inside

A multi-stage build produces a runtime image containing:

- `OmniRenderDaemon.exe` — 64-bit processing host.
- `omnirender-hook.dll`  — client injected proxy DLL.
- `shaders/`             — pre-compiled DXIL blobs (cs_6_5).

> NVIDIA proprietary runtimes (`nvngx_dlss.dll`, `sl.interposer.dll`,
> `sl.d3d11.dll`) are not bundled. Mount a host directory containing
> them, or rely on a system-wide install.

## Pull

```bat
docker pull ghcr.io/guru-machanaica/omnirender:v1.0.0
```

## Run (Windows host)

```bat
docker run --rm -it ^
    --name omnirender ^
    -v %CD%\models:C:\omnirender\models ^
    -v %CD%\dlss:C:\omnirender\dlss:ro ^
    ghcr.io/guru-machanaica/omnirender:v1.0.0
```

## Publishing a new release

1. Bump version in `CMakeLists.txt` (`project(OmniRender VERSION ...)`)
   and add an entry in [`CHANGELOG.md`](../CHANGELOG.md).
2. Commit on `main` and push a `vMAJOR.MINOR.PATCH` tag:

   ```bat
   git tag -a v1.1.0 -m "Release 1.1.0"
   git push origin v1.1.0
   ```

3. The `.github/workflows/publish-image.yml` workflow runs automatically
   and publishes new image tags to GHCR.

## Visibility

The image is **public** so that downstream `docker pull` and
`FROM ghcr.io/guru-machanaica/omnirender` references work without a
GitHub PAT. To make it private, set the package visibility in
**Settings → Packages**.
